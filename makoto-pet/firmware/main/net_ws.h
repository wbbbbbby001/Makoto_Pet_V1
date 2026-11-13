#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void net_ws_start(void);
bool net_ws_is_connected(void);

int net_ws_send_text(const char *json);
int net_ws_send_bin(const void *data, size_t len);
/* 把一段 16k 单声道 PCM 发给服务端（内部节流，不会阻塞太久） */
void net_ws_send_pcm(const int16_t *pcm, size_t samples);
/* 记事本拼音输入：把 pinyin 发给服务端换中文候选；offset 用于候选翻页 */
int net_ws_send_pinyin(unsigned id, const char *text, int offset);

/* 音乐控制：action = pause / resume / toggle / stop / next / prev */
int net_ws_send_music_cmd(const char *action);
/* 音乐控制：action = pause / resume / toggle / stop / next / prev */
int net_ws_send_music_cmd(const char *action);
/* 播放缓冲彻底播完（TTS/音乐结束）时通知服务端 */
int net_ws_send_audio_idle(void);
