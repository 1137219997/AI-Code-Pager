#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/usb/usb_device.h>
#include <lvgl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    /* 初始化 USB 虚拟串口，为后续与 Python 脚本通信做准备 */
    if (usb_enable(NULL)) {
        return 0;
    }
    
    /* 等待 USB 枚举完成 (可选) */
    k_msleep(1000); 

    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready!");
        return 0;
    }

    display_blanking_off(display_dev);

    lv_obj_t * screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x00FF44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t * label = lv_label_create(screen);
    lv_label_set_text(label, "AI PAGER OS\n\nSystem Boot OK!");
    lv_obj_set_style_text_color(label, lv_color_hex(0x050B05), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);

    LOG_INF("Entering main loop...");

    while (1) {
        lv_task_handler();
        k_msleep(5);
    }

    return 0;
}