#pragma once

#include <stdbool.h>

/* 设备主状态机 */
typedef enum {
    ST_BOOT = 0,
    ST_IDLE,        /* 待机 */
    ST_LISTENING,   /* 正在录音上传 */
    ST_THINKING,    /* 等服务端 ASR/LLM */
    ST_SPEAKING,    /* 正在播放 TTS */
    ST_ERROR,       /* 网络/服务异常 */
} app_state_t;

void app_set_state(app_state_t st);
app_state_t app_get_state(void);

/* 按住说话：down=true 开始，false 结束（松开） */
void app_ptt(bool down);
/* 打断当前回复 */
void app_abort(void);

void app_set_volume(int volume);
int app_get_volume(void);
