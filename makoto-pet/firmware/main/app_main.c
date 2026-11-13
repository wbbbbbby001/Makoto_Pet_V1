#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_camera.h"
#include "nvs_flash.h"

#include "esp32_s3_szp.h"
#include "app.h"
#include "ui_app.h"
#include "desktop_ui.h"
#include "camera_app.h"
#include "music_app.h"
#include "audio_io.h"
#include "net_ws.h"
#include "wifi.h"

static const char *TAG = "app";

static volatile app_state_t s_state = ST_BOOT;

void app_set_state(app_state_t st)
{
    s_state = st;
    ui_update_state(st);
    ESP_LOGI(TAG, "state -> %d", (int)st);
}

app_state_t app_get_state(void)
{
    return s_state;
}

void app_ptt(bool down)
{
    if (down) {
        /* 音乐模式：不进入语音识别，长按 BOOT 只停音乐 */
        if (music_app_loaded()) {
            net_ws_send_music_cmd("stop");
            ui_set_hint("音乐模式：已停止音乐");
            return;
        }
        if (s_state == ST_SPEAKING) {
            /* 第一次按下：打断；再按一次才开始说话 */
            app_abort();
            return;
        }
        if (s_state != ST_IDLE && s_state != ST_ERROR) {
            return;
        }
        if (!net_ws_is_connected()) {
            ui_set_hint("语音中枢未连接，请检查服务端/网络");
            return;
        }

        net_ws_send_text("{\"type\":\"ptt_start\"}");
        audio_mic_start();
        app_set_state(ST_LISTENING);
    } else {
        if (s_state != ST_LISTENING) return;
        audio_mic_stop();
        net_ws_send_text("{\"type\":\"ptt_stop\"}");
        app_set_state(ST_THINKING);
    }
}

void app_abort(void)
{
    net_ws_send_text("{\"type\":\"abort\"}");
    audio_play_reset();
    audio_mic_stop();
    app_set_state(ST_IDLE);
    ui_set_hint("已打断");
}

void app_set_volume(int volume)
{
    audio_set_volume(volume);
}

int app_get_volume(void)
{
    return audio_get_volume();
}

#if CONFIG_MAKOTO_BOOT_PTT
#ifndef CONFIG_MAKOTO_BOOT_LONGPRESS_MS
#define CONFIG_MAKOTO_BOOT_LONGPRESS_MS 3000
#endif

static void boot_key_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    bool last = false;        /* 消抖后的按键状态 */
    bool pressed = false;
    bool long_fired = false;  /* 这次按住已经触发过语音对话 */
    int stable = 0;
    TickType_t press_tick = 0;

    for (;;) {
        bool cur = (gpio_get_level(GPIO_NUM_0) == 0); /* BOOT 低电平按下 */

        if (cur != last) {
            if (++stable >= 3) {   /* 60ms 消抖 */
                last = cur;
                stable = 0;

                if (cur) {
                    /* 按下：先不启动录音，等按住满 3 秒 */
                    pressed = true;
                    long_fired = false;
                    press_tick = xTaskGetTickCount();
                    ESP_LOGI(TAG, "BOOT down");
                } else {
                    /* 松开 */
                    pressed = false;
                    if (long_fired) {
                        app_ptt(false);          /* 结束本轮语音 */
                    } else if (camera_app_active()) {
                        camera_app_take_photo(); /* 相机界面：短按拍照 */
                    } else if (music_app_loaded()) {
                        music_app_toggle_pause(); /* 音乐模式：短按暂停/继续 */
                    }
                    long_fired = false;
                    ESP_LOGI(TAG, "BOOT up");
                }
            }
        } else {
            stable = 0;
        }

        /* 按住达到阈值：启动语音对话；之后继续按住说话，松开结束 */
        if (pressed && !long_fired &&
            (xTaskGetTickCount() - press_tick) >= pdMS_TO_TICKS(CONFIG_MAKOTO_BOOT_LONGPRESS_MS)) {
            long_fired = true;
            ESP_LOGI(TAG, "BOOT long press -> PTT");
            app_ptt(true);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

void app_main(void)
{
    /* 摄像头的内部 DMA 缓冲必须在 LVGL/Wi-Fi 把堆打碎之前预留，
     * 否则打开相机时找不到连续 8KB，只能退回 PSRAM DMA（RGB565 会花屏）。 */
    static uint8_t *cam_dma_pool = NULL;
    cam_dma_pool = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (cam_dma_pool) {
        cam_set_dma_buffer(cam_dma_pool, 8192);
        ESP_LOGI(TAG, "camera DMA buffer reserved: %p", (void *)cam_dma_pool);
    } else {
        ESP_LOGE(TAG, "camera DMA buffer reservation failed");
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 板级外设 */
    ESP_ERROR_CHECK(bsp_i2c_init());
    pca9557_init();
    bsp_lvgl_start();
    ui_init();
    desktop_ui_init();
    ESP_ERROR_CHECK(bsp_codec_init());

    audio_init();
    audio_set_mic_cb(net_ws_send_pcm);

    app_set_state(ST_BOOT);
    ui_set_hint("正在连接 Wi-Fi…");

    wifi_start();

    if (wifi_is_connected()) {
        ui_set_hint("正在连接语音中枢…");
        net_ws_start();
    }

#if CONFIG_MAKOTO_BOOT_PTT
    xTaskCreatePinnedToCore(boot_key_task, "bootkey", 3072, NULL, 4, NULL, 0);
#endif

    ESP_LOGI(TAG, "boot done");
}
