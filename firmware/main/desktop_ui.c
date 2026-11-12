#include "desktop_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "app.h"
#include "audio_io.h"
#include "camera_app.h"
#include "esp32_s3_szp.h"
#include "music_app.h"
#include "net_ws.h"
#include "note_store.h"
#include "wifi.h"

#define TAG "desktop"

LV_FONT_DECLARE(font_cn_16);
LV_FONT_DECLARE(clock_font);
LV_IMG_DECLARE(desktop_bg);
LV_IMG_DECLARE(music_bg);
LV_IMG_DECLARE(makoto_standby);
LV_IMG_DECLARE(icon_ai);
LV_IMG_DECLARE(icon_games);
LV_IMG_DECLARE(icon_note);
LV_IMG_DECLARE(icon_settings);
LV_IMG_DECLARE(icon_music);
LV_IMG_DECLARE(icon_camera);
LV_IMG_DECLARE(shinchan_icon);
LV_IMG_DECLARE(shinchan_space);
LV_IMG_DECLARE(shinchan_kb);

static const lv_font_t *F = &font_cn_16;

#define C_BG      lv_color_hex(0x0b1020)
#define C_PANEL   lv_color_hex(0x14263a)
#define C_PANEL2  lv_color_hex(0x152336)
#define C_TEXT    lv_color_hex(0xf2f7ff)
#define C_DIM     lv_color_hex(0x8fa6bd)
#define C_CYAN    lv_color_hex(0x39d0ff)
#define C_GREEN   lv_color_hex(0x63e6a8)
#define C_RED     lv_color_hex(0xff6b6b)

enum {
    APP_NONE = 0,
    APP_AI = 100,
    APP_GAMES = 101,
    APP_NOTE = 102,
    APP_SETTINGS = 103,
    APP_MUSIC = 104,
    APP_CAMERA = 105,
    APP_2048 = 200,
    APP_SNAKE = 201,
};

static const int kWidth = 320;
static const int kHeight = 240;

#define STANDBY_CLOCK_Y   36
#define STANDBY_TEXT_OPA  220

static lv_obj_t *root_;
static lv_obj_t *launcher_;
static lv_obj_t *standby_;
static lv_obj_t *standby_img_ = NULL;
static lv_obj_t *standby_clock_ = NULL;
static bool standby_shown_ = false;
static lv_point_t standby_press_pt_;
static bool standby_swipe_done_ = true;
static lv_obj_t *app_root_;
static lv_obj_t *app_title_;
static lv_obj_t *cam_img_ = NULL;
static lv_obj_t *clock_label_;
static lv_obj_t *wifi_label_;
static lv_obj_t *toast_;
static lv_timer_t *state_timer_;
static lv_timer_t *clock_timer_;
static lv_timer_t *toast_timer_;
static lv_timer_t *snake_timer_;
static bool ai_override_;
static int idle_ticks_;
static int brightness_ = 70;

static void open_app(int app);
static void close_app(void);
static bool desktop_should_show(void);

static void cb_back(lv_event_t *e)
{
    (void)e;
    close_app();
}

static void cb_2048_over(lv_event_t *e)
{
    (void)e;
    open_app(APP_2048);
}

static void cb_snake_over(lv_event_t *e)
{
    (void)e;
    open_app(APP_SNAKE);
}

static void toast_timeout_cb(lv_timer_t *t)
{
    if (toast_) lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    toast_timer_ = NULL;
    lv_timer_del(t);
}

/* ---------------- 通用 ------------- */

static lv_obj_t *make_obj(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_color_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (radius > 0) lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_color_t color,
                            int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, F, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    return l;
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_color_t color, const char *text, lv_color_t text_color,
                          lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, F, 0);
    lv_obj_set_style_text_color(l, text_color, 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

static void show_toast(const char *text)
{
    if (!toast_) {
        toast_ = make_obj(root_, 80, 188, 160, 34, lv_color_hex(0x1f8fff), 8);
        make_label(toast_, text, lv_color_hex(0xffffff), 0, 8, 160);
        lv_obj_t *l = lv_obj_get_child(toast_, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_label_set_text(lv_obj_get_child(toast_, 0), text);
    }
    lv_obj_clear_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    if (toast_timer_) {
        lv_timer_del(toast_timer_);
        toast_timer_ = NULL;
    }
    toast_timer_ = lv_timer_create(toast_timeout_cb, 1200, NULL);
}

/* ---------------- Launcher ------------- */

static void tile_cb(lv_event_t *e)
{
    int app = (int)(intptr_t)lv_event_get_user_data(e);
    if (app == APP_AI) {
        desktop_ui_enter_ai();
        return;
    }
    open_app(app);
}

static void build_launcher(void)
{
    launcher_ = lv_obj_create(root_);
    lv_obj_remove_style_all(launcher_);
    lv_obj_set_size(launcher_, kWidth, kHeight);
    lv_obj_set_pos(launcher_, 0, 0);
    lv_obj_clear_flag(launcher_, LV_OBJ_FLAG_SCROLLABLE);

    /* 主桌面背景：01.jpg */
    lv_obj_t *bg = lv_img_create(launcher_);
    lv_img_set_src(bg, &desktop_bg);
    lv_obj_set_pos(bg, 0, 0);

    lv_obj_t *head = make_obj(launcher_, 0, 0, kWidth, 26, C_PANEL, 0);
    lv_obj_set_style_bg_opa(head, 140, 0);
    clock_label_ = make_label(head, "--:--:--", C_TEXT, 8, 4, 0);
    lv_obj_t *brand = make_label(head, "makoto", lv_color_hex(0x9fc4e8), 0, 5, kWidth);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    wifi_label_ = make_label(head, "", C_CYAN, 0, 4, 0);
    lv_obj_align(wifi_label_, LV_ALIGN_RIGHT_MID, -8, 0);

    struct Tile { const lv_img_dsc_t *icon; const char *name; int app; };
    const struct Tile tiles[] = {
        {&icon_ai, "AI 助手", APP_AI},
        {&icon_games, "小游戏", APP_GAMES},
        {&icon_note, "记事本", APP_NOTE},
        {&icon_settings, "设置", APP_SETTINGS},
        {&icon_music, "音乐", APP_MUSIC},
        {&icon_camera, "相机", APP_CAMERA},
    };

    for (int i = 0; i < 6; i++) {
        int x = 6 + (i % 3) * 104;
        /* 标题栏 26px，下面 214px 的可用区里垂直居中：
         * 两行共 74*2 + 8 = 156，(214-156)/2 = 29，26+29 = 55 */
        int y = 55 + (i / 3) * 82;
        lv_obj_t *b = lv_btn_create(launcher_);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_size(b, 100, 74);
        /* 去掉最外圈圆角矩形和阴影，只保留图标 + 名称 */
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);

        lv_obj_t *img = lv_img_create(b);
        lv_img_set_src(img, tiles[i].icon);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *n = lv_label_create(b);
        lv_label_set_text(n, tiles[i].name);
        lv_obj_set_style_text_font(n, F, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(0xf2f7ff), 0);
        lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -2);

        lv_obj_add_event_cb(b, tile_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)tiles[i].app);
    }

}

/* ---------------- 2048 ------------- */

/* ---------------- 待机画面 ---------------- */

static void hide_standby(void)
{
    standby_shown_ = false;
    if (standby_) lv_obj_add_flag(standby_, LV_OBJ_FLAG_HIDDEN);
}

static void show_standby(void)
{
    if (!standby_ || standby_shown_) return;
    standby_shown_ = true;
    standby_swipe_done_ = true;
    if (standby_img_) lv_obj_set_style_img_opa(standby_img_, LV_OPA_COVER, 0);
    if (standby_clock_) {
        lv_obj_set_style_text_opa(standby_clock_, STANDBY_TEXT_OPA, 0);
        lv_obj_set_y(standby_clock_, STANDBY_CLOCK_Y);
    }
    lv_obj_clear_flag(standby_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(standby_);
}

/* 淡出：图片和数字一起慢慢变透明，露出后面的桌面 */
static void standby_fade_cb(void *var, int32_t v)
{
    (void)var;
    if (standby_img_) {
        lv_obj_set_style_img_opa(standby_img_, (lv_opa_t)v, 0);
    }
    if (standby_clock_) {
        lv_obj_set_style_text_opa(standby_clock_,
                                  (lv_opa_t)((v * STANDBY_TEXT_OPA) / 255), 0);
    }
}

static void standby_fade_done(lv_anim_t *a)
{
    (void)a;
    hide_standby();
    if (standby_img_) {
        lv_obj_set_style_img_opa(standby_img_, LV_OPA_COVER, 0);
    }
    if (standby_clock_) {
        lv_obj_set_style_text_opa(standby_clock_, STANDBY_TEXT_OPA, 0);
        lv_obj_set_y(standby_clock_, STANDBY_CLOCK_Y);
    }
}

static void standby_fade_out(void)
{
    if (!standby_ || !standby_shown_) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, standby_);
    lv_anim_set_exec_cb(&a, standby_fade_cb);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_time(&a, 380);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, standby_fade_done);
    lv_anim_start(&a);
}

static void standby_unlock(void)
{
    standby_swipe_done_ = true;
    ai_override_ = false;
    if (root_ && desktop_should_show()) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    standby_fade_out();
}

static void standby_clock_y_anim_cb(void *obj, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void cb_standby_pressed(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) lv_indev_get_point(indev, &standby_press_pt_);
    standby_swipe_done_ = false;
}

/* 拖动时数字跟着手指往上走 */
static void cb_standby_pressing(lv_event_t *e)
{
    (void)e;
    if (standby_swipe_done_ || !standby_clock_) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = standby_press_pt_.y - p.y;
    if (dy < 0) dy = 0;
    if (dy > 160) dy = 160;
    lv_obj_set_y(standby_clock_, STANDBY_CLOCK_Y - dy);
}

/* 松手：上滑 >=45px 且横向偏移 <=80px 才解锁；不够则弹回 */
static void cb_standby_released(lv_event_t *e)
{
    (void)e;
    if (standby_swipe_done_) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int dy = standby_press_pt_.y - p.y;
    int dx = abs(p.x - standby_press_pt_.x);
    if (dy >= 45 && dx <= 80) {
        standby_unlock();
        return;
    }
    if (standby_clock_) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, standby_clock_);
        lv_anim_set_exec_cb(&a, standby_clock_y_anim_cb);
        lv_anim_set_values(&a, lv_obj_get_y(standby_clock_), STANDBY_CLOCK_Y);
        lv_anim_set_time(&a, 180);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

static void build_standby(void)
{
    standby_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(standby_);
    lv_obj_set_pos(standby_, 0, 0);
    lv_obj_set_size(standby_, kWidth, kHeight);
    lv_obj_clear_flag(standby_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(standby_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(standby_, cb_standby_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(standby_, cb_standby_pressing, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(standby_, cb_standby_released, LV_EVENT_RELEASED, NULL);

    standby_img_ = lv_img_create(standby_);
    lv_img_set_src(standby_img_, &makoto_standby);
    lv_obj_set_pos(standby_img_, 0, 0);
    lv_obj_clear_flag(standby_img_, LV_OBJ_FLAG_CLICKABLE);

    /* 白色艺术字时钟，没有底板/矩形阴影 */
    standby_clock_ = lv_label_create(standby_);
    lv_obj_set_pos(standby_clock_, 0, STANDBY_CLOCK_Y);
    lv_obj_set_width(standby_clock_, kWidth);
    lv_obj_set_style_text_align(standby_clock_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(standby_clock_, &clock_font, 0);
    lv_obj_set_style_text_color(standby_clock_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(standby_clock_, STANDBY_TEXT_OPA, 0);
    lv_label_set_text(standby_clock_, "--:--");
    lv_obj_clear_flag(standby_clock_, LV_OBJ_FLAG_CLICKABLE);
}

static int g2048_[16];
static int g2048_score_;
static lv_obj_t *g2048_cells_[16];
static lv_obj_t *g2048_score_label_;
static lv_obj_t *g2048_over_;

static lv_color_t tile_color(int v)
{
    switch (v) {
    case 2: return lv_color_hex(0xeee4da);
    case 4: return lv_color_hex(0xede0c8);
    case 8: return lv_color_hex(0xf2b179);
    case 16: return lv_color_hex(0xf59563);
    case 32: return lv_color_hex(0xf67c5f);
    case 64: return lv_color_hex(0xf65e3b);
    case 128: return lv_color_hex(0xedcf72);
    case 256: return lv_color_hex(0xedcc61);
    case 512: return lv_color_hex(0xedc850);
    case 1024: return lv_color_hex(0xedc53f);
    case 2048: return lv_color_hex(0xedc22e);
    default: return lv_color_hex(0x1c2b3a);
    }
}

static void render_2048(void)
{
    for (int i = 0; i < 16; i++) {
        if (!g2048_cells_[i]) continue;
        int v = g2048_[i];
        lv_obj_set_style_bg_color(g2048_cells_[i], tile_color(v), 0);
        lv_obj_t *l = lv_obj_get_child(g2048_cells_[i], 0);
        if (v == 0) {
            lv_label_set_text(l, "");
        } else {
            lv_label_set_text_fmt(l, "%d", v);
            lv_obj_set_style_text_color(l, v <= 4 ? lv_color_hex(0x5a4b3c)
                                                  : lv_color_hex(0xffffff), 0);
        }
    }
    if (g2048_score_label_) {
        lv_label_set_text_fmt(g2048_score_label_, "分数\n%d", g2048_score_);
    }
}

static bool spawn_2048(void)
{
    int empty[16], n = 0;
    for (int i = 0; i < 16; i++) if (g2048_[i] == 0) empty[n++] = i;
    if (!n) return false;
    int idx = empty[esp_random() % n];
    g2048_[idx] = (esp_random() % 10 == 0) ? 4 : 2;
    return true;
}

static void move_line_2048(int a, int b, int c, int d, bool *moved)
{
    int idx[4] = {a, b, c, d};
    int tmp[4] = {0}, n = 0;
    for (int i = 0; i < 4; i++) if (g2048_[idx[i]]) tmp[n++] = g2048_[idx[i]];
    int out[4] = {0}, o = 0;
    for (int i = 0; i < n; i++) {
        if (i + 1 < n && tmp[i] == tmp[i + 1]) {
            out[o] = tmp[i] * 2;
            g2048_score_ += out[o];
            i++;
        } else {
            out[o] = tmp[i];
        }
        o++;
    }
    for (int i = 0; i < 4; i++) {
        if (g2048_[idx[i]] != out[i]) *moved = true;
        g2048_[idx[i]] = out[i];
    }
}

static bool is_2048_over(void)
{
    for (int i = 0; i < 16; i++) if (g2048_[i] == 0) return false;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int v = g2048_[r * 4 + c];
            if (c < 3 && v == g2048_[r * 4 + c + 1]) return false;
            if (r < 3 && v == g2048_[(r + 1) * 4 + c]) return false;
        }
    }
    return true;
}

static void show_2048_over(void)
{
    if (g2048_over_) return;
    g2048_over_ = make_obj(app_root_, 6, 44, 240, 148, lv_color_hex(0x0b1020), 8);
    lv_obj_set_style_bg_opa(g2048_over_, 210, 0);
    lv_obj_add_flag(g2048_over_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = make_label(g2048_over_, "游戏结束\n点击重开", C_TEXT, 0, 30, 240);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(g2048_over_, cb_2048_over, LV_EVENT_CLICKED, NULL);
}

static void move_2048(int dir)
{
    bool moved = false;
    for (int i = 0; i < 4; i++) {
        if (dir == 0) move_line_2048(i * 4, i * 4 + 1, i * 4 + 2, i * 4 + 3, &moved);
        else if (dir == 1) move_line_2048(i * 4 + 3, i * 4 + 2, i * 4 + 1, i * 4, &moved);
        else if (dir == 2) move_line_2048(i, 4 + i, 8 + i, 12 + i, &moved);
        else move_line_2048(12 + i, 8 + i, 4 + i, i, &moved);
    }
    if (moved) {
        spawn_2048();
        render_2048();
        if (is_2048_over()) show_2048_over();
    } else {
        render_2048();
    }
}

static void cb_2048_up(lv_event_t *e) { (void)e; move_2048(2); }
static void cb_2048_left(lv_event_t *e) { (void)e; move_2048(0); }
static void cb_2048_right(lv_event_t *e) { (void)e; move_2048(1); }
static void cb_2048_down(lv_event_t *e) { (void)e; move_2048(3); }
static void cb_restart_2048(lv_event_t *e) { (void)e; open_app(APP_2048); }

static void build_2048(void)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);

    g2048_score_ = 0;
    memset(g2048_, 0, sizeof(g2048_));
    spawn_2048();
    spawn_2048();

    for (int i = 0; i < 16; i++) {
        int x = 6 + (i % 4) * 61;
        int y = 44 + (i / 4) * 38;
        g2048_cells_[i] = make_obj(app_root_, x, y, 57, 34, lv_color_hex(0x1c2b3a), 5);
        lv_obj_t *l = lv_label_create(g2048_cells_[i]);
        lv_obj_set_style_text_font(l, F, 0);
        lv_obj_center(l);
    }

    g2048_score_label_ = make_label(app_root_, "分数\n0", C_TEXT, 252, 46, 62);
    lv_obj_set_style_text_align(g2048_score_label_, LV_TEXT_ALIGN_CENTER, 0);

    make_btn(app_root_, 268, 92, 30, 28, lv_color_hex(0x1b3550), "上", C_TEXT, cb_2048_up, NULL);
    make_btn(app_root_, 250, 122, 30, 28, lv_color_hex(0x1b3550), "左", C_TEXT, cb_2048_left, NULL);
    make_btn(app_root_, 286, 122, 30, 28, lv_color_hex(0x1b3550), "右", C_TEXT, cb_2048_right, NULL);
    make_btn(app_root_, 268, 152, 30, 28, lv_color_hex(0x1b3550), "下", C_TEXT, cb_2048_down, NULL);
    make_btn(app_root_, 250, 190, 66, 28, lv_color_hex(0x8a2b2b), "重开", C_TEXT, cb_restart_2048, NULL);

    render_2048();
}

/* ---------------- 贪吃蛇 ------------- */

#define SW 20
#define SH 15
#define SMAX 300
#define SDRAW 128

static int snake_x_[SMAX], snake_y_[SMAX];
static int snake_len_, snake_dir_, snake_fx_, snake_fy_, snake_score_;
static bool snake_dead_;
static lv_obj_t *snake_grid_, *snake_segs_[SDRAW], *snake_food_, *snake_over_;

static void place_food(void)
{
    if (snake_len_ >= SW * SH) {
        snake_fx_ = snake_fy_ = -1;
        return;
    }
    for (int tries = 0; tries < 500; tries++) {
        int x = esp_random() % SW, y = esp_random() % SH;
        bool hit = false;
        for (int i = 0; i < snake_len_; i++) {
            if (snake_x_[i] == x && snake_y_[i] == y) { hit = true; break; }
        }
        if (!hit) { snake_fx_ = x; snake_fy_ = y; return; }
    }
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            bool hit = false;
            for (int i = 0; i < snake_len_; i++) {
                if (snake_x_[i] == x && snake_y_[i] == y) { hit = true; break; }
            }
            if (!hit) { snake_fx_ = x; snake_fy_ = y; return; }
        }
    }
    snake_fx_ = snake_fy_ = -1;
}

static void render_snake(void)
{
    for (int i = 0; i < SDRAW; i++) {
        if (!snake_segs_[i]) continue;
        if (i < snake_len_) {
            lv_obj_set_pos(snake_segs_[i], snake_x_[i] * 12, snake_y_[i] * 12);
            lv_obj_set_style_bg_color(snake_segs_[i], i == 0 ? C_CYAN : C_GREEN, 0);
            lv_obj_clear_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (snake_food_) {
        if (snake_fx_ >= 0) {
            lv_obj_set_pos(snake_food_, snake_fx_ * 12, snake_fy_ * 12);
            lv_obj_clear_flag(snake_food_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(snake_food_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (app_title_) {
        lv_label_set_text_fmt(app_title_, "贪吃蛇 得分 %d", snake_score_);
    }
}

static void snake_die(void)
{
    snake_dead_ = true;
    if (snake_over_) return;
    snake_over_ = make_obj(app_root_, 4, 34, 240, 180, lv_color_hex(0x0b1020), 8);
    lv_obj_set_style_bg_opa(snake_over_, 200, 0);
    lv_obj_add_flag(snake_over_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = make_label(snake_over_, "游戏结束\n点击重开", C_TEXT, 0, 40, 240);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(snake_over_, cb_snake_over, LV_EVENT_CLICKED, NULL);
}

static void snake_tick(lv_timer_t *t)
{
    (void)t;
    if (snake_dead_) return;
    static const int dx[4] = {0, 1, 0, -1};
    static const int dy[4] = {-1, 0, 1, 0};
    int nx = snake_x_[0] + dx[snake_dir_];
    int ny = snake_y_[0] + dy[snake_dir_];
    if (nx < 0 || nx >= SW || ny < 0 || ny >= SH) { snake_die(); return; }
    bool eat = (snake_fx_ >= 0 && nx == snake_fx_ && ny == snake_fy_);
    int limit = eat ? snake_len_ : snake_len_ - 1;
    for (int i = 0; i < limit; i++) {
        if (snake_x_[i] == nx && snake_y_[i] == ny) { snake_die(); return; }
    }
    if (eat) {
        if (snake_len_ < SMAX) {
            for (int i = snake_len_; i > 0; i--) {
                snake_x_[i] = snake_x_[i - 1];
                snake_y_[i] = snake_y_[i - 1];
            }
            snake_len_++;
        }
        snake_score_ += 10;
        snake_x_[0] = nx; snake_y_[0] = ny;
        place_food();
    } else {
        for (int i = snake_len_ - 1; i > 0; i--) {
            snake_x_[i] = snake_x_[i - 1];
            snake_y_[i] = snake_y_[i - 1];
        }
        snake_x_[0] = nx; snake_y_[0] = ny;
    }
    render_snake();
}

static void set_dir(int d)
{
    if ((d + 2) % 4 == snake_dir_) return;
    snake_dir_ = d;
}

static void cb_snake_gesture(lv_event_t *e)
{
    (void)e;
    lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (d == LV_DIR_TOP) set_dir(0);
    else if (d == LV_DIR_BOTTOM) set_dir(2);
    else if (d == LV_DIR_LEFT) set_dir(3);
    else if (d == LV_DIR_RIGHT) set_dir(1);
}

static void cb_dir_up(lv_event_t *e) { (void)e; set_dir(0); }
static void cb_dir_left(lv_event_t *e) { (void)e; set_dir(3); }
static void cb_dir_right(lv_event_t *e) { (void)e; set_dir(1); }
static void cb_dir_down(lv_event_t *e) { (void)e; set_dir(2); }
static void cb_restart_snake(lv_event_t *e) { (void)e; open_app(APP_SNAKE); }

static void build_snake(void)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(app_root_, "贪吃蛇", C_TEXT, 0, 4, 210);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 10, 4);

    snake_len_ = 3;
    snake_dir_ = 1;
    snake_score_ = 0;
    snake_dead_ = false;
    snake_x_[0] = 10; snake_y_[0] = 7;
    snake_x_[1] = 9;  snake_y_[1] = 7;
    snake_x_[2] = 8;  snake_y_[2] = 7;
    place_food();

    snake_grid_ = make_obj(app_root_, 4, 34, SW * 12, SH * 12, lv_color_hex(0x0c1726), 4);
    lv_obj_add_flag(snake_grid_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(snake_grid_, cb_snake_gesture, LV_EVENT_GESTURE, NULL);
    for (int i = 0; i < SDRAW; i++) {
        snake_segs_[i] = make_obj(snake_grid_, 0, 0, 11, 11, C_GREEN, 2);
        lv_obj_add_flag(snake_segs_[i], LV_OBJ_FLAG_HIDDEN);
    }
    snake_food_ = make_obj(snake_grid_, 0, 0, 11, 11, C_RED, 2);
    lv_obj_add_flag(snake_food_, LV_OBJ_FLAG_HIDDEN);

    make_btn(app_root_, 266, 48, 34, 30, lv_color_hex(0x1b3550), "上", C_TEXT, cb_dir_up, NULL);
    make_btn(app_root_, 246, 82, 34, 30, lv_color_hex(0x1b3550), "左", C_TEXT, cb_dir_left, NULL);
    make_btn(app_root_, 284, 82, 34, 30, lv_color_hex(0x1b3550), "右", C_TEXT, cb_dir_right, NULL);
    make_btn(app_root_, 266, 116, 34, 30, lv_color_hex(0x1b3550), "下", C_TEXT, cb_dir_down, NULL);
    make_btn(app_root_, 246, 162, 72, 30, lv_color_hex(0x8a2b2b), "重开", C_TEXT, cb_restart_snake, NULL);

    render_snake();
    snake_timer_ = lv_timer_create(snake_tick, 180, NULL);
}

/* ---------------- 记事本 ------------- */

/* -------- 记事本拼音输入（服务端候选 + 蜡笔小新皮肤） -------- */
#define NOTE_CANDS 24
#define NOTE_COLS  8
#define NOTE_ROWS  3
#define SHIN_KEY        lv_color_hex(0xFFD84D)
#define SHIN_KEY_BORDER lv_color_hex(0xC98A1B)
#define SHIN_RED        lv_color_hex(0xE64A3B)
#define SHIN_ORANGE     lv_color_hex(0xF28C28)
#define SHIN_INK        lv_color_hex(0x3B2A1A)

static lv_obj_t *note_ta_ = NULL;
static lv_obj_t *note_kb_ = NULL;
static lv_obj_t *note_ime_btn_ = NULL;
static lv_obj_t *note_comp_bar_ = NULL;
static lv_obj_t *note_comp_lbl_ = NULL;
static lv_obj_t *note_preview_lbl_ = NULL;
static lv_obj_t *note_cand_scroll_ = NULL;
static lv_obj_t *note_prev_btn_ = NULL;
static lv_obj_t *note_next_btn_ = NULL;
static lv_obj_t *note_cand_col_[NOTE_COLS] = {NULL};
static lv_obj_t *note_cand_btn_[NOTE_CANDS] = {NULL};
static char note_pinyin_[40] = {0};
static char (*note_cands_)[96] = NULL;
static int note_cand_cnt_ = 0;
static int note_cand_page_ = 0;
static bool note_cand_more_ = false;
static uint32_t note_pinyin_id_ = 0;
static bool note_ime_on_ = true;
static lv_keyboard_mode_t note_kb_mode_ = LV_KEYBOARD_MODE_TEXT_LOWER;

static void note_update_page_btns(void);

static size_t utf8_char_len(unsigned char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* 候选显示：最多 5 个汉字，超出加省略号（实际提交的仍是完整文本） */
static void note_cand_display(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;
    int chars = 0;
    while (src[i] && chars < 2) {
        i += utf8_char_len((unsigned char)src[i]);
        chars++;
    }
    if (src[i]) {
        snprintf(dst, dst_size, "%.*s…", (int)i, src);
    } else {
        snprintf(dst, dst_size, "%s", src);
    }
}

static void note_update_ime_btn(void)
{
    if (!note_ime_btn_) return;
    lv_obj_t *l = lv_obj_get_child(note_ime_btn_, 0);
    if (l) lv_label_set_text(l, note_ime_on_ ? "中" : "英");
    lv_obj_set_style_bg_color(note_ime_btn_,
        note_ime_on_ ? SHIN_RED : lv_color_hex(0x1b3550), 0);
}

/* 顶部预览第一条候选：直接点它就能上屏 */
static void note_update_preview(void)
{
    if (!note_preview_lbl_) return;
    if (note_cand_cnt_ > 0 && note_cands_[0][0]) {
        lv_label_set_text(note_preview_lbl_, note_cands_[0]);
    } else {
        lv_label_set_text(note_preview_lbl_, "…");
    }
}

static void note_update_comp_label(void)
{
    if (!note_comp_lbl_) return;
    char out[32];
    size_t n = strlen(note_pinyin_);
    if (n > 11) {
        snprintf(out, sizeof(out), "…%.10s", note_pinyin_ + (n - 10));
    } else {
        snprintf(out, sizeof(out), "%.11s", note_pinyin_);
    }
    lv_label_set_text(note_comp_lbl_, out);
    note_update_preview();
}

/* 结束组合：不插入任何文字 */
static void note_end_comp(void)
{
    note_pinyin_[0] = 0;
    note_cand_cnt_ = 0;
    note_cand_page_ = 0;
    note_cand_more_ = false;
    note_pinyin_id_++;
    if (note_comp_bar_) lv_obj_add_flag(note_comp_bar_, LV_OBJ_FLAG_HIDDEN);
}

/* 组合上屏：把 text 插到光标处 */
static void note_commit(const char *text)
{
    if (note_ta_ && text && text[0]) {
        lv_textarea_add_text(note_ta_, text);
    }
    note_end_comp();
}

/* 点预览条 = 上屏第一条候选 */
static void cb_note_cand_top(lv_event_t *e)
{
    (void)e;
    if (note_cand_cnt_ > 0) note_commit(note_cands_[0]);
}

/* 每次按键都向服务端要一次候选；id 递增，只认最后一次的结果 */
static void note_request_pinyin(void)
{
    if (!note_comp_bar_) return;
    note_cand_cnt_ = 0;
    for (int i = 0; i < NOTE_CANDS; i++) {
        if (note_cand_btn_[i]) lv_obj_add_flag(note_cand_btn_[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int c = 0; c < NOTE_COLS; c++) {
        if (note_cand_col_[c]) lv_obj_add_flag(note_cand_col_[c], LV_OBJ_FLAG_HIDDEN);
    }
    note_update_comp_label();
    lv_obj_clear_flag(note_comp_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(note_comp_bar_);

    if (note_cand_col_[0]) lv_obj_clear_flag(note_cand_col_[0], LV_OBJ_FLAG_HIDDEN);
    if (note_cand_btn_[0]) {
        lv_obj_t *l = lv_obj_get_child(note_cand_btn_[0], 0);
        if (l) lv_label_set_text(l, "…");
        lv_obj_clear_flag(note_cand_btn_[0], LV_OBJ_FLAG_HIDDEN);
    }
    if (note_cand_scroll_) lv_obj_scroll_to_x(note_cand_scroll_, 0, LV_ANIM_OFF);

    note_cand_more_ = false;
    note_update_page_btns();
    note_pinyin_id_++;
    net_ws_send_pinyin(note_pinyin_id_, note_pinyin_,
                       note_cand_page_ * NOTE_CANDS);
}

static void note_update_page_btns(void)
{
    if (note_prev_btn_) {
        if (note_cand_page_ > 0)
            lv_obj_clear_state(note_prev_btn_, LV_STATE_DISABLED);
        else
            lv_obj_add_state(note_prev_btn_, LV_STATE_DISABLED);
    }
    if (note_next_btn_) {
        if (note_cand_more_)
            lv_obj_clear_state(note_next_btn_, LV_STATE_DISABLED);
        else
            lv_obj_add_state(note_next_btn_, LV_STATE_DISABLED);
    }
}

static void cb_note_page_prev(lv_event_t *e)
{
    (void)e;
    if (note_cand_page_ <= 0) return;
    note_cand_page_--;
    note_request_pinyin();
}

static void cb_note_page_next(lv_event_t *e)
{
    (void)e;
    if (!note_cand_more_) return;
    note_cand_page_++;
    note_request_pinyin();
}

static void note_add_pinyin_char(char c)
{
    size_t n = strlen(note_pinyin_);
    if (n + 1 >= sizeof(note_pinyin_)) return;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    note_pinyin_[n] = c;
    note_pinyin_[n + 1] = 0;
    note_cand_page_ = 0;
    note_cand_more_ = false;
    note_request_pinyin();
}

static void cb_note_ime(lv_event_t *e)
{
    (void)e;
    note_ime_on_ = !note_ime_on_;
    if (!note_ime_on_) note_end_comp();
    note_update_ime_btn();
}

static void cb_note_cand(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= note_cand_cnt_) return;
    note_commit(note_cands_[i]);
}

static void cb_note_kb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(kb, id);
    if (!txt || !txt[0]) return;

    /* 切换 abc / ABC / 1# 时，默认回调会先换整张键盘的 map。
     * 这时再读按钮文字会读到新 map 的同一个位置（例如 "1"），
     * 所以用 mode 是否变化来判断并跳过。 */
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(kb);
    if (mode != note_kb_mode_) {
        note_kb_mode_ = mode;
        return;
    }

    if (!strcmp(txt, " ")) {
        if (note_pinyin_[0]) {
            note_commit(note_cand_cnt_ > 0 ? note_cands_[0] : note_pinyin_);
            if (note_ta_) lv_textarea_add_char(note_ta_, ' ');
        } else if (note_ta_) {
            lv_textarea_add_char(note_ta_, ' ');
        }
        return;
    }

    if (!strcmp(txt, LV_SYMBOL_BACKSPACE)) {
        size_t n = strlen(note_pinyin_);
        if (n > 0) {
            note_pinyin_[n - 1] = 0;
            note_cand_page_ = 0;
            note_cand_more_ = false;
            if (note_pinyin_[0]) note_request_pinyin();
            else note_end_comp();
        } else if (note_ta_) {
            lv_textarea_del_char(note_ta_);
        }
        return;
    }

    if (!strcmp(txt, LV_SYMBOL_LEFT)) {
        if (note_ta_) lv_textarea_cursor_left(note_ta_);
        return;
    }
    if (!strcmp(txt, LV_SYMBOL_RIGHT)) {
        if (note_ta_) lv_textarea_cursor_right(note_ta_);
        return;
    }

    if (!strcmp(txt, LV_SYMBOL_NEW_LINE) || !strcmp(txt, "Enter") ||
        !strcmp(txt, LV_SYMBOL_OK)) {
        if (note_pinyin_[0]) {
            note_commit(note_cand_cnt_ > 0 ? note_cands_[0] : note_pinyin_);
        } else if (note_ta_) {
            lv_textarea_add_char(note_ta_, '\n');
        }
        return;
    }

    /* 键盘左下角的“收起键盘”键，默认回调已经处理了 CANCEL，这里忽略 */
    if (!strcmp(txt, LV_SYMBOL_KEYBOARD) || !strcmp(txt, LV_SYMBOL_CLOSE)) return;

    /* 普通字符 */
    if (note_ime_on_ && txt[1] == 0 &&
        ((txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' && txt[0] <= 'Z'))) {
        note_add_pinyin_char(txt[0]);
        return;
    }

    /* 英文模式，或拼音组合中的数字/标点：先把组合上屏，再插入该字符 */
    if (note_pinyin_[0]) {
        note_commit(note_cand_cnt_ > 0 ? note_cands_[0] : note_pinyin_);
    }
    if (note_ta_) lv_textarea_add_text(note_ta_, txt);
}

static void cb_note_save(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    note_save(lv_textarea_get_text(ta));
    show_toast("已保存");
}

static void cb_note_clear(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
    note_end_comp();
    lv_textarea_set_text(ta, "");
    note_clear();
    show_toast("已清空");
}

static void build_note(void)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);

    /* 蜡笔小新头像 */
    lv_obj_t *ava = lv_img_create(app_root_);
    lv_img_set_src(ava, &shinchan_icon);
    lv_obj_set_pos(ava, 54, 0);

    app_title_ = make_label(app_root_, "记事本", C_TEXT, 86, 4, 60);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_LEFT, 0);

    note_ime_btn_ = make_btn(app_root_, 150, 2, 40, 24,
                             SHIN_RED, "中", C_TEXT, cb_note_ime, NULL);
    lv_obj_t *save = make_btn(app_root_, 194, 2, 54, 24,
                              lv_color_hex(0x18a058), "保存", C_TEXT, NULL, NULL);
    lv_obj_t *clear = make_btn(app_root_, 252, 2, 64, 24,
                               lv_color_hex(0x8a2b2b), "清空", C_TEXT, NULL, NULL);

    lv_obj_t *ta = lv_textarea_create(app_root_);
    lv_obj_set_pos(ta, 4, 30);
    lv_obj_set_size(ta, 312, 70);
    lv_obj_set_style_text_font(ta, F, 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0a1320), 0);
    lv_obj_set_style_text_color(ta, C_TEXT, 0);
    lv_obj_set_style_border_width(ta, 2, 0);
    lv_obj_set_style_border_color(ta, SHIN_KEY, 0);
    lv_obj_set_style_radius(ta, 8, 0);
    char buf[1801] = {0};
    note_load(buf, sizeof(buf));
    lv_textarea_set_text(ta, buf);
    note_ta_ = ta;
    note_pinyin_[0] = 0;
    note_cand_cnt_ = 0;
    if (!note_cands_) {
        note_cands_ = heap_caps_malloc((size_t)NOTE_CANDS * 96,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!note_cands_) note_cands_ = heap_caps_malloc((size_t)NOTE_CANDS * 96,
                                                         MALLOC_CAP_8BIT);
    }

    /* 组合输入候选条：盖在输入框上；上行拼音+首选，下方可横向滑动的候选 */
    note_comp_bar_ = make_obj(app_root_, 4, 30, 312, 84, lv_color_hex(0x0a1320), 8);
    lv_obj_set_style_border_width(note_comp_bar_, 2, 0);
    lv_obj_set_style_border_color(note_comp_bar_, SHIN_KEY, 0);

    note_comp_lbl_ = lv_label_create(note_comp_bar_);
    lv_obj_set_pos(note_comp_lbl_, 6, 2);
    lv_obj_set_width(note_comp_lbl_, 90);
    lv_label_set_long_mode(note_comp_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(note_comp_lbl_, F, 0);
    lv_obj_set_style_text_color(note_comp_lbl_, C_CYAN, 0);
    lv_label_set_text(note_comp_lbl_, "");

    note_preview_lbl_ = lv_label_create(note_comp_bar_);
    lv_obj_set_pos(note_preview_lbl_, 100, 2);
    lv_obj_set_width(note_preview_lbl_, 150);
    lv_label_set_long_mode(note_preview_lbl_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(note_preview_lbl_, F, 0);
    lv_obj_set_style_text_color(note_preview_lbl_, C_TEXT, 0);
    lv_label_set_text(note_preview_lbl_, "");
    lv_obj_add_flag(note_preview_lbl_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(note_preview_lbl_, cb_note_cand_top, LV_EVENT_CLICKED, NULL);

    /* 候选翻页：每页 16 个，服务端每键候选已扩到 32 个 */
    note_prev_btn_ = make_btn(note_comp_bar_, 254, 1, 24, 20,
                              lv_color_hex(0x1b3550), "<", C_TEXT,
                              cb_note_page_prev, NULL);
    note_next_btn_ = make_btn(note_comp_bar_, 280, 1, 24, 20,
                              lv_color_hex(0x1b3550), ">", C_TEXT,
                              cb_note_page_next, NULL);
    lv_obj_add_state(note_prev_btn_, LV_STATE_DISABLED);
    lv_obj_add_state(note_next_btn_, LV_STATE_DISABLED);

    /* 横向滑动的候选区：8 列 x 2 行 = 16 个候选 */
    note_cand_scroll_ = lv_obj_create(note_comp_bar_);
    lv_obj_remove_style_all(note_cand_scroll_);
    lv_obj_set_pos(note_cand_scroll_, 4, 24);
    lv_obj_set_size(note_cand_scroll_, 308, 60);
    lv_obj_set_scroll_dir(note_cand_scroll_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(note_cand_scroll_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(note_cand_scroll_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(note_cand_scroll_, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(note_cand_scroll_, 0, 0);
    lv_obj_set_style_pad_column(note_cand_scroll_, 2, 0);
    lv_obj_set_style_bg_opa(note_cand_scroll_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(note_cand_scroll_, 0, 0);
    lv_obj_set_style_width(note_cand_scroll_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(note_cand_scroll_, SHIN_KEY, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(note_cand_scroll_, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(note_cand_scroll_, 2, LV_PART_SCROLLBAR);

    for (int c = 0; c < NOTE_COLS; c++) {
        lv_obj_t *col = lv_obj_create(note_cand_scroll_);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, 36, 60);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
        note_cand_col_[c] = col;
        for (int r = 0; r < NOTE_ROWS; r++) {
            int i = c * NOTE_ROWS + r;
            lv_obj_t *b = lv_btn_create(col);
            lv_obj_set_pos(b, 0, r * 20);
            lv_obj_set_size(b, 36, 20);
            lv_obj_set_style_bg_color(b, SHIN_KEY, 0);
            lv_obj_set_style_bg_color(b, SHIN_RED, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(b, 6, 0);
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, SHIN_KEY_BORDER, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_pad_all(b, 0, 0);
            lv_obj_t *l = lv_label_create(b);
            lv_obj_set_style_text_font(l, F, 0);
            lv_obj_set_style_text_color(l, SHIN_INK, 0);
            lv_label_set_text(l, "");
            lv_obj_center(l);
            lv_obj_add_event_cb(b, cb_note_cand, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            note_cand_btn_[i] = b;
        }
        lv_obj_add_flag(col, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(note_comp_bar_, LV_OBJ_FLAG_HIDDEN);

    /* 键盘背景：真实小新图片，键盘半透明按键盖在上面 */
    lv_obj_t *kb_bg = lv_img_create(app_root_);
    lv_img_set_src(kb_bg, &shinchan_kb);
    lv_obj_set_pos(kb_bg, 2, 114);
    lv_obj_clear_flag(kb_bg, LV_OBJ_FLAG_CLICKABLE);

    /* 键盘 4 行，放在 y=118，高 118，底部 = 236；和候选条之间留 4px 间距。
     * lv_keyboard 构造时默认 align=BOTTOM_MID，set_pos 前必须先改 TOP_LEFT。 */
    lv_obj_t *kb = lv_keyboard_create(app_root_);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_set_align(kb, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(kb, 2, 118);
    lv_obj_set_size(kb, 316, 118);

    /* 蜡笔小新皮肤：背景是真实图片，按键半透明黄色，按下红色、特殊键橙色 */
    lv_obj_set_style_bg_color(kb, SHIN_RED, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(kb, 10, 0);
    lv_obj_set_style_border_width(kb, 2, 0);
    lv_obj_set_style_border_color(kb, SHIN_KEY, 0);
    lv_obj_set_style_pad_all(kb, 3, 0);
    lv_obj_set_style_pad_row(kb, 2, 0);
    lv_obj_set_style_pad_column(kb, 2, 0);

    lv_obj_set_style_bg_color(kb, SHIN_KEY, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, 190, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, SHIN_KEY_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, SHIN_INK, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, SHIN_RED, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(kb, 235, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xffffff),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, SHIN_ORANGE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, 220, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xffffff),
                                LV_PART_ITEMS | LV_STATE_CHECKED);

    /* 空格键上的真实小新头像；不拦截触摸，空格照常能按 */
    lv_obj_t *space_img = lv_img_create(kb);
    lv_img_set_src(space_img, &shinchan_space);
    lv_obj_set_pos(space_img, 143, 94);
    lv_obj_clear_flag(space_img, LV_OBJ_FLAG_CLICKABLE);
    note_kb_ = kb;
    note_kb_mode_ = lv_keyboard_get_mode(kb);
    lv_obj_add_event_cb(kb, cb_note_kb, LV_EVENT_VALUE_CHANGED, NULL);

    note_update_ime_btn();
    lv_obj_add_event_cb(save, cb_note_save, LV_EVENT_CLICKED, ta);
    lv_obj_add_event_cb(clear, cb_note_clear, LV_EVENT_CLICKED, ta);
}

/* 服务端拼音候选到达（WS 任务里调用，自己加 LVGL 锁） */
void desktop_ui_pinyin_result(int id, const char *packed)
{
    if (!lvgl_port_lock(0)) return;
    if (!note_ta_ || (uint32_t)id != note_pinyin_id_) {
        lvgl_port_unlock();
        return;
    }

    int n = 0;
    const char *p = packed ? packed : "";
    while (*p && n < NOTE_CANDS) {
        const char *sep = strchr(p, 0x1f);
        size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len >= sizeof(note_cands_[n])) len = sizeof(note_cands_[n]) - 1;
        memcpy(note_cands_[n], p, len);
        note_cands_[n][len] = 0;
        n++;
        if (!sep) break;
        p = sep + 1;
    }
    note_cand_cnt_ = n;
    note_cand_more_ = (n >= NOTE_CANDS);
    note_update_page_btns();

    for (int i = 0; i < NOTE_CANDS; i++) {
        if (!note_cand_btn_[i]) continue;
        if (i < n && note_cands_[i][0]) {
            char disp[24];
            note_cand_display(disp, sizeof(disp), note_cands_[i]);
            lv_obj_t *l = lv_obj_get_child(note_cand_btn_[i], 0);
            if (l) lv_label_set_text(l, disp);
            lv_obj_clear_flag(note_cand_btn_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(note_cand_btn_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (int c = 0; c < NOTE_COLS; c++) {
        bool any = false;
        for (int r = 0; r < NOTE_ROWS; r++) {
            int i = c * NOTE_ROWS + r;
            if (i < n && note_cands_[i][0]) any = true;
        }
        if (note_cand_col_[c]) {
            if (any) lv_obj_clear_flag(note_cand_col_[c], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(note_cand_col_[c], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (note_cand_scroll_) {
        lv_obj_update_layout(note_cand_scroll_);
        lv_obj_scroll_to_x(note_cand_scroll_, 0, LV_ANIM_OFF);
    }
    note_update_preview();
    lvgl_port_unlock();
}

/* ---------------- 设置 ------------- */

void desktop_ui_set_brightness(int v)
{
    if (v < 1) v = 1;
    if (v > 100) v = 100;
    brightness_ = v;
    bsp_display_brightness_set(v);
}

int desktop_ui_get_brightness(void)
{
    return brightness_;
}

static void cb_bright(lv_event_t *e)
{
    desktop_ui_set_brightness(lv_slider_get_value(lv_event_get_target(e)));
}

static void cb_vol(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    audio_set_volume(v);
}

static void cb_wifi_reset(lv_event_t *e)
{
    (void)e;
    wifi_reset_credentials();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void cb_reboot(lv_event_t *e)
{
    (void)e;
    esp_restart();
}

static void build_settings(void)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(app_root_, "设置", C_TEXT, 0, 4, 120);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 0, 4);

    make_label(app_root_, "亮度", C_TEXT, 8, 38, 0);
    lv_obj_t *bs = lv_slider_create(app_root_);
    lv_obj_set_pos(bs, 70, 44);
    lv_obj_set_size(bs, 220, 10);
    lv_slider_set_range(bs, 5, 100);
    lv_slider_set_value(bs, brightness_, LV_ANIM_OFF);
    lv_obj_add_event_cb(bs, cb_bright, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(app_root_, "音量", C_TEXT, 8, 76, 0);
    lv_obj_t *vs = lv_slider_create(app_root_);
    lv_obj_set_pos(vs, 70, 82);
    lv_obj_set_size(vs, 220, 10);
    lv_slider_set_range(vs, 0, 100);
    lv_slider_set_value(vs, audio_get_volume(), LV_ANIM_OFF);
    lv_obj_add_event_cb(vs, cb_vol, LV_EVENT_VALUE_CHANGED, NULL);

    char info[200];
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (nif) esp_netif_get_ip_info(nif, &ip);
    snprintf(info, sizeof(info), "WiFi：%s\nIP：%d.%d.%d.%d",
             wifi_get_ssid(), (int)(ip.ip.addr & 0xff), (int)((ip.ip.addr >> 8) & 0xff),
             (int)((ip.ip.addr >> 16) & 0xff), (int)((ip.ip.addr >> 24) & 0xff));
    make_label(app_root_, info, C_DIM, 8, 104, 304);

    make_btn(app_root_, 16, 180, 140, 40, lv_color_hex(0x1b3550), "重新配网", C_TEXT, cb_wifi_reset, NULL);
    make_btn(app_root_, 164, 180, 140, 40, lv_color_hex(0x8a2b2b), "重启设备", C_TEXT, cb_reboot, NULL);
}

/* ---------------- 占位页 / 菜单 ------------- */

static void cb_open_games_2048(lv_event_t *e) { (void)e; open_app(APP_2048); }
static void cb_open_games_snake(lv_event_t *e) { (void)e; open_app(APP_SNAKE); }

static void build_games_menu(void)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(app_root_, "小游戏", C_TEXT, 0, 4, 120);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 0, 4);

    make_btn(app_root_, 34, 70, 116, 86, lv_color_hex(0x8a6d1a), "2048", lv_color_hex(0xffffff),
             cb_open_games_2048, NULL);
    make_btn(app_root_, 170, 70, 116, 86, lv_color_hex(0x18a058), "贪吃蛇", lv_color_hex(0xffffff),
             cb_open_games_snake, NULL);
}

/* ---------------- 相机 ---------------- */

static void cb_shutter(lv_event_t *e)
{
    (void)e;
    camera_app_take_photo();
}

static void build_music(void)
{
    /* 音乐应用背景：02.jpg */
    lv_obj_t *bg = lv_img_create(app_root_);
    lv_img_set_src(bg, &music_bg);
    lv_obj_set_pos(bg, 0, 0);

    lv_obj_t *top = make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    lv_obj_set_style_bg_opa(top, 140, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(app_root_, "音乐", C_TEXT, 0, 4, 120);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 0, 4);
    music_app_build(app_root_);
}

static void build_camera(void)
{
    /* 音乐模式先停音乐，相机和音乐不抢资源 */
    net_ws_send_music_cmd("stop");
    /* 预览图铺满全屏，顶栏和底栏叠在它上面 */
    cam_img_ = lv_img_create(app_root_);
    lv_obj_set_pos(cam_img_, 0, 0);
    lv_obj_set_size(cam_img_, kWidth, kHeight);
    lv_obj_clear_flag(cam_img_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(cam_img_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cam_img_, LV_OPA_COVER, 0);

    lv_obj_t *top = make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(top, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(top, "相机", C_TEXT, 0, 4, 120);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *sd = make_label(top, "TF ...", C_DIM, 0, 4, 0);
    lv_obj_align(sd, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t *bar = make_obj(app_root_, 0, 200, kWidth, 40, C_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, 200, 0);   /* 半透明，预览还能透出来 */
    lv_obj_t *hint = make_label(bar, "轻按 BOOT 拍照", C_TEXT, 6, 12, 232);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_LEFT, 0);
    make_btn(bar, 248, 6, 66, 28, lv_color_hex(0x8a2b2b), "拍照",
             lv_color_hex(0xffffff), cb_shutter, NULL);

    bool ok = camera_app_start(cam_img_);
    bool sd_ok = camera_app_sd_ready();
    lv_label_set_text(sd, sd_ok ? "TF OK" : "TF 未插");
    lv_obj_set_style_text_color(sd, sd_ok ? C_GREEN : C_RED, 0);
    if (!ok) {
        char msg[40];
        snprintf(msg, sizeof(msg), "相机失败:%s", camera_app_last_error());
        show_toast(msg);
    }
}

static void build_placeholder(const char *title, const char *text)
{
    make_obj(app_root_, 0, 0, kWidth, 28, C_PANEL, 0);
    make_btn(app_root_, 4, 2, 48, 24, lv_color_hex(0x1b3550), "返回", C_TEXT, cb_back, NULL);
    app_title_ = make_label(app_root_, title, C_TEXT, 0, 4, 120);
    lv_obj_set_style_text_align(app_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(app_title_, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *l = make_label(app_root_, text, C_DIM, 16, 80, 288);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

/* ---------------- App 切换 ------------- */

static void close_app(void)
{
    /* 相机任务必须先停，再删预览对象，否则任务会访问已删除的 LVGL 对象 */
    camera_app_stop();
    cam_img_ = NULL;
    music_app_close();

    if (snake_timer_) {
        lv_timer_del(snake_timer_);
        snake_timer_ = NULL;
    }
    if (app_root_) {
        lv_obj_del(app_root_);
        app_root_ = NULL;
    }
    for (int i = 0; i < 16; i++) g2048_cells_[i] = NULL;
    g2048_score_label_ = NULL;
    g2048_over_ = NULL;
    snake_grid_ = NULL;
    snake_food_ = NULL;
    snake_over_ = NULL;
    for (int i = 0; i < SDRAW; i++) snake_segs_[i] = NULL;
    note_ta_ = NULL;
    note_kb_ = NULL;
    note_ime_btn_ = NULL;
    note_comp_bar_ = NULL;
    note_comp_lbl_ = NULL;
    note_prev_btn_ = NULL;
    note_next_btn_ = NULL;
    note_cand_page_ = 0;
    note_cand_more_ = false;
    if (note_cands_) {
        heap_caps_free(note_cands_);
        note_cands_ = NULL;
    }
    for (int i = 0; i < NOTE_CANDS; i++) note_cand_btn_[i] = NULL;
    note_pinyin_[0] = 0;
    note_cand_cnt_ = 0;
    note_pinyin_id_++;
    app_title_ = NULL;
    if (launcher_) lv_obj_clear_flag(launcher_, LV_OBJ_FLAG_HIDDEN);
}

static void open_app(int app)
{
    close_app();
    if (launcher_) lv_obj_add_flag(launcher_, LV_OBJ_FLAG_HIDDEN);

    app_root_ = lv_obj_create(root_);
    lv_obj_remove_style_all(app_root_);
    lv_obj_set_pos(app_root_, 0, 0);
    lv_obj_set_size(app_root_, kWidth, kHeight);
    lv_obj_clear_flag(app_root_, LV_OBJ_FLAG_SCROLLABLE);

    switch (app) {
    case APP_GAMES: build_games_menu(); break;
    case APP_2048: build_2048(); break;
    case APP_SNAKE: build_snake(); break;
    case APP_NOTE: build_note(); break;
    case APP_SETTINGS: build_settings(); break;
    case APP_MUSIC: build_music(); break;
    case APP_CAMERA: build_camera(); break;
    default: break;
    }
}

/* ---------------- 时钟 / 状态 / 对外接口 ---------------- */

static void update_clock(void)
{
    if (!clock_label_) return;
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    if (tm_info.tm_year + 1900 < 2024) {
        lv_label_set_text(clock_label_, "--:--:--");
    } else {
        lv_label_set_text_fmt(clock_label_, "%02d:%02d:%02d",
                              tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    }
    if (standby_clock_) {
        if (tm_info.tm_year + 1900 < 2024) {
            lv_label_set_text(standby_clock_, "--:--");
        } else {
            lv_label_set_text_fmt(standby_clock_, "%02d:%02d",
                                  tm_info.tm_hour, tm_info.tm_min);
        }
    }
    if (wifi_label_) {
        const char *ssid = wifi_get_ssid();
        lv_label_set_text(wifi_label_, (ssid && ssid[0]) ? ssid : "配网");
    }
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    update_clock();
}

static bool desktop_should_show(void)
{
    if (ai_override_) return false;
    app_state_t st = app_get_state();
    return st == ST_IDLE || st == ST_ERROR;
}

static void state_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!root_) return;
    static bool hidden = true;

    if (!desktop_should_show()) {
        idle_ticks_ = 0;
        /* 语音交互时不要盖着待机图；启动/配网阶段保留待机图 */
        app_state_t st = app_get_state();
        if (st == ST_LISTENING || st == ST_THINKING || st == ST_SPEAKING) {
            hide_standby();
        }
        if (!hidden) {
            hidden = true;
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            if (snake_timer_) lv_timer_pause(snake_timer_);
        }
        return;
    }

    if (!hidden) {
        idle_ticks_ = 0;
        /* 桌面 Launcher 闲置 30 秒进入待机；打开应用时不打扰 */
        if (app_root_ == NULL && !standby_shown_ &&
            lv_disp_get_inactive_time(NULL) >= 30000) {
            show_standby();
        }
        return;
    }
    if (++idle_ticks_ >= 4) {
        idle_ticks_ = 0;
        hidden = false;
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        if (snake_timer_) lv_timer_resume(snake_timer_);
    }
}

void desktop_ui_init(void)
{
    if (!lvgl_port_lock(0)) return;

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, kWidth, kHeight);
    lv_obj_set_style_bg_color(root_, C_BG, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    build_launcher();
    build_standby();
    show_standby();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    clock_timer_ = lv_timer_create(clock_timer_cb, 1000, NULL);
    state_timer_ = lv_timer_create(state_timer_cb, 250, NULL);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "desktop ready");
}

void desktop_ui_enter_ai(void)
{
    ai_override_ = true;
    hide_standby();
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void desktop_ui_exit_ai(void)
{
    ai_override_ = false;
    if (root_ && desktop_should_show()) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void desktop_ui_toast(const char *text)
{
    if (!text) return;
    if (!lvgl_port_lock(0)) return;
    hide_standby();
    show_toast(text);
    lvgl_port_unlock();
}

void desktop_ui_toast_try(const char *text)
{
    if (!text) return;
    if (!lvgl_port_lock(20)) return;
    hide_standby();
    show_toast(text);
    lvgl_port_unlock();
}

void desktop_ui_open_app(const char *name)
{
    int app = APP_NONE;
    if (!name) return;
    if (!strcmp(name, "desktop")) app = APP_NONE;
    else if (!strcmp(name, "games")) app = APP_GAMES;
    else if (!strcmp(name, "2048")) app = APP_2048;
    else if (!strcmp(name, "snake")) app = APP_SNAKE;
    else if (!strcmp(name, "note")) app = APP_NOTE;
    else if (!strcmp(name, "settings")) app = APP_SETTINGS;
    else if (!strcmp(name, "music")) app = APP_MUSIC;
    else if (!strcmp(name, "camera")) app = APP_CAMERA;

    if (!lvgl_port_lock(0)) return;
    hide_standby();
    if (app == APP_NONE) {
        close_app();
        ai_override_ = false;
        if (root_ && desktop_should_show()) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    } else {
        ai_override_ = false;
        if (root_) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        open_app(app);
    }
    lvgl_port_unlock();
}
