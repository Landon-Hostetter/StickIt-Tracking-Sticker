
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* Hardware: LIS3DH on I2C0, interrupt on P0.11 */
#define LIS3DH_ADDR       0x18
#define LIS3DH_WHO_AM_I   0x0F
#define LIS3DH_CTRL_REG1  0x20
#define LIS3DH_CTRL_REG2  0x21
#define LIS3DH_CTRL_REG3  0x22
#define LIS3DH_CTRL_REG4  0x23
#define LIS3DH_CTRL_REG5  0x24
#define LIS3DH_INT1_CFG   0x30
#define LIS3DH_INT1_SRC   0x31
#define LIS3DH_INT1_THS   0x32
#define LIS3DH_INT1_DUR   0x33

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const struct gpio_dt_spec motion_int = {
    .port = DEVICE_DT_GET(DT_NODELABEL(gpio0)),
    .pin = 11,
    .dt_flags = GPIO_ACTIVE_HIGH,
};

static struct gpio_callback motion_cb;
static struct k_sem motion_sem;

static const char device_name[] = "StickerTracker";

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            device_name,
            sizeof(device_name) - 1),
};

static int lis3dh_write(uint8_t reg, uint8_t value)
{
    return i2c_reg_write_byte(i2c_dev, LIS3DH_ADDR, reg, value);
}

static int lis3dh_read(uint8_t reg, uint8_t *value)
{
    return i2c_reg_read_byte(i2c_dev, LIS3DH_ADDR, reg, value);
}

static void motion_isr(const struct device *port,
                       struct gpio_callback *cb,
                       uint32_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    k_sem_give(&motion_sem);
}

static int lis3dh_init(void)
{
    uint8_t who_am_i;
    int err;

    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready\n");
        return -ENODEV;
    }

    err = lis3dh_read(LIS3DH_WHO_AM_I, &who_am_i);
    if (err || who_am_i != 0x33) {
        printk("LIS3DH not detected: err=%d ID=0x%02x\n",
               err, who_am_i);
        return -EIO;
    }

    /* Power down before configuring. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG1, 0x00)) < 0) {
        return err;
    }

    /* High-pass filter enabled; route filtered signal to interrupt. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG2, 0x01)) < 0) {
        return err;
    }

    /* Route INT1 generator to physical INT1 pin. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG3, 0x40)) < 0) {
        return err;
    }

    /* ±2g range, high-resolution disabled. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG4, 0x00)) < 0) {
        return err;
    }

    /* Latch interrupt until INT1_SRC is read. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG5, 0x08)) < 0) {
        return err;
    }

    /* Threshold and duration from the working motion test. */
    if ((err = lis3dh_write(LIS3DH_INT1_THS, 0x10)) < 0) {
        return err;
    }

    if ((err = lis3dh_write(LIS3DH_INT1_DUR, 0x00)) < 0) {
        return err;
    }

    /* Enable high-axis motion events on X, Y and Z. */
    if ((err = lis3dh_write(LIS3DH_INT1_CFG, 0x2A)) < 0) {
        return err;
    }

    /* 100 Hz, XYZ enabled. */
    if ((err = lis3dh_write(LIS3DH_CTRL_REG1, 0x57)) < 0) {
        return err;
    }

    /* Clear any pending interrupt. */
    uint8_t source;
    (void)lis3dh_read(LIS3DH_INT1_SRC, &source);

    printk("LIS3DH initialized\n");
    return 0;
}

static void error_blink(void)
{
    while (1) {
        for (int i = 0; i < 3; i++) {
            gpio_pin_set_dt(&led, 1);
            k_msleep(150);
            gpio_pin_set_dt(&led, 0);
            k_msleep(150);
        }
        k_sleep(K_SECONDS(2));
    }
}

int main(void)
{
    int err;
    uint8_t source;

    k_sem_init(&motion_sem, 0, 1);

    if (!gpio_is_ready_dt(&led) ||
        !gpio_is_ready_dt(&motion_int)) {
        printk("GPIO device not ready\n");
        return 0;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&motion_int, GPIO_INPUT);

    printk("Starting StickerTracker\n");

    err = lis3dh_init();
    if (err) {
        printk("LIS3DH initialization failed: %d\n", err);
        error_blink();
    }

    gpio_init_callback(&motion_cb, motion_isr, BIT(motion_int.pin));
    gpio_add_callback(motion_int.port, &motion_cb);
    gpio_pin_interrupt_configure_dt(&motion_int, GPIO_INT_EDGE_TO_ACTIVE);

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth initialization failed: %d\n", err);
        error_blink();
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                          advertising_data,
                          ARRAY_SIZE(advertising_data),
                          NULL, 0);
    if (err) {
        printk("Advertising failed: %d\n", err);
        error_blink();
    }

    printk("Advertising as StickerTracker\n");

    /* Two short flashes indicate successful startup. */
    for (int i = 0; i < 2; i++) {
        gpio_pin_set_dt(&led, 1);
        k_msleep(150);
        gpio_pin_set_dt(&led, 0);
        k_msleep(150);
    }

    while (1) {
        k_sem_take(&motion_sem, K_FOREVER);

        /* Read source to release the latched LIS3DH interrupt. */
        if (lis3dh_read(LIS3DH_INT1_SRC, &source) == 0) {
            printk("Motion detected, INT1_SRC=0x%02x\n", source);

            gpio_pin_set_dt(&led, 1);
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);
        }
    }

    return 0;
}
