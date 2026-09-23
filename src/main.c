#include "app.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(ai_code_pager, LOG_LEVEL_INF);

int main(void)
{
    int err = app_ui_init();
    if (err) {
        LOG_ERR("UI initialization failed (%d)", err);
        return err;
    }

    err = app_ble_init();
    if (err) {
        return err;
    }

    err = app_storage_init();
    if (err) {
        LOG_WRN("Using default keymap because settings failed (%d)", err);
    }

    err = app_input_init();
    if (err) {
        return err;
    }

    err = app_ble_start_advertising();
    if (err) {
        return err;
    }

    LOG_INF("AI-Code-Pager ready");
    while (true) {
        app_ui_process();
        uint32_t wait_ms = lv_timer_handler();
        k_sleep(K_MSEC(CLAMP(wait_ms, 5U, 20U)));
    }
    return 0;
}
