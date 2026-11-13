#include "audio_io.h"

#include <math.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp32_s3_szp.h"
#include "app.h"
#include "ui_app.h"

static const char *TAG = "audio";

#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_SAMPLES   320          /* 20ms */
#define ADC_CHANNELS        4            /* ES7210: 4 个 I2S 槽 */
#define PLAY_RING_SIZE      (512 * 1024) /* PSRAM 环形缓冲，约 8 秒 32k PCM16 */

static TaskHandle_t s_mic_task = NULL;
static TaskHandle_t s_play_task = NULL;
static volatile bool s_mic_running = false;
static audio_mic_cb_t s_mic_cb = NULL;

static RingbufHandle_t s_ring = NULL;
static volatile uint32_t s_play_rate = 0;
static volatile bool s_play_active = false;
static volatile bool s_play_end = false;
static volatile bool s_play_paused = false;
static volatile uint32_t s_play_prefill_bytes = 0;
static volatile uint32_t s_play_starve = 0;
static volatile uint32_t s_play_bytes_recv = 0;
static TickType_t s_play_start_tick = 0;
static int s_volume = 70;
static int32_t *s_stereo_buf = NULL;
#define STEREO_BUF_BYTES (4096 * 2 * sizeof(int32_t))

/* ---------------- 麦克风 ---------------- */

static void mic_task(void *arg)
{
    const size_t read_bytes = MIC_FRAME_SAMPLES * ADC_CHANNELS * sizeof(int16_t);
    int16_t *raw = heap_caps_malloc(read_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *mono = heap_caps_malloc(MIC_FRAME_SAMPLES * sizeof(int16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw || !mono) {
        ESP_LOGE(TAG, "mic buffer alloc failed");
        s_mic_running = false;
        vTaskDelete(NULL);
        return;
    }

    int ui_tick = 0;
    while (s_mic_running) {
        if (bsp_get_feed_data(false, raw, read_bytes) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int64_t sum = 0;
        for (int i = 0; i < MIC_FRAME_SAMPLES; i++) {
            /* 去交织后 3 通道布局：0=MIC1, 1=MIC2, 2=参考 */
            int16_t v = raw[3 * i];
            mono[i] = v;
            sum += (int32_t)v * (int32_t)v;
        }

        if (s_mic_cb) {
            s_mic_cb(mono, MIC_FRAME_SAMPLES);
        }

        /* 每 100ms 更新一次电平条 */
        if (++ui_tick >= 5) {
            ui_tick = 0;
            float rms = sqrtf((float)sum / MIC_FRAME_SAMPLES);
            int level = 0;
            if (rms > 1.0f) {
                level = (int)(20.0f * log10f(rms / 32768.0f) + 60.0f);
            }
            if (level < 0) level = 0;
            if (level > 100) level = 100;
            ui_set_mic_level(level);
        }
    }

    heap_caps_free(raw);
    heap_caps_free(mono);
    s_mic_task = NULL;
    vTaskDelete(NULL);
}

void audio_mic_start(void)
{
    if (s_mic_running) return;

    audio_play_reset();
    bsp_play_close();
    bsp_mic_close();
    esp_err_t ret = bsp_mic_open(MIC_SAMPLE_RATE, 32, I2S_SLOT_MODE_STEREO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mic open failed: %s", esp_err_to_name(ret));
        app_set_state(ST_ERROR);
        return;
    }
    ESP_LOGI(TAG, "mic open @%dHz", MIC_SAMPLE_RATE);

    s_mic_running = true;
    xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 6, &s_mic_task, 0);
}

void audio_mic_stop(void)
{
    if (!s_mic_running) return;
    s_mic_running = false;
    /* 等采集任务退出 */
    for (int i = 0; i < 30 && s_mic_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    bsp_mic_close();
    ui_set_mic_level(0);
}

bool audio_mic_is_running(void)
{
    return s_mic_running;
}

void audio_set_mic_cb(audio_mic_cb_t cb)
{
    s_mic_cb = cb;
}

/* ---------------- 播放 ---------------- */

static void play_task(void *arg)
{
    size_t len = 0;
    void *item = NULL;

    while (1) {
        if (s_play_paused) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        /* 音乐预缓冲：收够 prefill 字节再开始消费；最长等 6 秒 */
        if (s_play_prefill_bytes && s_play_bytes_recv < s_play_prefill_bytes) {
            if (!s_play_end &&
                (xTaskGetTickCount() - s_play_start_tick) < pdMS_TO_TICKS(6000)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            s_play_prefill_bytes = 0;
        }
        item = xRingbufferReceiveUpTo(s_ring, &len, pdMS_TO_TICKS(50), 4096);
        if (!item) {
            if (s_play_active && !s_play_paused && !s_play_end) {
                s_play_starve++;
                if ((s_play_starve % 10) == 1) {
                    ESP_LOGW(TAG, "audio starve (ring empty) x%" PRIu32, s_play_starve);
                }
            }
            continue;
        }
        if (item) {
            if (s_play_active) {
                /* int16 单声道 -> int32 立体声：ES8311 在这块板上按 32-bit I2S 工作 */
                size_t samples = len / sizeof(int16_t);
                if (s_stereo_buf && samples * 2 * sizeof(int32_t) <= STEREO_BUF_BYTES) {
                    const int16_t *src = (const int16_t *)item;
                    for (size_t i = 0; i < samples; i++) {
                        int32_t v = ((int32_t)src[i]) << 16;
                        s_stereo_buf[2 * i] = v;
                        s_stereo_buf[2 * i + 1] = v;
                    }
                    bsp_play_write(s_stereo_buf, samples * 2 * sizeof(int32_t));
                }
            }
            vRingbufferReturnItem(s_ring, item);
            continue;
        }

        if (s_play_end) {
            s_play_end = false;
            s_play_active = false;
            bsp_play_close();
            ESP_LOGI(TAG, "playback done");
            app_set_state(ST_IDLE);
        }
    }
}

static void ring_drain(void)
{
    if (!s_ring) return;
    size_t len = 0;
    void *item = NULL;
    while ((item = xRingbufferReceiveUpTo(s_ring, &len, 0, 4096)) != NULL) {
        vRingbufferReturnItem(s_ring, item);
    }
}

static void play_begin_internal(uint32_t sample_rate, uint32_t prefill_ms)
{
    if (sample_rate < 8000 || sample_rate > 48000) sample_rate = 32000;

    audio_mic_stop();
    ring_drain();
    s_play_end = false;
    s_play_paused = false;
    s_play_bytes_recv = 0;
    s_play_start_tick = xTaskGetTickCount();
    s_play_prefill_bytes = (uint32_t)((uint64_t)sample_rate * 2 * prefill_ms / 1000);
    s_play_starve = 0;

    if (s_play_active && s_play_rate == sample_rate) {
        return;
    }

    bsp_mic_close();
    bsp_play_close();
    esp_err_t ret = bsp_play_open(sample_rate, 32, I2S_SLOT_MODE_STEREO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "speaker open failed: %s", esp_err_to_name(ret));
        return;
    }
    s_play_rate = sample_rate;
    s_play_active = true;
    /* 重新打开后把音量再设一次，避免 codec 复位后回到默认/静音 */
    int got = 0;
    bsp_codec_volume_set(audio_get_volume(), &got);
    ESP_LOGI(TAG, "speaker open @%" PRIu32 "Hz, vol=%d", sample_rate, got);
}

void audio_play_begin(uint32_t sample_rate)
{
    play_begin_internal(sample_rate, 0);
}

void audio_play_begin_buffered(uint32_t sample_rate, uint32_t prefill_ms)
{
    play_begin_internal(sample_rate, prefill_ms);
}

void audio_play_chunk(const void *data, size_t len)
{
    if (!data || !len) return;
    if (!s_play_active) {
        /* 没收到 tts_start 的兜底 */
        audio_play_begin(32000);
    }
    s_play_bytes_recv += (uint32_t)len;
    if (xRingbufferSend(s_ring, data, len, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "play ring full, drop %u bytes", (unsigned)len);
    }
}

void audio_play_end(void)
{
    if (!s_play_active) {
        app_set_state(ST_IDLE);
        return;
    }
    s_play_end = true;
}

void audio_play_reset(void)
{
    s_play_end = false;
    s_play_active = false;
    s_play_paused = false;
    s_play_prefill_bytes = 0;
    s_play_bytes_recv = 0;
    ring_drain();
}

void audio_play_pause(void) { s_play_paused = true; }
void audio_play_resume(void) { s_play_paused = false; }
bool audio_play_is_paused(void) { return s_play_paused; }

void audio_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    int got = 0;
    bsp_codec_volume_set(volume, &got);
}

int audio_get_volume(void)
{
    return s_volume;
}

void audio_init(void)
{
    s_ring = xRingbufferCreateWithCaps(PLAY_RING_SIZE, RINGBUF_TYPE_BYTEBUF,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
        ESP_LOGE(TAG, "ring buffer create failed, fallback to internal ram");
        s_ring = xRingbufferCreate(PLAY_RING_SIZE, RINGBUF_TYPE_BYTEBUF);
    }
    configASSERT(s_ring);

    s_stereo_buf = heap_caps_malloc(STEREO_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_stereo_buf) {
        ESP_LOGE(TAG, "stereo buffer alloc failed");
    }

    xTaskCreatePinnedToCore(play_task, "play", 4096, NULL, 6, &s_play_task, 1);
    audio_set_volume(CONFIG_MAKOTO_VOLUME);
}
