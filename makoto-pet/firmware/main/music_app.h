#pragma once

#include <stdbool.h>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

/* 音乐 App（v1）：界面 + 播放状态。
 * 音频走 net_ws 的二进制 PCM 通道，由服务端解码后推送。
 */
void music_app_build(lv_obj_t *parent);
void music_app_close(void);

void music_app_on_start(const char *title, const char *artist,
                        const char *source, const char *license,
                        double duration, double position,
                        int index, int total);
void music_app_on_state(const char *state);
void music_app_on_progress(double position, double duration);
void music_app_on_end(const char *error);

/* 有歌处于播放/暂停状态（音乐模式） */
bool music_app_loaded(void);
bool music_app_playing(void);
void music_app_toggle_pause(void);
