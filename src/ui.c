#include "app.h"
#include "assets/pet_animations.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(pager_ui, LOG_LEVEL_INF);

#define COLOR_BG       lv_color_hex(0x101713)
#define COLOR_PANEL    lv_color_hex(0x15211c)
#define COLOR_LINE     lv_color_hex(0x426448)
#define COLOR_GREEN    lv_color_hex(0x57ae5b)
#define COLOR_CREAM    lv_color_hex(0xe8dda8)
#define COLOR_DIM      lv_color_hex(0x8ba487)
#define COLOR_PINK     lv_color_hex(0xe87e75)

enum ui_event_type {
    UI_EVENT_INPUT,
    UI_EVENT_TEXT,
    UI_EVENT_PET,
    UI_EVENT_CONNECTION,
};

struct ui_event {
    uint8_t type;
    uint8_t arg;
    uint8_t flags;
    uint8_t len;
    char data[APP_TEXT_CHUNK_MAX];
};

K_MSGQ_DEFINE(ui_queue, sizeof(struct ui_event), 8, 4);

static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static lv_obj_t *pet_anim;
static lv_obj_t *status_label;
static lv_obj_t *message_label;
static lv_obj_t *menu_label;
static lv_obj_t *hint_label;
static char message_buffer[512] = "Waiting for your coding agent...\n\n"
                                  "Turn the RKJXT control to browse actions.";
static size_t message_length;
static uint8_t menu_index;

static const char *const menu_items[] = { "APPROVE", "DETAILS", "REJECT" };
static const char *const input_names[APP_INPUT_COUNT] = {
    "K1", "K2", "K3", "K4", "K5", "K6", "UP", "DOWN", "LEFT",
    "RIGHT", "PUSH", "DIAL-", "DIAL+",
};

static const lv_img_dsc_t *scratch_frames[] = {
    &pet_scratch_0, &pet_scratch_1, &pet_scratch_2, &pet_scratch_3,
};
static const lv_img_dsc_t *cheer_frames[] = {
    &pet_cheer_0, &pet_cheer_1, &pet_cheer_2, &pet_cheer_3,
};
static const lv_img_dsc_t *sleep_frames[] = {
    &pet_sleep_0, &pet_sleep_1, &pet_sleep_2, &pet_sleep_3,
};

static void set_pet(enum app_pet_state state)
{
    const void **frames = (const void **)scratch_frames;
    uint32_t duration = 680;

    switch (state) {
    case APP_PET_POINT_DOWN:
        /* Keep protocol state 1 compatible after removing this asset. */
        break;
    case APP_PET_CHEER:
        frames = (const void **)cheer_frames;
        duration = 520;
        break;
    case APP_PET_SLEEP:
        frames = (const void **)sleep_frames;
        duration = 1100;
        break;
    case APP_PET_SCRATCH:
    default:
        break;
    }

    lv_animimg_set_src(pet_anim, frames, 4);
    lv_animimg_set_duration(pet_anim, duration);
    lv_animimg_set_repeat_count(pet_anim, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(pet_anim);
}

static void show_lcd_self_test(lv_obj_t *screen)
{
    static const uint32_t colors[] = {
        0xff0000, 0x00ff00, 0x0000ff, 0xffffff,
    };

    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < ARRAY_SIZE(colors); ++i) {
        lv_obj_t *bar = lv_obj_create(screen);
        lv_obj_set_pos(bar, i * 80, 0);
        lv_obj_set_size(bar, 80, 170);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    }

    display_blanking_off(display_dev);
    lv_refr_now(NULL);
    k_sleep(K_MSEC(1500));
    lv_obj_clean(screen);
}

static void update_menu(void)
{
    char text[64];
    snprintf(text, sizeof(text), "<  %s  >", menu_items[menu_index]);
    lv_label_set_text(menu_label, text);
}

static void handle_input(uint8_t input_id, bool pressed)
{
    if (!pressed || input_id >= APP_INPUT_COUNT) {
        return;
    }

    char hint[32];
    snprintf(hint, sizeof(hint), "%s INPUT", input_names[input_id]);
    lv_label_set_text(hint_label, hint);

    if (input_id == APP_NAV_UP || input_id == APP_NAV_LEFT ||
        input_id == APP_ENCODER_CCW) {
        menu_index = (menu_index + ARRAY_SIZE(menu_items) - 1) %
                     ARRAY_SIZE(menu_items);
        update_menu();
    } else if (input_id == APP_NAV_DOWN || input_id == APP_NAV_RIGHT ||
               input_id == APP_ENCODER_CW) {
        menu_index = (menu_index + 1) % ARRAY_SIZE(menu_items);
        update_menu();
    } else if (input_id == APP_NAV_PUSH) {
        if (menu_index == 0) {
            set_pet(APP_PET_CHEER);
        } else if (menu_index == 1) {
            set_pet(APP_PET_POINT_DOWN);
        } else {
            set_pet(APP_PET_SCRATCH);
        }
    }
}

static void handle_text(const struct ui_event *event)
{
    if (event->flags & 0x01) {
        message_length = 0;
        message_buffer[0] = '\0';
    }

    size_t room = sizeof(message_buffer) - 1 - message_length;
    size_t copy = MIN((size_t)event->len, room);
    memcpy(&message_buffer[message_length], event->data, copy);
    message_length += copy;
    message_buffer[message_length] = '\0';

    if (event->flags & 0x02) {
        lv_label_set_text(message_label, message_buffer);
        lv_obj_scroll_to_view(message_label, LV_ANIM_ON);
    }
}

int app_ui_init(void)
{
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display is not ready");
        return -ENODEV;
    }
    lv_obj_t *screen = lv_scr_act();
    show_lcd_self_test(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_12, 0);

    lv_obj_t *left = lv_obj_create(screen);
    lv_obj_set_pos(left, 4, 4);
    lv_obj_set_size(left, 112, 162);
    lv_obj_set_style_bg_color(left, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left, COLOR_LINE, 0);
    lv_obj_set_style_border_width(left, 2, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pet_title = lv_label_create(left);
    lv_label_set_text(pet_title, "// MOSS-01 //");
    lv_obj_set_style_text_color(pet_title, COLOR_GREEN, 0);
    lv_obj_align(pet_title, LV_ALIGN_TOP_MID, 0, 7);

    pet_anim = lv_animimg_create(left);
    lv_obj_set_size(pet_anim, 80, 80);
    lv_obj_align(pet_anim, LV_ALIGN_CENTER, 0, -3);
    set_pet(APP_PET_SCRATCH);

    hint_label = lv_label_create(left);
    lv_label_set_text(hint_label, "READY");
    lv_obj_set_style_text_color(hint_label, COLOR_DIM, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    lv_obj_t *right = lv_obj_create(screen);
    lv_obj_set_pos(right, 120, 4);
    lv_obj_set_size(right, 196, 162);
    lv_obj_set_style_bg_color(right, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right, COLOR_LINE, 0);
    lv_obj_set_style_border_width(right, 2, 0);
    lv_obj_set_style_radius(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(right);
    lv_label_set_text(title, "AI-CODE-PAGER");
    lv_obj_set_style_text_color(title, COLOR_CREAM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title, 8, 7);

    status_label = lv_label_create(right);
    lv_label_set_text(status_label, "BLE ...");
    lv_obj_set_style_text_color(status_label, COLOR_PINK, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, -7, 8);

    lv_obj_t *rule = lv_obj_create(right);
    lv_obj_set_pos(rule, 7, 27);
    lv_obj_set_size(rule, 180, 2);
    lv_obj_set_style_bg_color(rule, COLOR_LINE, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);

    lv_obj_t *message_box = lv_obj_create(right);
    lv_obj_set_pos(message_box, 6, 32);
    lv_obj_set_size(message_box, 184, 91);
    lv_obj_set_style_bg_color(message_box, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(message_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(message_box, 0, 0);
    lv_obj_set_style_radius(message_box, 0, 0);
    lv_obj_set_style_pad_all(message_box, 6, 0);
    lv_obj_set_scroll_dir(message_box, LV_DIR_VER);

    message_label = lv_label_create(message_box);
    lv_obj_set_width(message_label, 172);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(message_label, message_buffer);
    lv_obj_set_style_text_color(message_label, COLOR_CREAM, 0);
    message_length = strlen(message_buffer);

    menu_label = lv_label_create(right);
    lv_obj_set_style_text_color(menu_label, COLOR_GREEN, 0);
    lv_obj_set_style_text_font(menu_label, &lv_font_montserrat_14, 0);
    lv_obj_align(menu_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    update_menu();

    display_blanking_off(display_dev);
    LOG_INF("320x170 split UI ready");
    return 0;
}

void app_ui_post_input(uint8_t input_id, bool pressed)
{
    struct ui_event event = {
        .type = UI_EVENT_INPUT,
        .arg = input_id,
        .flags = pressed ? 1 : 0,
    };
    (void)k_msgq_put(&ui_queue, &event, K_NO_WAIT);
}

void app_ui_post_text(const uint8_t *text, size_t len, bool start, bool end)
{
    struct ui_event event = {
        .type = UI_EVENT_TEXT,
        .flags = (start ? 0x01 : 0) | (end ? 0x02 : 0),
        .len = MIN(len, (size_t)APP_TEXT_CHUNK_MAX),
    };
    memcpy(event.data, text, event.len);
    (void)k_msgq_put(&ui_queue, &event, K_NO_WAIT);
}

void app_ui_post_pet_state(enum app_pet_state state)
{
    struct ui_event event = { .type = UI_EVENT_PET, .arg = state };
    (void)k_msgq_put(&ui_queue, &event, K_NO_WAIT);
}

void app_ui_post_connection(bool connected)
{
    struct ui_event event = {
        .type = UI_EVENT_CONNECTION,
        .arg = connected ? 1 : 0,
    };
    (void)k_msgq_put(&ui_queue, &event, K_NO_WAIT);
}

void app_ui_process(void)
{
    struct ui_event event;
    while (k_msgq_get(&ui_queue, &event, K_NO_WAIT) == 0) {
        switch (event.type) {
        case UI_EVENT_INPUT:
            handle_input(event.arg, event.flags != 0);
            break;
        case UI_EVENT_TEXT:
            handle_text(&event);
            break;
        case UI_EVENT_PET:
            set_pet((enum app_pet_state)event.arg);
            break;
        case UI_EVENT_CONNECTION:
            lv_label_set_text(status_label, event.arg ? "BLE OK" : "BLE ...");
            lv_obj_set_style_text_color(status_label,
                                        event.arg ? COLOR_GREEN : COLOR_PINK, 0);
            break;
        default:
            break;
        }
    }
}
