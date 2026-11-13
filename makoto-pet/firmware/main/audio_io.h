#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 麦克风采集回调：pcm 为 16kHz / 16bit / 单声道 */
typedef void (*audio_mic_cb_t)(const int16_t *pcm, size_t samples);

void audio_init(void);
void audio_set_mic_cb(audio_mic_cb_t cb);

void audio_mic_start(void);
void audio_mic_stop(void);
bool audio_mic_is_running(void);

/* 播放：服务端会先发 tts_start(采样率)，随后二进制 PCM16 单声道 */
void audio_play_begin(uint32_t sample_rate);
/* 音乐专用：先收满 prefill_ms 毫秒的 PCM 再开始出声 */
void audio_play_begin_buffered(uint32_t sample_rate, uint32_t prefill_ms);
void audio_play_chunk(const void *data, size_t len);
void audio_play_end(void);
void audio_play_reset(void);
/* 音乐暂停/继续：暂停时保留环形缓冲 */
void audio_play_pause(void);
void audio_play_resume(void);
bool audio_play_is_paused(void);

void audio_set_volume(int volume);
int  audio_get_volume(void);
