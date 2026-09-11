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

    /* 
     * 在 Zephyr 中，开启 CONFIG_LVGL=y 后，底层显示接口和 lv_init() 
     * 会在 main 函数运行前由系统自动完成初始化。
     * 直接获取当前活动屏幕对象即可。
     */
    lv_obj_t * screen = lv_scr_act();

    /* 设置纯绿背景，呈现复古液晶质感 */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x00FF44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    /* 创建测试文本标签 */
    lv_obj_t * label = lv_label_create(screen);
    lv_label_set_text(label, "AI PAGER OS\n\nScreen Init OK");
    lv_obj_set_style_text_color(label, lv_color_hex(0x050B05), LV_PART_MAIN); // 纯黑色文字
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);

    /* 启动后台定时器，点亮屏幕 (由于部分模块默认背光可能是关闭的，需调用API) */
    display_blanking_off(display_dev);

    LOG_INF("LVGL initialization complete, entering main loop.");

    /* LVGL 任务渲染主循环 */
    while (1) {
        lv_task_handler();
        k_msleep(10);
    }

    return 0;
}