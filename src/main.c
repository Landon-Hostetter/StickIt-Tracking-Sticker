
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>

#include "dw3000_hw.h"
#include "deca_probe_interface.h"
#include "deca_device_api.h"

/* BLE UUIDs */

static struct bt_uuid_128 tracker_service_uuid =
    BT_UUID_INIT_128(0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
                     0x6B,0x4F,0x48,0x80,0x9A,0x21,0x5E,0x7D);

static struct bt_uuid_128 tracker_status_uuid =
    BT_UUID_INIT_128(0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
                     0x6B,0x4F,0x48,0x80,0x9B,0x21,0x5E,0x7D);

static struct bt_uuid_128 tracker_uwb_id_uuid =
    BT_UUID_INIT_128(0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
                     0x6B,0x4F,0x48,0x80,0x9C,0x21,0x5E,0x7D);

static struct bt_uuid_128 tracker_command_uuid =
    BT_UUID_INIT_128(0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
                     0x6B,0x4F,0x48,0x80,0x9D,0x21,0x5E,0x7D);

static struct bt_uuid_128 tracker_notify_uuid =
    BT_UUID_INIT_128(0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
                     0x6B,0x4F,0x48,0x80,0x9E,0x21,0x5E,0x7D);

/* Tracker state */

static uint8_t tracker_status = 0;
static bool uwb_ready = false;

static const uint8_t uwb_device_id_bytes[] = {
    0x02, 0x03, 0xCA, 0xDE
};

#define TRACKER_EVENT_PING_ACK  0x81
#define TRACKER_EVENT_STATUS    0x82
#define TRACKER_EVENT_UWB_TX    0x83

#define TRACKER_NOTIFY_ATTR_INDEX 8
#define TRACKER_TXFRS_MASK        0x00000080UL

static void send_tracker_notification(
    struct bt_conn *conn,
    uint8_t event,
    uint8_t status
);

/* UWB radio configuration */

static int initialize_uwb_radio(void)
{
    printk("\n[UWB] Initializing DW3110 radio...\n");

    int ret = dwt_initialise(0);

    if (ret != DWT_SUCCESS) {
        printk("[UWB] Radio initialization failed: %d\n", ret);
        return -1;
    }

    printk("[UWB] Applying radio configuration...\n");

    static dwt_config_t config = {
        .chan           = 5,
        .txPreambLength = DWT_PLEN_128,
        .rxPAC          = DWT_PAC8,
        .txCode         = 9,
        .rxCode         = 9,
        .sfdType        = DWT_SFD_IEEE_4Z,
        .dataRate       = DWT_BR_6M8,
        .phrMode        = DWT_PHRMODE_STD,
        .phrRate        = DWT_PHRRATE_STD,
        .sfdTO          = 0,
        .stsMode        = DWT_STS_MODE_OFF,
        .stsLength      = DWT_STS_LEN_64,
        .pdoaMode       = DWT_PDOA_M0
    };

    ret = dwt_configure(&config);

    if (ret != DWT_SUCCESS) {
        printk("[UWB] Radio configuration failed: %d\n", ret);
        return -2;
    }

    printk("[UWB] Radio configured successfully.\n");
    return 0;
}

/* Transmit one UWB test packet */

static int transmit_uwb_packet(void)
{
    if (!uwb_ready) {
        printk("[UWB] ERROR: Radio is not ready.\n");
        return -1;
    }

    static uint8_t packet[] = {
        'D', 'W', 'M', '3', '0', '0', '1'
    };

    printk("\n[UWB] Preparing test transmission...\n");

    /* Clear any previous TX completion flag. */
    dwt_writesysstatuslo(TRACKER_TXFRS_MASK);

    int ret = dwt_writetxdata(
        sizeof(packet),
        packet,
        0
    );

    if (ret != DWT_SUCCESS) {
        printk("[UWB] Failed to write TX data: %d\n", ret);
        return -2;
    }

    /* Hardware adds the two-byte CRC. */
    dwt_writetxfctrl(
        sizeof(packet) + 2,
        0,
        0
    );

    printk("[UWB] Starting transmission...\n");

    ret = dwt_starttx(DWT_START_TX_IMMEDIATE);

    if (ret != DWT_SUCCESS) {
        printk("[UWB] Transmission could not start: %d\n", ret);
        return -4;
    }

    /* Wait up to approximately 100 ms for TX completion. */
    for (int i = 0; i < 100; i++) {
        uint32_t status = dwt_readsysstatuslo();

        if (status & TRACKER_TXFRS_MASK) {
            dwt_writesysstatuslo(TRACKER_TXFRS_MASK);

            printk("[UWB] SUCCESS: Transmission completed!\n");
            return 0;
        }

        k_msleep(1);
    }

    printk("[UWB] ERROR: Transmission timed out.\n");
    return -5;
}

/* BLE read callbacks */

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

/* BLE command callback */

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
        printk("[BLE] PING received.\n");
        send_tracker_notification(conn, TRACKER_EVENT_PING_ACK,
                                  tracker_status);
        break;

    case 0x02:
        printk("[BLE] STATUS requested.\n");
        send_tracker_notification(conn, TRACKER_EVENT_STATUS,
                                  tracker_status);
        break;

    case 0x03: {
        printk("[BLE] UWB transmission requested.\n");

        int result = transmit_uwb_packet();

        send_tracker_notification(
            conn,
            TRACKER_EVENT_UWB_TX,
            (result == 0) ? 1 : 0
        );

        break;
    }

    default:
        printk("[BLE] Unknown command.\n");
        break;
    }

    return len;
}

/* BLE GATT service */

BT_GATT_SERVICE_DEFINE(
    tracker_service,

    BT_GATT_PRIMARY_SERVICE(&tracker_service_uuid),

    BT_GATT_CHARACTERISTIC(
        &tracker_status_uuid.uuid,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_tracker_status,
        NULL,
        NULL
    ),

    BT_GATT_CHARACTERISTIC(
        &tracker_uwb_id_uuid.uuid,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_uwb_device_id,
        NULL,
        NULL
    ),

    BT_GATT_CHARACTERISTIC(
        &tracker_command_uuid.uuid,
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE,
        NULL,
        write_tracker_command,
        NULL
    ),

    BT_GATT_CHARACTERISTIC(
        &tracker_notify_uuid.uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL,
        NULL,
        NULL
    ),

    BT_GATT_CCC(
        NULL,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE
    )
);

/* BLE notifications */

static void send_tracker_notification(
    struct bt_conn *conn,
    uint8_t event,
    uint8_t status
)
{
    uint8_t data[2] = { event, status };

    int ret = bt_gatt_notify(
        conn,
        &tracker_service.attrs[TRACKER_NOTIFY_ATTR_INDEX],
        data,
        sizeof(data)
    );

    if (ret == 0) {
        printk("[BLE] Notification: %02X %02X\n", event, status);
    } else {
        printk("[BLE] Notification failed: %d\n", ret);
    }
}

/* BLE advertising */

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(
        BT_DATA_FLAGS,
        (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)
    ),

    BT_DATA_BYTES(
        BT_DATA_UUID128_ALL,
        0x10,0x02,0xC4,0x01,0xA7,0x3C,0x91,0x2D,
        0x6B,0x4F,0x48,0x80,0x9A,0x21,0x5E,0x7D
    )
};

static const struct bt_data scan_response[] = {
    BT_DATA(
        BT_DATA_NAME_COMPLETE,
        "DWM3001-TRACKER",
        sizeof("DWM3001-TRACKER") - 1
    )
};

/* Main */

int main(void)
{
    printk("\n====================================\n");
    printk("       DWM3001 TRACKER FIRMWARE\n");
    printk("====================================\n");

    printk("\n[1] Initializing DW3110 hardware...\n");

    int ret = dw3000_hw_init();

    if (ret != 0) {
        printk("[ERROR] UWB hardware initialization: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    dw3000_hw_reset();
    k_msleep(10);

    int32_t probe_result =
        dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    if (probe_result != DWT_SUCCESS) {
        printk("[ERROR] DW3110 identification failed.\n");
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    uint32_t device_id = dwt_readdevid();

    printk("[SUCCESS] DW3110 detected: 0x%08X\n",
           (unsigned int)device_id);

    ret = initialize_uwb_radio();

    if (ret == 0) {
        uwb_ready = true;
        tracker_status = 1;
        printk("[SUCCESS] UWB radio is ready.\n");
    } else {
        printk("[WARNING] UWB radio unavailable: %d\n", ret);
        tracker_status = 0;
    }

    printk("\n[2] Initializing Bluetooth...\n");

    ret = bt_enable(NULL);

    if (ret != 0) {
        printk("[ERROR] Bluetooth initialization failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("[SUCCESS] Bluetooth initialized.\n");

    printk("\n[3] Starting BLE advertising...\n");

    ret = bt_le_adv_start(
        BT_LE_ADV_CONN_FAST_1,
        advertising_data,
        ARRAY_SIZE(advertising_data),
        scan_response,
        ARRAY_SIZE(scan_response)
    );

    if (ret != 0) {
        printk("[ERROR] BLE advertising failed: %d\n", ret);
        while (1) {
            k_sleep(K_SECONDS(2));
        }
    }

    printk("[SUCCESS] Tracker advertising!\n");
    printk("Device name: DWM3001-TRACKER\n");
    printk("BLE command 0x01: PING\n");
    printk("BLE command 0x02: STATUS\n");
    printk("BLE command 0x03: UWB TRANSMIT\n");

    printk("\n====================================\n");
    printk("          TRACKER READY\n");
    printk("====================================\n");

    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
