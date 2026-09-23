#include "app.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(pager_storage, LOG_LEVEL_INF);

#define KEYMAP_VERSION 1U

struct stored_keymap {
    uint32_t version;
    struct app_keymap_entry entries[APP_INPUT_COUNT];
};

static const struct app_keymap_entry default_keymap[APP_INPUT_COUNT] = {
    [APP_KEY_1] = { .usage = 0x68 }, /* F13 */
    [APP_KEY_2] = { .usage = 0x69 }, /* F14 */
    [APP_KEY_3] = { .usage = 0x6a }, /* F15 */
    [APP_KEY_4] = { .usage = 0x6b }, /* F16 */
    [APP_KEY_5] = { .usage = 0x6c }, /* F17 */
    [APP_KEY_6] = { .usage = 0x6d }, /* F18 */
    [APP_NAV_UP] = { .usage = 0x52 },
    [APP_NAV_DOWN] = { .usage = 0x51 },
    [APP_NAV_LEFT] = { .usage = 0x50 },
    [APP_NAV_RIGHT] = { .usage = 0x4f },
    [APP_NAV_PUSH] = { .usage = 0x28 }, /* Enter */
    [APP_ENCODER_CCW] = { .usage = 0x4b }, /* Page Up */
    [APP_ENCODER_CW] = { .usage = 0x4e }, /* Page Down */
};

static struct stored_keymap keymap;

static void load_defaults(void)
{
    keymap.version = KEYMAP_VERSION;
    memcpy(keymap.entries, default_keymap, sizeof(default_keymap));
}

static int keymap_settings_set(const char *name, size_t len,
                               settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "keymap") != 0 || len != sizeof(keymap)) {
        return -ENOENT;
    }

    struct stored_keymap incoming;
    ssize_t read = read_cb(cb_arg, &incoming, sizeof(incoming));
    if (read != sizeof(incoming) || incoming.version != KEYMAP_VERSION) {
        LOG_WRN("Ignoring incompatible keymap");
        return 0;
    }

    keymap = incoming;
    LOG_INF("Loaded keymap v%u", keymap.version);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pager, "pager", NULL, keymap_settings_set,
                               NULL, NULL);

int app_storage_init(void)
{
    load_defaults();
    int err = settings_subsys_init();
    if (err) {
        LOG_ERR("settings_subsys_init failed (%d)", err);
        return err;
    }
    err = settings_load();
    if (err) {
        LOG_ERR("settings_load failed (%d)", err);
    }
    return err;
}

const struct app_keymap_entry *app_keymap_get(void)
{
    return keymap.entries;
}

int app_keymap_set(uint8_t input_id, uint8_t modifiers, uint8_t usage)
{
    if (input_id >= APP_INPUT_COUNT) {
        return -EINVAL;
    }
    keymap.entries[input_id].modifiers = modifiers;
    keymap.entries[input_id].usage = usage;
    return 0;
}

int app_keymap_save(void)
{
    return settings_save_one("pager/keymap", &keymap, sizeof(keymap));
}

int app_keymap_reset(void)
{
    load_defaults();
    return app_keymap_save();
}
