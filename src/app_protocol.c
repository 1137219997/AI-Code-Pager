#include "app.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pager_protocol, LOG_LEVEL_INF);

enum pager_opcode {
    PAGER_OP_SET_KEY = 0x01,
    PAGER_OP_SAVE_KEYMAP = 0x02,
    PAGER_OP_RESET_KEYMAP = 0x03,
    PAGER_OP_GET_KEYMAP = 0x04,
    PAGER_OP_TEXT = 0x10,
    PAGER_OP_PET_STATE = 0x11,
    PAGER_OP_PING = 0x20,
    PAGER_EVT_INPUT = 0x80,
    PAGER_EVT_ACK = 0x81,
    PAGER_EVT_KEYMAP = 0x82,
};

static bool pressed[APP_INPUT_COUNT];

static void send_ack(uint8_t opcode, int status)
{
    const uint8_t packet[] = {
        PAGER_EVT_ACK,
        opcode,
        (uint8_t)(status == 0 ? 0 : -status),
    };
    (void)app_ble_vendor_notify(packet, sizeof(packet));
}

static void send_keymap(void)
{
    uint8_t packet[2 + APP_INPUT_COUNT * 2];
    const struct app_keymap_entry *map = app_keymap_get();

    packet[0] = PAGER_EVT_KEYMAP;
    packet[1] = APP_INPUT_COUNT;
    for (size_t i = 0; i < APP_INPUT_COUNT; ++i) {
        packet[2 + i * 2] = map[i].modifiers;
        packet[3 + i * 2] = map[i].usage;
    }
    (void)app_ble_vendor_notify(packet, sizeof(packet));
}

static void rebuild_hid_report(void)
{
    uint8_t report[8] = { 0 };
    const struct app_keymap_entry *map = app_keymap_get();
    size_t key_slot = 2;

    for (size_t i = 0; i < APP_INPUT_COUNT; ++i) {
        if (!pressed[i] || map[i].usage == 0) {
            continue;
        }
        report[0] |= map[i].modifiers;
        if (key_slot < sizeof(report)) {
            report[key_slot++] = map[i].usage;
        }
    }
    (void)app_ble_hid_report(report);
}

void app_on_input(uint8_t input_id, bool is_pressed)
{
    if (input_id >= APP_INPUT_COUNT) {
        return;
    }

    pressed[input_id] = is_pressed;
    rebuild_hid_report();
    app_ui_post_input(input_id, is_pressed);

    const uint8_t event[] = { PAGER_EVT_INPUT, input_id, is_pressed ? 1 : 0 };
    (void)app_ble_vendor_notify(event, sizeof(event));
}

int app_protocol_handle(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return -EINVAL;
    }

    int err = 0;
    switch (data[0]) {
    case PAGER_OP_SET_KEY:
        if (len != 4) {
            err = -EMSGSIZE;
            break;
        }
        err = app_keymap_set(data[1], data[2], data[3]);
        break;
    case PAGER_OP_SAVE_KEYMAP:
        err = app_keymap_save();
        break;
    case PAGER_OP_RESET_KEYMAP:
        err = app_keymap_reset();
        if (!err) {
            send_keymap();
        }
        break;
    case PAGER_OP_GET_KEYMAP:
        send_keymap();
        break;
    case PAGER_OP_TEXT:
        if (len < 3) {
            err = -EMSGSIZE;
            break;
        }
        app_ui_post_text(&data[2], len - 2, (data[1] & 0x01) != 0,
                         (data[1] & 0x02) != 0);
        break;
    case PAGER_OP_PET_STATE:
        if (len != 2 || data[1] >= APP_PET_STATE_COUNT) {
            err = -EINVAL;
            break;
        }
        app_ui_post_pet_state((enum app_pet_state)data[1]);
        break;
    case PAGER_OP_PING:
        break;
    default:
        err = -ENOTSUP;
        break;
    }

    send_ack(data[0], err);
    return err;
}
