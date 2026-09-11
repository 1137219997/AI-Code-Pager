#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    /* 获取设备树中配置的屏幕设备 */
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready, check app.overlay!");
        return 0;
    }

    /* 开启背光/解除休眠 (关键：防止屏幕默认处于黑屏状态) */
    display_blanking_off(display_dev);

    /* 
     * 开启 CONFIG_LVGL=y 后，底层显示接口和 lv_init() 
     * 会由 Zephyr 自动完成初始化。直接获取当前屏幕对象即可。
     */
    lv_obj_t * screen = lv_scr_act();

    /* 设置复古荧光绿背景 #00FF44 */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x00FF44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    /* 创建测试文本标签 */
    lv_obj_t * label = lv_label_create(screen);
    lv_label_set_text(label, "AI PAGER OS\n\nScreen Init OK!");
    lv_obj_set_style_text_color(label, lv_color_hex(0x050B05), LV_PART_MAIN); /* 纯黑色字体 */
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);

    LOG_INF("LVGL initialization complete, entering main loop.");

    /* LVGL 任务渲染主循环 */
    while (1) {
        lv_task_handler();
        k_msleep(5);
    }

    return 0;
}