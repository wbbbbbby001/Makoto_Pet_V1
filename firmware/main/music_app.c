#include "music_app.h"

#include <stdio.h>
#include <string.h>

#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "net_ws.h"

LV_FONT_DECLARE(font_cn_16);
static const lv_font_t *F = &font_cn_16;

static lv_obj_t *title_lbl_;
static lv_obj_t *artist_lbl_;

static char s_title[96];
static char s_artist[64];
static bool s_loaded;
static bool s_playing;
static bool s_paused;

static void render(void)
{
    if (!title_lbl_) return;
    lv_label_set_text(title_lbl_, s_title[0] ? s_title : "未播放");
    lv_label_set_text(artist_lbl_, s_artist[0] ? s_artist : "");
}

static void action_cb(lv_event_t *e)
{
    const char *action = (const char *)lv_event_get_user_data(e);
    if (action) net_ws_send_music_cmd(action);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            int w, int h, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, F, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1b3550), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, F, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xf2f7ff), 0);
    lv_obj_center(l);
    return b;
}

void music_app_build(lv_obj_t *parent)
{
    /* 中间只显示歌名和作者 */
    title_lbl_ = make_label(parent, "未播放", 10, 82, 300, 30,
                            lv_color_hex(0xf2f7ff));
    lv_label_set_long_mode(title_lbl_, LV_LABEL_LONG_SCROLL_CIRCULAR);

    artist_lbl_ = make_label(parent, "", 10, 122, 300, 22,
                             lv_color_hex(0x9fc4e8));

    /* 四个按键：上一首 / 暂停 / 播放 / 下一首 */
    make_btn(parent, 8, 176, 70, 40, "上一首", action_cb, (void *)"prev");
    make_btn(parent, 84, 176, 70, 40, "暂停", action_cb, (void *)"pause");
    make_btn(parent, 160, 176, 70, 40, "播放", action_cb, (void *)"resume");
    make_btn(parent, 236, 176, 70, 40, "下一首", action_cb, (void *)"next");

    render();
}

void music_app_close(void)
{
    title_lbl_ = NULL;
    artist_lbl_ = NULL;
}

void music_app_on_start(const char *title, const char *artist,
                        const char *source, const char *license,
                        double duration, double position,
                        int index, int total)
{
    (void)source;
    (void)license;
    (void)duration;
    (void)position;
    (void)index;
    (void)total;
    snprintf(s_title, sizeof(s_title), "%s", title ? title : "");
    snprintf(s_artist, sizeof(s_artist), "%s", artist ? artist : "");
    s_loaded = true;
    s_playing = true;
    s_paused = false;
    if (lvgl_port_lock(0)) {
        render();
        lvgl_port_unlock();
    }
}

void music_app_on_state(const char *state)
{
    if (!state) return;
    if (!strcmp(state, "buffering")) {
        s_loaded = true;
        s_playing = true;
        s_paused = false;
    } else if (!strcmp(state, "paused")) {
        s_loaded = true;
        s_playing = true;
        s_paused = true;
    } else if (!strcmp(state, "playing")) {
        s_loaded = true;
        s_playing = true;
        s_paused = false;
    } else if (!strcmp(state, "stopped")) {
        s_loaded = false;
        s_playing = false;
        s_paused = false;
    }
}

void music_app_on_progress(double position, double duration)
{
    (void)position;
    (void)duration;
}

void music_app_on_end(const char *error)
{
    (void)error;
    s_loaded = false;
    s_playing = false;
    s_paused = false;
}

bool music_app_loaded(void)
{
    return s_loaded;
}

bool music_app_playing(void)
{
    return s_loaded && s_playing && !s_paused;
}

void music_app_toggle_pause(void)
{
    if (!s_loaded) return;
    net_ws_send_music_cmd(s_paused ? "resume" : "pause");
}
