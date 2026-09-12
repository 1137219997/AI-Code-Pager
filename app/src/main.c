/**
 * @file main.c
 * @brief NEON CODE PAGER 01 - 蓝牙/USB 极客 AI 审批寻呼机固件
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* 配色定义 (RGB888 宏) */
#define COLOR_BG          lv_color_hex(0x060C08) // 极深墨绿
#define COLOR_NEON_GREEN  lv_color_hex(0x29F27D) // 荧光青绿
#define COLOR_CYAN        lv_color_hex(0x00F0FF) // 亮青色
#define COLOR_AMBER       lv_color_hex(0xFFB703) // 橙黄色卡片
#define COLOR_CARD_BG     lv_color_hex(0x13241B) // 深绿卡片底色
#define COLOR_TEXT_DIM    lv_color_hex(0x80998A) // 暗绿文字

/* 全局 UI 控件指针 */
static lv_obj_t *label_project;
static lv_obj_t *label_status;
static lv_obj_t *label_feedback;
static lv_obj_t *label_reminder;
static lv_obj_t *label_hint;
static lv_obj_t *banner_card;

/* 20x20 招手小怪兽 Avatar 像素点阵 (保存在 Flash 只读段) */
static const uint8_t monster_map[20][20] = {
    {0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0},
    {0,0,0,0,1,2,2,1,0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,1,2,2,2,2,1,1,1,1,2,2,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,0},
    {0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0},
    {0,1,2,2,3,3,2,2,2,2,2,2,3,3,2,2,2,2,1,1},
    {1,2,2,3,4,4,3,2,2,2,2,3,4,4,3,2,2,2,2,1},
    {1,2,2,3,4,4,3,2,2,2,2,3,4,4,3,2,2,2,2,1},
    {1,2,2,2,3,3,2,2,2,2,2,2,3,3,2,2,2,2,2,1},
    {0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0},
    {0,0,1,2,2,2,3,3,3,3,3,3,2,2,2,2,2,1,0,0},
    {0,0,0,1,2,3,4,4,4,4,4,4,3,2,2,2,1,0,0,0},
    {0,0,0,1,2,3,4,4,4,4,4,4,3,2,2,2,1,0,0,0},
    {0,0,0,0,1,2,3,3,3,3,3,3,2,2,2,1,0,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,0},
    {0,0,1,2,2,1,0,0,0,0,0,0,1,2,2,1,0,0,0,0},
    {0,0,1,2,1,0,0,0,0,0,0,0,0,1,2,1,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

/* 动态绘制小怪兽 Canvas */
static void create_avatar(lv_obj_t *parent) {
    lv_obj_t *avatar_box = lv_obj_create(parent);
    lv_obj_set_size(avatar_box, 86, 92);
    lv_obj_set_pos(avatar_box, 10, 26);
    lv_obj_set_style_bg_color(avatar_box, lv_color_hex(0x0A1810), 0);
    lv_obj_set_style_border_color(avatar_box, COLOR_NEON_GREEN, 0);
    lv_obj_set_style_border_width(avatar_box, 1, 0);
    lv_obj_set_style_radius(avatar_box, 6, 0);
    lv_obj_clear_flag(avatar_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 内部小怪兽像素画布 (使用静态分配内存) */
    static lv_color_t cbuf[80 * 70];
    lv_obj_t *canvas = lv_canvas_create(avatar_box);
    lv_canvas_set_buffer(canvas, cbuf, 80, 70, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, 0);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x0A1810), LV_OPA_COVER);

    int scale = 3;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            uint8_t p = monster_map[y][x];
            lv_color_t color;
            if (p == 1) color = COLOR_NEON_GREEN;
            else if (p == 2) color = lv_color_hex(0x55FFAA);
            else if (p == 3) color = lv_color_black();
            else if (p == 4) color = lv_color_hex(0xFF3366);
            else continue;

            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    lv_canvas_set_px_color(canvas, 10 + x * scale + dx, 5 + y * scale + dy, color);
                }
            }
        }
    }

    /* 状态标签 */
    lv_obj_t *tag = lv_label_create(avatar_box);
    lv_label_set_text(tag, "ACTIVE");
    lv_obj_set_style_text_color(tag, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
    lv_obj_align(tag, LV_ALIGN_BOTTOM_MID, 0, 2);
}

/* 构建主屏幕界面 (严格对照图片 320x170 布局) */
static void build_pager_ui(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. 顶部标题栏 */
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, 320, 22);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x0E1F15), 0);
    lv_obj_set_style_border_color(top_bar, COLOR_NEON_GREEN, 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);

    label_project = lv_label_create(top_bar);
    lv_label_set_text(label_project, "AI LINK: CLAUDE_CODE: ACTIVE");
    lv_obj_set_style_text_color(label_project, COLOR_NEON_GREEN, 0);
    lv_obj_set_style_text_font(label_project, &lv_font_montserrat_14, 0);
    lv_obj_align(label_project, LV_ALIGN_LEFT_MID, 6, 0);

    /* 2. 左侧像素伴侣 */
    create_avatar(scr);

    /* 3. 右侧状态信息区 */
    label_status = lv_label_create(scr);
    lv_label_set_text(label_status, "STATUS: LISTENING");
    lv_obj_set_style_text_color(label_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label_status, 106, 26);

    label_feedback = lv_label_create(scr);
    lv_label_set_text(label_feedback, "NEW AI FEEDBACK:\nACTION REQUIRED");
    lv_obj_set_style_text_color(label_feedback, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(label_feedback, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label_feedback, 106, 44);

    /* 4. 黄色高亮审批卡片 */
    banner_card = lv_obj_create(scr);
    lv_obj_set_size(banner_card, 208, 42);
    lv_obj_set_pos(banner_card, 104, 76);
    lv_obj_set_style_bg_color(banner_card, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_color(banner_card, COLOR_AMBER, 0);
    lv_obj_set_style_border_width(banner_card, 1, 0);
    lv_obj_set_style_radius(banner_card, 4, 0);
    lv_obj_clear_flag(banner_card, LV_OBJ_FLAG_SCROLLABLE);

    label_reminder = lv_label_create(banner_card);
    lv_label_set_text(label_reminder, "! REVIEW PULL REQUEST #401");
    lv_obj_set_style_text_color(label_reminder, COLOR_AMBER, 0);
    lv_obj_set_style_text_font(label_reminder, &lv_font_montserrat_14, 0);
    lv_obj_align(label_reminder, LV_ALIGN_TOP_LEFT, -2, -4);

    label_hint = lv_label_create(banner_card);
    lv_label_set_text(label_hint, "(CLICK RKJXT)");
    lv_obj_set_style_text_color(label_hint, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(label_hint, LV_ALIGN_BOTTOM_LEFT, -2, 4);

    /* 5. 底部 HUD 按键指示条 */
    lv_obj_t *bot_bar = lv_obj_create(scr);
    lv_obj_set_size(bot_bar, 320, 24);
    lv_obj_set_pos(bot_bar, 0, 146);
    lv_obj_set_style_bg_color(bot_bar, lv_color_hex(0x051009), 0);
    lv_obj_set_style_border_color(bot_bar, lv_color_hex(0x183822), 0);
    lv_obj_set_style_border_side(bot_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bot_bar, 0, 0);

    lv_obj_t *hud_y = lv_label_create(bot_bar);
    lv_label_set_text(hud_y, "[Y] Yes");
    lv_obj_set_style_text_color(hud_y, COLOR_NEON_GREEN, 0);
    lv_obj_align(hud_y, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *hud_n = lv_label_create(bot_bar);
    lv_label_set_text(hud_n, "[N] No");
    lv_obj_set_style_text_color(hud_n, lv_color_hex(0xFF5555), 0);
    lv_obj_align(hud_n, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hud_opt = lv_label_create(bot_bar);
    lv_label_set_text(hud_opt, "[DETAIL] View");
    lv_obj_set_style_text_color(hud_opt, COLOR_CYAN, 0);
    lv_obj_align(hud_opt, LV_ALIGN_RIGHT_MID, -16, 0);
}

/* 主线程入口 */
int main(void) {
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) {
        LOG_ERR("显示设备未就绪");
        return 0;
    }

    /* 启动 USB CDC ACM 虚拟串口 */
    if (usb_enable(NULL)) {
        LOG_ERR("USB 初始化失败");
    }

    /* 开启屏幕显示并渲染 UI */
    display_blanking_off(display_dev);
    build_pager_ui();

    /* 主循环: 周期性让 LVGL 处理渲染刷新 */
    while (1) {
        lv_task_handler();
        k_msleep(5);
    }
    return 0;
}
