#include "app.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pager_input, LOG_LEVEL_INF);

#define BUTTON_COUNT 11

static const struct gpio_dt_spec buttons[BUTTON_COUNT] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(input0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(input1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(input2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(input3), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(input4), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(input5), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(nav_up), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(nav_down), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(nav_left), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(nav_right), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(nav_push), gpios),
};

static const struct gpio_dt_spec encoder_a =
    GPIO_DT_SPEC_GET(DT_ALIAS(encoder_a), gpios);
static const struct gpio_dt_spec encoder_b =
    GPIO_DT_SPEC_GET(DT_ALIAS(encoder_b), gpios);

static struct gpio_callback button_callbacks[BUTTON_COUNT];
static struct gpio_callback encoder_callbacks[2];
static bool button_state[BUTTON_COUNT];
static struct k_work_delayable button_scan_work;
static struct k_work encoder_work;
static uint8_t encoder_previous;
static int8_t encoder_accumulator;
static atomic_t encoder_pending;

static void scan_buttons(struct k_work *work)
{
    for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        int value = gpio_pin_get_dt(&buttons[i]);
        if (value < 0 || (bool)value == button_state[i]) {
            continue;
        }
        button_state[i] = (bool)value;
        app_on_input(i, button_state[i]);
    }
}

static void button_irq(const struct device *port, struct gpio_callback *cb,
                       gpio_port_pins_t pins)
{
    (void)k_work_reschedule(&button_scan_work, K_MSEC(10));
}

static void report_encoder(struct k_work *work)
{
    atomic_val_t steps = atomic_set(&encoder_pending, 0);
    uint8_t input_id = steps > 0 ? APP_ENCODER_CW : APP_ENCODER_CCW;

    for (atomic_val_t i = 0; i < ABS(steps); ++i) {
        app_on_input(input_id, true);
        app_on_input(input_id, false);
    }
}

static void encoder_irq(const struct device *port, struct gpio_callback *cb,
                        gpio_port_pins_t pins)
{
    static const int8_t transition[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0,
    };
    int a = gpio_pin_get_dt(&encoder_a);
    int b = gpio_pin_get_dt(&encoder_b);
    if (a < 0 || b < 0) {
        return;
    }

    uint8_t current = ((uint8_t)a << 1) | (uint8_t)b;
    encoder_accumulator += transition[(encoder_previous << 2) | current];
    encoder_previous = current;

    if (encoder_accumulator >= 4) {
        atomic_inc(&encoder_pending);
        encoder_accumulator = 0;
        (void)k_work_submit(&encoder_work);
    } else if (encoder_accumulator <= -4) {
        atomic_dec(&encoder_pending);
        encoder_accumulator = 0;
        (void)k_work_submit(&encoder_work);
    }
}

static int configure_input(const struct gpio_dt_spec *spec,
                           struct gpio_callback *callback,
                           gpio_callback_handler_t handler)
{
    if (!gpio_is_ready_dt(spec)) {
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (err) {
        return err;
    }
    gpio_init_callback(callback, handler, BIT(spec->pin));
    err = gpio_add_callback(spec->port, callback);
    if (err) {
        return err;
    }
    return gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
}

int app_input_init(void)
{
    k_work_init_delayable(&button_scan_work, scan_buttons);
    k_work_init(&encoder_work, report_encoder);

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        int err = configure_input(&buttons[i], &button_callbacks[i], button_irq);
        if (err) {
            LOG_ERR("Button %u init failed (%d)", (unsigned)i, err);
            return err;
        }
        button_state[i] = gpio_pin_get_dt(&buttons[i]) > 0;
    }

    int err = configure_input(&encoder_a, &encoder_callbacks[0], encoder_irq);
    if (err) {
        return err;
    }
    err = configure_input(&encoder_b, &encoder_callbacks[1], encoder_irq);
    if (err) {
        return err;
    }

    encoder_previous = ((uint8_t)(gpio_pin_get_dt(&encoder_a) > 0) << 1) |
                       (uint8_t)(gpio_pin_get_dt(&encoder_b) > 0);
    LOG_INF("13 logical inputs ready");
    return 0;
}
