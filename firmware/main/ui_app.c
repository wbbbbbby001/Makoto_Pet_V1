#include "ui_app.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "app.h"
#include "desktop_ui.h"

LV_FONT_DECLARE(font_cn_16);
LV_IMG_DECLARE(ai_bg);

static lv_obj_t *lbl_state;
static lv_obj_t *lbl_wifi;
static lv_obj_t *lbl_voice;
static lv_obj_t *chat_box;
static lv_obj_t *lbl_hint;
static lv_obj_t *bar_mic;
static lv_obj_t *btn_talk;
static lv_obj_t *lbl_btn;

#define COL_BG        lv_color_hex(0x0b1020)
#define COL_PANEL     lv_color_hex(0x14263a)
#define COL_TEXT      lv_color_hex(0xf2f7ff)
#define COL_DIM       lv_color_hex(0x8fa6bd)
#define COL_CYAN      lv_color_hex(0x39d0ff)
#define COL_GREEN     lv_color_hex(0x63e6a8)
#define COL_YELLOW    lv_color_hex(0xffd166)
#define COL_RED       lv_color_hex(0xff6b6b)

static const lv_font_t *FONT_CN = &font_cn_16;

/* 按 UTF-8 边界截断，避免把汉字切坏 */
static void utf8_copy(char *dst, size_t dst_size, const char *src, size_t max_bytes)
{
    size_t n = 0;
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[n] && n < max_bytes && n + 1 < dst_size) {
        unsigned char c = (unsigned char)src[n];
        size_t need = 1;
        if ((c & 0x80) == 0) need = 1;
        else if ((c & 0xE0) == 0xC0) need = 2;
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) need = 4;
        if (n + need > max_bytes || n + need + 1 > dst_size) break;
        n += need;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void set_label_text(lv_obj_t *lbl, const char *utf8)
{
    char buf[200];
    utf8_copy(buf, sizeof(buf), utf8 ? utf8 : "", 180);
    lv_label_set_text(lbl, buf);
}

/* 往可滚动的聊天区追加一条消息：user=true 右对齐（你），false 左对齐（真琴） */
static void chat_add(const char *text, bool user)
{
    if (!text || !text[0] || !chat_box) return;

    char buf[2048];
    utf8_copy(buf, sizeof(buf), text, 2000);
    if (!buf[0]) return;

    lv_obj_t *l = lv_label_create(chat_box);
    lv_obj_set_style_text_font(l, FONT_CN, 0);
    lv_obj_set_style_text_color(l, user ? COL_CYAN : COL_TEXT, 0);
    lv_obj_set_width(l, 292);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, user ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(l, buf);

    /* 只保留最近 24 条，避免 RAM 越用越多 */
    while (lv_obj_get_child_cnt(chat_box) > 24) {
        lv_obj_del(lv_obj_get_child(chat_box, 0));
    }

    lv_obj_update_layout(chat_box);
    lv_obj_scroll_to_view(l, LV_ANIM_OFF);
}

static void talk_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        app_ptt(true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        app_ptt(false);
    }
}

static void desk_event_cb(lv_event_t *e)
{
    (void)e;
    desktop_ui_exit_ai();
}

void ui_init(void)
{
    if (!lvgl_port_lock(0)) return;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 聊天页背景：03.png（必须先创建，后面的控件才会盖在它上面） */
    lv_obj_t *bg = lv_img_create(scr);
    lv_img_set_src(bg, &ai_bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    /* 顶栏 */
    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, 320, 26);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(top, 220, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lbl_state = lv_label_create(top);
    lv_obj_set_style_text_font(lbl_state, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_state, COL_CYAN, 0);
    lv_label_set_text(lbl_state, "开机中");
    lv_obj_align(lbl_state, LV_ALIGN_LEFT_MID, 8, 0);

    lbl_voice = lv_label_create(top);
    lv_obj_set_style_text_font(lbl_voice, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_voice, COL_DIM, 0);
    lv_label_set_text(lbl_voice, "音色: --");
    lv_obj_align(lbl_voice, LV_ALIGN_CENTER, 0, 0);

    lbl_wifi = lv_label_create(top);
    lv_obj_set_style_text_font(lbl_wifi, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_wifi, COL_DIM, 0);
    lv_label_set_text(lbl_wifi, "WiFi…");
    lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, -8, 0);

    /* 标题 */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, FONT_CN, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "橘 真 琴");
    lv_obj_set_pos(title, 12, 32);

    /* 可上下滑动的聊天区：你和真琴的消息按时间顺序排列 */
    chat_box = lv_obj_create(scr);
    lv_obj_set_pos(chat_box, 8, 52);
    lv_obj_set_size(chat_box, 304, 98);
    lv_obj_set_style_bg_color(chat_box, lv_color_hex(0x0e1a28), 0);
    lv_obj_set_style_bg_opa(chat_box, 180, 0);
    lv_obj_set_style_radius(chat_box, 8, 0);
    lv_obj_set_style_border_width(chat_box, 1, 0);
    lv_obj_set_style_border_color(chat_box, lv_color_hex(0x28425e), 0);
    lv_obj_set_style_pad_all(chat_box, 4, 0);
    lv_obj_set_style_pad_row(chat_box, 3, 0);
    lv_obj_set_flex_flow(chat_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(chat_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_box, LV_SCROLLBAR_MODE_OFF);

    chat_add("按住下面的按钮，或按住 BOOT 键和我说话。\n"
             "我可以用真琴的声音回答，还能联网查资料。", false);

    /* 提示 */
    lbl_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_hint, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0xcfe0f2), 0);
    lv_obj_set_width(lbl_hint, 300);
    lv_obj_set_pos(lbl_hint, 10, 152);
    lv_obj_set_style_text_align(lbl_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl_hint, "");

    /* 麦克风电平 */
    bar_mic = lv_bar_create(scr);
    lv_obj_set_size(bar_mic, 280, 8);
    lv_obj_set_pos(bar_mic, 20, 176);
    lv_bar_set_range(bar_mic, 0, 100);
    lv_bar_set_value(bar_mic, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_mic, lv_color_hex(0x1c2b3a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_mic, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_mic, COL_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_mic, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_mic, 4, LV_PART_INDICATOR);

    /* 按钮 */
    btn_talk = lv_btn_create(scr);
    lv_obj_set_size(btn_talk, 200, 40);
    lv_obj_set_pos(btn_talk, 60, 194);
    lv_obj_set_style_bg_color(btn_talk, lv_color_hex(0x1f8fff), 0);
    lv_obj_set_style_radius(btn_talk, 8, 0);
    lv_obj_set_style_shadow_width(btn_talk, 0, 0);

    lbl_btn = lv_label_create(btn_talk);
    lv_obj_set_style_text_font(lbl_btn, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0xffffff), 0);
    lv_label_set_text(lbl_btn, "按住说话");
    lv_obj_center(lbl_btn);

    lv_obj_add_event_cb(btn_talk, talk_event_cb, LV_EVENT_ALL, NULL);

    /* 返回桌面（AI 入口进入对话页后可用） */
    lv_obj_t *btn_desk = lv_btn_create(scr);
    lv_obj_set_size(btn_desk, 48, 40);
    lv_obj_set_pos(btn_desk, 6, 194);
    lv_obj_set_style_bg_color(btn_desk, lv_color_hex(0x1b3550), 0);
    lv_obj_set_style_radius(btn_desk, 8, 0);
    lv_obj_set_style_shadow_width(btn_desk, 0, 0);
    lv_obj_t *lbl_desk = lv_label_create(btn_desk);
    lv_obj_set_style_text_font(lbl_desk, FONT_CN, 0);
    lv_obj_set_style_text_color(lbl_desk, COL_TEXT, 0);
    lv_label_set_text(lbl_desk, "桌面");
    lv_obj_center(lbl_desk);
    lv_obj_add_event_cb(btn_desk, desk_event_cb, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();
}

void ui_update_state(app_state_t st)
{
    if (!lvgl_port_lock(0)) return;

    switch (st) {
    case ST_BOOT:
        lv_label_set_text(lbl_state, "开机中");
        lv_obj_set_style_text_color(lbl_state, COL_DIM, 0);
        break;
    case ST_IDLE:
        lv_label_set_text(lbl_state, "待机");
        lv_obj_set_style_text_color(lbl_state, COL_DIM, 0);
        lv_label_set_text(lbl_btn, "按住说话");
        lv_obj_set_style_bg_color(btn_talk, lv_color_hex(0x1f8fff), 0);
        lv_bar_set_value(bar_mic, 0, LV_ANIM_OFF);
        break;
    case ST_LISTENING:
        lv_label_set_text(lbl_state, "聆听中…");
        lv_obj_set_style_text_color(lbl_state, COL_GREEN, 0);
        lv_label_set_text(lbl_btn, "松开结束");
        lv_obj_set_style_bg_color(btn_talk, lv_color_hex(0x18a058), 0);
        break;
    case ST_THINKING:
        lv_label_set_text(lbl_state, "思考中…");
        lv_obj_set_style_text_color(lbl_state, COL_YELLOW, 0);
        lv_label_set_text(lbl_btn, "请稍等");
        lv_obj_set_style_bg_color(btn_talk, lv_color_hex(0x8a6d1a), 0);
        lv_bar_set_value(bar_mic, 0, LV_ANIM_OFF);
        break;
    case ST_SPEAKING:
        lv_label_set_text(lbl_state, "说话中…");
        lv_obj_set_style_text_color(lbl_state, COL_CYAN, 0);
        lv_label_set_text(lbl_btn, "点击打断");
        lv_obj_set_style_bg_color(btn_talk, lv_color_hex(0x8a2b2b), 0);
        break;
    case ST_ERROR:
        lv_label_set_text(lbl_state, "离线");
        lv_obj_set_style_text_color(lbl_state, COL_RED, 0);
        lv_label_set_text(lbl_btn, "按住说话");
        break;
    }
    lvgl_port_unlock();
}

void ui_set_user_text(const char *text)
{
    if (!lvgl_port_lock(0)) return;
    chat_add(text, true);
    lvgl_port_unlock();
}

void ui_set_ai_text(const char *text)
{
    if (!lvgl_port_lock(0)) return;
    chat_add(text, false);
    lvgl_port_unlock();
}

void ui_set_hint(const char *text)
{
    if (!lvgl_port_lock(0)) return;
    set_label_text(lbl_hint, text);
    lvgl_port_unlock();
}

void ui_set_mic_level(int level)
{
    static int last = -1;
    if (level == last) return;
    last = level;
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    if (!lvgl_port_lock(0)) return;
    lv_bar_set_value(bar_mic, level, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void ui_set_wifi(bool connected, const char *ssid)
{
    if (!lvgl_port_lock(0)) return;
    if (connected) {
        lv_label_set_text_fmt(lbl_wifi, "WiFi %s", ssid ? ssid : "");
        lv_obj_set_style_text_color(lbl_wifi, COL_GREEN, 0);
    } else {
        lv_label_set_text(lbl_wifi, "配网");
        lv_obj_set_style_text_color(lbl_wifi, COL_RED, 0);
    }
    lvgl_port_unlock();
}

void ui_set_voice(const char *name)
{
    if (!lvgl_port_lock(0)) return;
    char buf[64];
    utf8_copy(buf, sizeof(buf), name ? name : "--", 48);
    lv_label_set_text_fmt(lbl_voice, "音色: %s", buf);
    lvgl_port_unlock();
}
