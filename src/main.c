#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "dw3000_hw.h"
#include "deca_probe_interface.h"
#include "deca_device_api.h"

int main(void)
{
    printk("\n");
    printk("====================================\n");
    printk("       DWM3001CDK UWB TEST\n");
    printk("====================================\n");

    /* Step 1: Initialize GPIO and SPI */
    printk("\n[1] Initializing DW3000 hardware...\n");

    int ret = dw3000_hw_init();

    if (ret != 0) {
        printk("ERROR: Hardware initialization failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("SUCCESS: Hardware initialized.\n");

    /* Step 2: Reset the DW3110 */
    printk("\n[2] Resetting DW3110...\n");

    dw3000_hw_reset();
    k_msleep(10);

    printk("Reset sequence completed.\n");

    /* Step 3: Probe the DW3110 */
    printk("\n[3] Probing UWB transceiver...\n");

    int32_t probe_result =
        dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    if (probe_result != DWT_SUCCESS) {
        printk("ERROR: DW3000 driver did not recognize the device.\n");
        printk("Probe result: %d\n", probe_result);

        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("SUCCESS: DW3000 driver recognized the device.\n");

    /* Step 4: Read device ID */
    uint32_t device_id = dwt_readdevid();

    printk("\n[4] DEVICE IDENTIFICATION\n");
    printk("Device ID: 0x%08X\n", (unsigned int)device_id);

    if ((device_id & 0xFFFFFF0FUL) == (0xDECA0312UL & 0xFFFFFF0FUL)) {
        printk("SUCCESS: Device ID matches the DW3000 PDOA driver.\n");
    } else {
        printk("WARNING: Device ID does not match expected value.\n");
    }

    printk("\n====================================\n");
    printk("       INITIAL UWB TEST COMPLETE\n");
    printk("====================================\n");

    while (1) {
        k_sleep(K_SECONDS(5));
    }

    return 0;
}
