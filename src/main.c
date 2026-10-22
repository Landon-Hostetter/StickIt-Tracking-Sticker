
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#define I2C_NODE DT_NODELABEL(i2c0)
#define INT_PIN 11

#define LIS3DH_ADDR_1       0x18
#define LIS3DH_ADDR_2       0x19

#define REG_CTRL1           0x20
#define REG_CTRL2           0x21
#define REG_CTRL3           0x22
#define REG_CTRL5           0x24
#define REG_INT1_CFG        0x30
#define REG_INT1_SRC        0x31
#define REG_INT1_THS        0x32
#define REG_INT1_DURATION   0x33
#define REG_WHO_AM_I        0x0F

static const struct device *i2c =
    DEVICE_DT_GET(I2C_NODE);

static const struct device *gpio0 =
    DEVICE_DT_GET(DT_NODELABEL(gpio0));

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct gpio_callback int_callback;
static struct k_sem motion_sem;

static uint8_t sensor_addr;

static int write_reg(uint8_t reg, uint8_t value)
{
    return i2c_reg_write_byte(i2c, sensor_addr, reg, value);
}

static void motion_isr(const struct device *dev,
                       struct gpio_callback *cb,
                       uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    k_sem_give(&motion_sem);
}

static void blink(unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        gpio_pin_set_dt(&led, 1);
        k_msleep(150);
        gpio_pin_set_dt(&led, 0);
        k_msleep(150);
    }

    k_sleep(K_SECONDS(2));
}

static void setup_failed(void)
{
    while (1) {
        blink(3);
    }
}

int main(void)
{
    uint8_t id = 0;
    int ret;

    k_sem_init(&motion_sem, 0, 1);

    if (!device_is_ready(i2c) ||
        !device_is_ready(gpio0) ||
        !gpio_is_ready_dt(&led)) {
        return 0;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    /* Identify the sensor and its I2C address. */
    ret = i2c_reg_read_byte(i2c, LIS3DH_ADDR_1,
                            REG_WHO_AM_I, &id);

    if (ret == 0 && id == 0x33) {
        sensor_addr = LIS3DH_ADDR_1;
    } else {
        ret = i2c_reg_read_byte(i2c, LIS3DH_ADDR_2,
                                REG_WHO_AM_I, &id);

        if (ret != 0 || id != 0x33) {
            setup_failed();
        }

        sensor_addr = LIS3DH_ADDR_2;
    }

    /* Disable sensor while configuring it. */
    if (write_reg(REG_CTRL1, 0x00) != 0) {
        setup_failed();
    }

    /* Route the high-pass filter to the INT1 generator. */
    if (write_reg(REG_CTRL2, 0x01) != 0) {
        setup_failed();
    }

    /* Threshold: 16/128 of +/-2 g, approximately 250 mg.
       Tune this after observing real-world movement. */
    if (write_reg(REG_INT1_THS, 0x10) != 0 ||
        write_reg(REG_INT1_DURATION, 0x00) != 0) {
        setup_failed();
    }

    /* Detect high acceleration events on X, Y, or Z. */
    if (write_reg(REG_INT1_CFG, 0x2A) != 0) {
        setup_failed();
    }

    /* Latch INT1 until its source register is read. */
    if (write_reg(REG_CTRL5, 0x08) != 0) {
        setup_failed();
    }

    /* Route interrupt generator 1 to the INT1 pin. */
    if (write_reg(REG_CTRL3, 0x40) != 0) {
        setup_failed();
    }

    /* Enable X, Y, Z at 10 Hz in normal mode. */
    if (write_reg(REG_CTRL1, 0x27) != 0) {
        setup_failed();
    }

    /* Clear a pending sensor interrupt before enabling GPIO. */
    if (i2c_reg_read_byte(i2c, sensor_addr,
                          REG_INT1_SRC, &id) != 0) {
        setup_failed();
    }

    if (gpio_pin_configure(gpio0, INT_PIN,
                           GPIO_INPUT | GPIO_PULL_DOWN) != 0) {
        setup_failed();
    }

    gpio_init_callback(&int_callback, motion_isr, BIT(INT_PIN));

    if (gpio_add_callback(gpio0, &int_callback) != 0 ||
        gpio_pin_interrupt_configure(gpio0, INT_PIN,
                                     GPIO_INT_EDGE_TO_ACTIVE) != 0) {
        setup_failed();
    }

    /* One startup flash means initialization succeeded. */
    gpio_pin_set_dt(&led, 1);
    k_msleep(500);
    gpio_pin_set_dt(&led, 0);

    while (1) {
        k_sem_take(&motion_sem, K_FOREVER);

        /* Reading INT1_SRC acknowledges the latched event. */
        ret = i2c_reg_read_byte(i2c, sensor_addr,
                                REG_INT1_SRC, &id);

        if (ret == 0 && (id & 0x40)) {
            blink(2);
        }
    }

    return 0;
}
