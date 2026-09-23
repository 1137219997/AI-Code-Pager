#include "app.h"

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pager_ble, LOG_LEVEL_INF);

#define UUID_HID_SERVICE       BT_UUID_DECLARE_16(0x1812)
#define UUID_HID_INFO          BT_UUID_DECLARE_16(0x2a4a)
#define UUID_HID_REPORT_MAP    BT_UUID_DECLARE_16(0x2a4b)
#define UUID_HID_CONTROL_POINT BT_UUID_DECLARE_16(0x2a4c)
#define UUID_HID_REPORT        BT_UUID_DECLARE_16(0x2a4d)
#define UUID_HID_PROTOCOL_MODE BT_UUID_DECLARE_16(0x2a4e)
#define UUID_REPORT_REFERENCE  BT_UUID_DECLARE_16(0x2908)

#define PAGER_UUID_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define PAGER_UUID_RX_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)
#define PAGER_UUID_TX_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

static struct bt_uuid_128 pager_service_uuid = BT_UUID_INIT_128(PAGER_UUID_SERVICE_VAL);
static struct bt_uuid_128 pager_rx_uuid = BT_UUID_INIT_128(PAGER_UUID_RX_VAL);
static struct bt_uuid_128 pager_tx_uuid = BT_UUID_INIT_128(PAGER_UUID_TX_VAL);

static struct bt_conn *active_conn;
static bool hid_notify_enabled;
static bool vendor_notify_enabled;
static uint8_t protocol_mode = 1;
static uint8_t last_hid_report[8];

static const uint8_t hid_information[] = { 0x11, 0x01, 0x00, 0x02 };
static const uint8_t report_reference[] = { 0x01, 0x01 };
static const uint8_t keyboard_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xa1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x05, 0x07,       /*   Usage Page (Keyboard) */
    0x19, 0xe0,       /*   Usage Minimum (Left Control) */
    0x29, 0xe7,       /*   Usage Maximum (Right GUI) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Constant) */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x73,       /*   Logical Maximum (115) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x29, 0x73,       /*   Usage Maximum (Keyboard Application) */
    0x81, 0x00,       /*   Input (Data, Array) */
    0xc0,             /* End Collection */
};

static ssize_t read_const(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                          void *buf, uint16_t len, uint16_t offset,
                          const void *data, uint16_t data_len)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, data, data_len);
}

static ssize_t read_hid_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return read_const(conn, attr, buf, len, offset, hid_information,
                      sizeof(hid_information));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return read_const(conn, attr, buf, len, offset, keyboard_report_map,
                      sizeof(keyboard_report_map));
}

static ssize_t read_protocol_mode(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr, void *buf,
                                  uint16_t len, uint16_t offset)
{
    return read_const(conn, attr, buf, len, offset, &protocol_mode,
                      sizeof(protocol_mode));
}

static ssize_t write_protocol_mode(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags)
{
    if (offset != 0 || len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    protocol_mode = *(const uint8_t *)buf;
    return len;
}

static ssize_t write_control_point(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len,
                                   uint16_t offset, uint8_t flags)
{
    return (offset == 0 && len == 1) ? len
                                     : BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
}

static ssize_t read_hid_report(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset)
{
    return read_const(conn, attr, buf, len, offset, last_hid_report,
                      sizeof(last_hid_report));
}

static ssize_t read_report_reference(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr, void *buf,
                                     uint16_t len, uint16_t offset)
{
    return read_const(conn, attr, buf, len, offset, report_reference,
                      sizeof(report_reference));
}

static void hid_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    hid_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(hid_svc,
    BT_GATT_PRIMARY_SERVICE(UUID_HID_SERVICE),
    BT_GATT_CHARACTERISTIC(UUID_HID_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_hid_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(UUID_HID_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(UUID_HID_PROTOCOL_MODE,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_protocol_mode, write_protocol_mode, NULL),
    BT_GATT_CHARACTERISTIC(UUID_HID_CONTROL_POINT,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_control_point, NULL),
    BT_GATT_CHARACTERISTIC(UUID_HID_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ, read_hid_report, NULL, NULL),
    BT_GATT_CCC(hid_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(UUID_REPORT_REFERENCE, BT_GATT_PERM_READ,
                       read_report_reference, NULL, NULL)
);

static ssize_t write_vendor_rx(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len,
                               uint16_t offset, uint8_t flags)
{
    if (offset != 0 || len == 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    int err = app_protocol_handle(buf, len);
    if (err == -EMSGSIZE) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    return len;
}

static void vendor_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    vendor_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(pager_svc,
    BT_GATT_PRIMARY_SERVICE(&pager_service_uuid),
    BT_GATT_CHARACTERISTIC(&pager_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_vendor_rx, NULL),
    BT_GATT_CHARACTERISTIC(&pager_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(vendor_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    if (conn_err != 0) {
        LOG_WRN("Connection failed (0x%02x)", conn_err);
        return;
    }
    active_conn = bt_conn_ref(conn);
    app_ui_post_connection(true);
    LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (active_conn != NULL) {
        bt_conn_unref(active_conn);
        active_conn = NULL;
    }
    hid_notify_enabled = false;
    vendor_notify_enabled = false;
    memset(last_hid_report, 0, sizeof(last_hid_report));
    app_ui_post_connection(false);
    LOG_INF("BLE disconnected (0x%02x)", reason);
    (void)app_ble_start_advertising();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int app_ble_init(void)
{
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (%d)", err);
    }
    return err;
}

int app_ble_start_advertising(void)
{
    static const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
        BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0x1812)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, PAGER_UUID_SERVICE_VAL),
    };
    static const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
                sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    };

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));
    if (err && err != -EALREADY) {
        LOG_ERR("Advertising failed (%d)", err);
    }
    return err == -EALREADY ? 0 : err;
}

bool app_ble_is_connected(void)
{
    return active_conn != NULL;
}

int app_ble_hid_report(const uint8_t report[8])
{
    memcpy(last_hid_report, report, sizeof(last_hid_report));
    if (active_conn == NULL || !hid_notify_enabled) {
        return -ENOTCONN;
    }
    return bt_gatt_notify(active_conn, &hid_svc.attrs[10], last_hid_report,
                          sizeof(last_hid_report));
}

int app_ble_vendor_notify(const uint8_t *data, size_t len)
{
    if (active_conn == NULL || !vendor_notify_enabled) {
        return -ENOTCONN;
    }
    return bt_gatt_notify(active_conn, &pager_svc.attrs[4], data, len);
}
