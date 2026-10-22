
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

#define I2C_NODE DT_NODELABEL(i2c0)
#define LIS3DH_WHO_AM_I 0x0F

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void blink(unsigned int count)
{
    for (unsigned int i = 0; i < count; i++) {
        gpio_pin_set_dt(&led, 1);
        k_msleep(200);
        gpio_pin_set_dt(&led, 0);
        k_msleep(200);
    }

    k_sleep(K_SECONDS(2));
}

int main(void)
{
    const struct device *i2c = DEVICE_DT_GET(I2C_NODE);
    uint8_t id = 0;
    int ret;

    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    if (!device_is_ready(i2c)) {
        while (1) {
            blink(3);
        }
    }

    /* Try both possible LIS3DH I2C addresses. */
    ret = i2c_reg_read_byte(i2c, 0x18, LIS3DH_WHO_AM_I, &id);

    if (ret != 0 || id != 0x33) {
        ret = i2c_reg_read_byte(i2c, 0x19,
                                LIS3DH_WHO_AM_I, &id);
    }

    if (ret == 0 && id == 0x33) {
        /* SUCCESS: one long flash, repeating. */
        while (1) {
            gpio_pin_set_dt(&led, 1);
            k_msleep(1000);
            gpio_pin_set_dt(&led, 0);
            k_sleep(K_SECONDS(2));
        }
    }

    /* FAILURE: two quick flashes, repeating. */
    while (1) {
        blink(2);
    }

    return 0;
}
