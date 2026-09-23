#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_INPUT_COUNT 13
#define APP_TEXT_CHUNK_MAX 180

enum app_input_id {
    APP_KEY_1 = 0,
    APP_KEY_2,
    APP_KEY_3,
    APP_KEY_4,
    APP_KEY_5,
    APP_KEY_6,
    APP_NAV_UP,
    APP_NAV_DOWN,
    APP_NAV_LEFT,
    APP_NAV_RIGHT,
    APP_NAV_PUSH,
    APP_ENCODER_CCW,
    APP_ENCODER_CW,
};

enum app_pet_state {
    APP_PET_SCRATCH = 0,
    APP_PET_POINT_DOWN,
    APP_PET_CHEER,
    APP_PET_SLEEP,
    APP_PET_STATE_COUNT,
};

struct app_keymap_entry {
    uint8_t modifiers;
    uint8_t usage;
};

int app_protocol_handle(const uint8_t *data, size_t len);
void app_on_input(uint8_t input_id, bool pressed);

int app_storage_init(void);
const struct app_keymap_entry *app_keymap_get(void);
int app_keymap_set(uint8_t input_id, uint8_t modifiers, uint8_t usage);
int app_keymap_save(void);
int app_keymap_reset(void);

int app_ble_init(void);
int app_ble_start_advertising(void);
bool app_ble_is_connected(void);
int app_ble_hid_report(const uint8_t report[8]);
int app_ble_vendor_notify(const uint8_t *data, size_t len);

int app_input_init(void);

int app_ui_init(void);
void app_ui_process(void);
void app_ui_post_input(uint8_t input_id, bool pressed);
void app_ui_post_text(const uint8_t *text, size_t len, bool start, bool end);
void app_ui_post_pet_state(enum app_pet_state state);
void app_ui_post_connection(bool connected);
