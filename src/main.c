#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#include "dw3000_hw.h"
#include "deca_probe_interface.h"
#include "deca_device_api.h"

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9A, 0x21, 0x5E, 0x7D)
};

static const struct bt_data scan_response[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
        "DWM3001-TRACKER",
        sizeof("DWM3001-TRACKER") - 1)
};

int main(void)
{
    printk("\n====================================\n");
    printk("       DWM3001 TRACKER FIRMWARE\n");
    printk("====================================\n");

    printk("\n[1] Initializing DW3110...\n");

    int ret = dw3000_hw_init();

    if (ret != 0) {
        printk("ERROR: UWB initialization failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    dw3000_hw_reset();
    k_msleep(10);

    int32_t probe_result =
        dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    if (probe_result != DWT_SUCCESS) {
        printk("ERROR: DW3110 identification failed.\n");
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    uint32_t device_id = dwt_readdevid();

    printk("SUCCESS: DW3110 initialized.\n");
    printk("Device ID: 0x%08X\n", (unsigned int)device_id);

    printk("\n[2] Initializing Bluetooth...\n");

    ret = bt_enable(NULL);

    if (ret != 0) {
        printk("ERROR: Bluetooth initialization failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("SUCCESS: Bluetooth initialized.\n");

    printk("\n[3] Starting tracker advertising...\n");

    ret = bt_le_adv_start(
        BT_LE_ADV_CONN_FAST_1,
        advertising_data,
        ARRAY_SIZE(advertising_data),
        scan_response,
        ARRAY_SIZE(scan_response)
    );

    if (ret != 0) {
        printk("ERROR: BLE advertising failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("SUCCESS: Tracker is advertising!\n");
    printk("Device name: DWM3001-TRACKER\n");
    printk("Service UUID: 7d5e219a-8048-4f6b-2d91-3ca701c40210\n");

    printk("\n====================================\n");
    printk("       TRACKER READY\n");
    printk("====================================\n");

    while (1) {
        k_sleep(K_SECONDS(10));
    }
}
