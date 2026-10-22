
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const char device_name[] = "StickerTracker";

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            device_name,
            sizeof(device_name) - 1),
};
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

    if (!gpio_is_ready_dt(&led)) {
        return 0;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    printk("Starting StickerTracker BLE test\n");

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

    /* One flash indicates advertising started successfully. */
    gpio_pin_set_dt(&led, 1);
    k_msleep(500);
    gpio_pin_set_dt(&led, 0);

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}
