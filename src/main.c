
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

#include "dw3000_hw.h"
#include "deca_probe_interface.h"
#include "deca_device_api.h"

/* ==========================================
 * BLE UUID DEFINITIONS
 * ========================================== */

/* Service: 7d5e219a-8048-4f6b-2d91-3ca701c40210 */
static struct bt_uuid_128 tracker_service_uuid =
    BT_UUID_INIT_128(
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9A, 0x21, 0x5E, 0x7D
    );

/* Status: 7d5e219b-8048-4f6b-2d91-3ca701c40210 */
static struct bt_uuid_128 tracker_status_uuid =
    BT_UUID_INIT_128(
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9B, 0x21, 0x5E, 0x7D
    );

/* UWB ID: 7d5e219c-8048-4f6b-2d91-3ca701c40210 */
static struct bt_uuid_128 tracker_uwb_id_uuid =
    BT_UUID_INIT_128(
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9C, 0x21, 0x5E, 0x7D
    );

/* Command: 7d5e219d-8048-4f6b-2d91-3ca701c40210 */
static struct bt_uuid_128 tracker_command_uuid =
    BT_UUID_INIT_128(
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9D, 0x21, 0x5E, 0x7D
    );

/* Notifications: 7d5e219e-8048-4f6b-2d91-3ca701c40210 */
static struct bt_uuid_128 tracker_notify_uuid =
    BT_UUID_INIT_128(
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9E, 0x21, 0x5E, 0x7D
    );

/* ==========================================
 * TRACKER DATA
 * ========================================== */

static uint8_t tracker_status = 0;

/* Device ID 0xDECA0302, transmitted little-endian */
static const uint8_t uwb_device_id_bytes[] = {
    0x02, 0x03, 0xCA, 0xDE
};

/* Notification event identifiers */
#define TRACKER_EVENT_PING_ACK  0x81
#define TRACKER_EVENT_STATUS    0x82

/* Forward declaration */
static void send_tracker_notification(
    struct bt_conn *conn,
    uint8_t event,
    uint8_t status
);

/* ==========================================
 * GATT READ CALLBACKS
 * ========================================== */

static ssize_t read_tracker_status(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    void *buf,
    uint16_t len,
    uint16_t offset)
{
    return bt_gatt_attr_read(
        conn, attr, buf, len, offset,
        &tracker_status, sizeof(tracker_status)
    );
}

static ssize_t read_uwb_device_id(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    void *buf,
    uint16_t len,
    uint16_t offset)
{
    return bt_gatt_attr_read(
        conn, attr, buf, len, offset,
        uwb_device_id_bytes, sizeof(uwb_device_id_bytes)
    );
}

/* ==========================================
 * GATT WRITE CALLBACK
 * ========================================== */

static ssize_t write_tracker_command(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    const void *buf,
    uint16_t len,
    uint16_t offset,
    uint8_t flags)
{
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *command = buf;

    printk("\n[BLE] Command received: 0x%02X\n", command[0]);

    switch (command[0]) {
    case 0x01:
        printk("[BLE] PING command received.\n");

        send_tracker_notification(
            conn,
            TRACKER_EVENT_PING_ACK,
            tracker_status
        );
        break;

    case 0x02:
        printk("[BLE] STATUS request received.\n");
        printk("[BLE] Tracker status: %u\n", tracker_status);

        send_tracker_notification(
            conn,
            TRACKER_EVENT_STATUS,
            tracker_status
        );
        break;

    default:
        printk("[BLE] Unknown command.\n");
        break;
    }

    return len;
}

/* ==========================================
 * GATT SERVICE DEFINITION
 * ========================================== */

BT_GATT_SERVICE_DEFINE(
    tracker_service,

    /* Characteristic 1: Tracker status */
    BT_GATT_PRIMARY_SERVICE(&tracker_service_uuid),

    BT_GATT_CHARACTERISTIC(
        &tracker_status_uuid.uuid,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_tracker_status,
        NULL,
        NULL
    ),

    /* Characteristic 2: UWB device ID */
    BT_GATT_CHARACTERISTIC(
        &tracker_uwb_id_uuid.uuid,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_uwb_device_id,
        NULL,
        NULL
    ),

    /* Characteristic 3: Command input */
    BT_GATT_CHARACTERISTIC(
        &tracker_command_uuid.uuid,
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE,
        NULL,
        write_tracker_command,
        NULL
    ),

    /* Characteristic 4: Tracker notifications */
    BT_GATT_CHARACTERISTIC(
        &tracker_notify_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL,
        NULL,
        NULL
    ),

    /* Client Characteristic Configuration Descriptor */
    BT_GATT_CCC(
        NULL,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
    )
);

/* ==========================================
 * BLE NOTIFICATION FUNCTION
 * ========================================== */

/*
 * Attribute index 8 is the notification characteristic's
 * value attribute:
 *
 * 0: Primary service
 * 1-2: Status declaration/value
 * 3-4: UWB ID declaration/value
 * 5-6: Command declaration/value
 * 7-8: Notification declaration/value
 * 9: CCCD
 */

#define TRACKER_NOTIFY_ATTR_INDEX 8

static void send_tracker_notification(
    struct bt_conn *conn,
    uint8_t event,
    uint8_t status
)
{
    uint8_t notification_data[2] = {
        event,
        status
    };

    int ret = bt_gatt_notify(
        conn,
        &tracker_service.attrs[TRACKER_NOTIFY_ATTR_INDEX],
        notification_data,
        sizeof(notification_data)
    );

    if (ret == 0) {
        printk(
            "[BLE] Notification sent: %02X %02X\n",
            event,
            status
        );
    } else {
        printk(
            "[BLE] Notification not sent: %d\n",
            ret
        );
        printk(
            "[BLE] Check that the iPhone subscribed to notifications.\n"
        );
    }
}

/* ==========================================
 * BLE ADVERTISING DATA
 * ========================================== */

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(
        BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
    ),

    BT_DATA_BYTES(
        BT_DATA_UUID128_ALL,
        0x10, 0x02, 0xC4, 0x01,
        0xA7, 0x3C, 0x91, 0x2D,
        0x6B, 0x4F, 0x48, 0x80,
        0x9A, 0x21, 0x5E, 0x7D
    )
};

static const struct bt_data scan_response[] = {
    BT_DATA(
        BT_DATA_NAME_COMPLETE,
        "DWM3001-TRACKER",
        sizeof("DWM3001-TRACKER") - 1
    )
};

/* ==========================================
 * MAIN APPLICATION
 * ========================================== */

int main(void)
{
    printk("\n====================================\n");
    printk("       DWM3001 TRACKER FIRMWARE\n");
    printk("====================================\n");

    /* Initialize DW3110 */

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

    tracker_status = 1;

    /* Initialize Bluetooth */

    printk("\n[2] Initializing Bluetooth...\n");

    ret = bt_enable(NULL);

    if (ret != 0) {
        printk("ERROR: Bluetooth initialization failed: %d\n", ret);

        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("SUCCESS: Bluetooth initialized.\n");

    /* Start BLE advertising */

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
    printk("Notification UUID: 7d5e219e-8048-4f6b-2d91-3ca701c40210\n");

    printk("\n====================================\n");
    printk("       TRACKER READY\n");
    printk("====================================\n");

    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
