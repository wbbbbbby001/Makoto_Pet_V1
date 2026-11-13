#include "camera_app.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "img_converters.h"

#include "esp32_s3_szp.h"
#include "desktop_ui.h"
#include "sd_card.h"

static const char *TAG = "camera";

/* components/esp32-camera 的本地补丁：SCCB 使用 BSP 已经建好的新版 I2C 总线 */
extern i2c_master_bus_handle_t g_sccb_bus_handle;

/* 屏幕预览尺寸（设备是 320×240） */
#define PREVIEW_W 320
#define PREVIEW_H 240
#define PREVIEW_INTERVAL_MS 1

/* 拍照使用 VGA，预览在内存里做 2× 降采样，避免运行中改 frame size */
#define CAPTURE_FRAME_SIZE FRAMESIZE_VGA
#define CAPTURE_JPEG_QUALITY 85

#define PHOTO_DIR "/sdcard/DCIM"

static volatile bool s_active = false;
static volatile bool s_run = false;
static volatile int s_task_count = 0;
static bool s_cam_inited = false;

static lv_obj_t *s_img = NULL;
static uint8_t *s_pv_buf = NULL;
static uint8_t *s_shot_buf = NULL;           /* 拍照挑最清晰帧用 */
static size_t s_shot_buf_size = 0;
static volatile int s_photo_mode = CAM_PHOTO_CLEAR;
static camera_config_t s_cam_cfg;            /* 当前相机配置，供高清模式重配 */
static bool s_cam_cfg_valid = false;

static esp_err_t camera_apply_config(framesize_t fs, int fb_count);
static lv_img_dsc_t s_dsc;
static bool s_src_set = false;

static SemaphoreHandle_t s_cam_lock = NULL;  /* 保护 esp_camera_fb_get/return */
static SemaphoreHandle_t s_shot_sem = NULL;  /* 拍照请求 */
static SemaphoreHandle_t s_init_sem = NULL;  /* 相机初始化完成 */
static volatile bool s_init_ok = false;
static bool s_preview_psram = false;         /* 预览任务栈是否在 PSRAM */
static framesize_t s_framesize = FRAMESIZE_VGA;
static char s_last_err[24] = "";             /* 给界面/网页看的失败原因 */

/* ---------------- 文件名 ---------------- */

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static const char *photo_dir(void)
{
    if (mkdir(PHOTO_DIR, 0777) == 0 || errno == EEXIST) return PHOTO_DIR;
    /* 有些卡根目录可写但子目录建不了，退回根目录 */
    return sd_card_mount_point();
}

/* 找到下一个没被占用的 IMG_xxxx 编号（8.3 文件名，兼容 FATFS LFN=none） */
static int next_photo_index(const char *dir)
{
    char path[80];
    for (int i = 1; i <= 9999; i++) {
        snprintf(path, sizeof(path), "%s/IMG_%04d.JPG", dir, i);
        if (file_exists(path)) continue;
        snprintf(path, sizeof(path), "%s/IMG_%04d.BMP", dir, i);
        if (file_exists(path)) continue;
        return i;
    }
    return -1;
}

static bool write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "open %s failed (%d)", path, errno);
        return false;
    }
    size_t n = fwrite(data, 1, len, f);
    fclose(f);
    if (n != len) {
        ESP_LOGE(TAG, "write %s failed (%u/%u)", path, (unsigned)n, (unsigned)len);
        remove(path);
        return false;
    }
    ESP_LOGI(TAG, "photo saved: %s (%u bytes)", path, (unsigned)len);
    return true;
}

/* ---------------- BMP 兜底（不依赖 JPEG 编码器） ---------------- */

static void put16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static void put32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

/* 相机 RGB565 在内存里是高字节在前（和 ST7789 一致），这里按字节读取 */
static bool write_bmp(const camera_fb_t *fb, const char *path)
{
    const int w = (int)fb->width;
    const int h = (int)fb->height;
    if (w <= 0 || h <= 0 || !fb->buf) return false;

    const int row_bytes = w * 3;
    const int pad = (4 - (row_bytes & 3)) & 3;
    const uint32_t image_size = (uint32_t)(row_bytes + pad) * (uint32_t)h;
    const uint32_t file_size = 54 + image_size;

    uint8_t *row = (uint8_t *)malloc(row_bytes + pad);
    if (!row) return false;

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(row);
        ESP_LOGE(TAG, "open %s failed (%d)", path, errno);
        return false;
    }

    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    put32le(hdr + 2, file_size);
    put32le(hdr + 10, 54);
    put32le(hdr + 14, 40);
    put32le(hdr + 18, (uint32_t)w);
    put32le(hdr + 22, (uint32_t)h);
    put16le(hdr + 26, 1);
    put16le(hdr + 28, 24);
    put32le(hdr + 34, image_size);

    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);

    for (int y = h - 1; ok && y >= 0; y--) {
        const uint8_t *src = fb->buf + (size_t)y * (size_t)w * 2;
        for (int x = 0; x < w; x++) {
            uint8_t hi = src[x * 2];
            uint8_t lo = src[x * 2 + 1];
            uint16_t px = ((uint16_t)hi << 8) | lo;
            uint8_t r5 = (px >> 11) & 0x1f;
            uint8_t g6 = (px >> 5) & 0x3f;
            uint8_t b5 = px & 0x1f;
            row[x * 3 + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
            row[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            row[x * 3 + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
        }
        if (pad) memset(row + row_bytes, 0, pad);
        if (fwrite(row, 1, row_bytes + pad, f) != (size_t)(row_bytes + pad)) ok = false;
    }

    free(row);
    fclose(f);

    if (!ok) {
        ESP_LOGE(TAG, "write %s failed", path);
        remove(path);
        return false;
    }
    ESP_LOGI(TAG, "photo saved: %s (%u bytes BMP)", path, (unsigned)file_size);
    return true;
}

/* ---------------- 预览 ---------------- */

static void downsample_to_preview(const camera_fb_t *fb, uint8_t *dst)
{
    const int sw = (int)fb->width;
    const int sh = (int)fb->height;
    const uint8_t *src = fb->buf;
    uint8_t *out = dst;

    /* 常规 DMA 模式下帧数据是完整连续的 RGB565；QVGA 直接整帧拷贝，
     * 不再做逐行相位检测/去噪，减少每帧 CPU 占用。 */
    if (sw == PREVIEW_W && sh == PREVIEW_H) {
        memcpy(out, src, (size_t)PREVIEW_W * PREVIEW_H * 2);
        return;
    }

    for (int y = 0; y < PREVIEW_H; y++) {
        int sy = y * sh / PREVIEW_H;
        const uint8_t *srow = src + (size_t)sy * (size_t)sw * 2;
        uint8_t *drow = out + (size_t)y * PREVIEW_W * 2;
        for (int x = 0; x < PREVIEW_W; x++) {
            int sx = x * sw / PREVIEW_W;
            drow[x * 2] = srow[sx * 2];
            drow[x * 2 + 1] = srow[sx * 2 + 1];
        }
    }
}

static void preview_task(void *arg)
{
    (void)arg;
    uint32_t frames = 0;
    uint32_t misses = 0;
    while (s_run) {
        bool updated = false;
        if (xSemaphoreTake(s_cam_lock, pdMS_TO_TICKS(120)) == pdTRUE) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb && fb->format == PIXFORMAT_RGB565 && s_pv_buf) {
                downsample_to_preview(fb, s_pv_buf);
                updated = true;
                misses = 0;
                if (frames++ == 0) {
                    ESP_LOGI(TAG, "preview first frame: %ux%u", (unsigned)fb->width,
                             (unsigned)fb->height);
                }
            } else if (!fb && ++misses == 5) {
                ESP_LOGW(TAG, "no camera frame after %u timeouts", (unsigned)misses);
            }
            if (fb) esp_camera_fb_return(fb);
            xSemaphoreGive(s_cam_lock);
        }

        if (updated && lvgl_port_lock(10)) {
            if (s_img) {
                if (!s_src_set) {
                    s_dsc.data = s_pv_buf;
                    lv_img_set_src(s_img, &s_dsc);
                    s_src_set = true;
                } else {
                    lv_obj_invalidate(s_img);
                }
            }
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(PREVIEW_INTERVAL_MS));
    }
    s_task_count--;
    if (s_preview_psram) vTaskDeleteWithCaps(NULL);
    else vTaskDelete(NULL);
}

/* ---------------- 拍照 ---------------- */

static bool save_photo(const camera_fb_t *fb, char *saved_name, size_t name_size)
{
    const char *dir = photo_dir();
    int idx = next_photo_index(dir);
    if (idx < 0) {
        ESP_LOGE(TAG, "no free photo index");
        return false;
    }

    char path[96];

    if (fb->format == PIXFORMAT_RGB565 && fb->buf && fb->len) {
        uint8_t *jpg = NULL;
        size_t jpg_len = 0;
        if (fmt2jpg(fb->buf, fb->len, fb->width, fb->height, fb->format,
                    CAPTURE_JPEG_QUALITY, &jpg, &jpg_len) && jpg && jpg_len) {
            snprintf(path, sizeof(path), "%s/IMG_%04d.JPG", dir, idx);
            bool ok = write_file(path, jpg, jpg_len);
            free(jpg);
            if (ok) {
                const char *base = strrchr(path, '/');
                snprintf(saved_name, name_size, "%s", base ? base + 1 : path);
                return true;
            }
        } else {
            ESP_LOGW(TAG, "JPEG encode failed, fallback to BMP");
        }
    }

    snprintf(path, sizeof(path), "%s/IMG_%04d.BMP", dir, idx);
    if (!write_bmp(fb, path)) return false;
    const char *base = strrchr(path, '/');
    snprintf(saved_name, name_size, "%s", base ? base + 1 : path);
    return true;
}

/* 用绿色通道的相邻像素梯度估清晰度：手持微晃时挑最清晰的一帧 */
static uint32_t frame_sharpness(const camera_fb_t *fb)
{
    const int w = (int)fb->width;
    const int h = (int)fb->height;
    if (!fb->buf || w < 2) return 0;
    uint32_t score = 0;
    for (int y = 1; y < h; y += 16) {
        const uint8_t *row = fb->buf + (size_t)y * (size_t)w * 2;
        for (int x = 1; x < w; x++) {
            uint16_t a = ((uint16_t)row[(x - 1) * 2] << 8) | row[(x - 1) * 2 + 1];
            uint16_t b = ((uint16_t)row[x * 2] << 8) | row[x * 2 + 1];
            score += (uint32_t)abs((int)((a >> 5) & 63) - (int)((b >> 5) & 63));
        }
    }
    return score;
}

static void capture_task(void *arg)
{
    (void)arg;
    while (s_run) {
        if (xSemaphoreTake(s_shot_sem, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (!s_active) continue;

        if (!sd_card_is_mounted()) {
            if (sd_card_mount() != ESP_OK) {
                desktop_ui_toast_try("TF 卡未插入");
                continue;
            }
        }

        desktop_ui_toast_try("拍照中...");

        camera_fb_t *fb = NULL;
        char name[32] = "";

        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        bool ok = false;

        sensor_t *sensor = esp_camera_sensor_get();
        bool gc = sensor && sensor->id.PID == GC2145_PID;
        int mode = s_photo_mode;

        if (gc && mode == CAM_PHOTO_HD) {
            /* 高清 2MP：重配为 1600x1200、单帧缓冲，自拍延时 3 秒，
             * 拍完恢复 VGA 预览。手持不可能完全不抖，必须放稳。 */
            gc2145_set_capture_mode(false);
            esp_err_t rerr = camera_apply_config(FRAMESIZE_UXGA, 1);
            if (rerr != ESP_OK) {
                ESP_LOGE(TAG, "HD reconfigure failed: %s", esp_err_to_name(rerr));
                camera_apply_config(FRAMESIZE_VGA, 3);
                desktop_ui_toast_try("高清模式初始化失败");
            } else {
                desktop_ui_toast_try("高清 2MP：请放稳，3 秒后拍");
                for (int i = 0; i < 30 && s_run; i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (s_run) {
                    fb = esp_camera_fb_get();
                    ok = (fb != NULL) && save_photo(fb, name, sizeof(name));
                    if (fb) esp_camera_fb_return(fb);
                }
                camera_apply_config(FRAMESIZE_VGA, 3);
            }
        } else {
            /* 广角模式：临时切 Subsample（1280x960 视野），拍完切回 Windowing。 */
            if (gc && mode == CAM_PHOTO_WIDE) {
                gc2145_set_capture_mode(true);
                sensor->set_framesize(sensor, s_framesize);
                vTaskDelay(pdMS_TO_TICKS(1500));
                for (int i = 0; i < 12; i++) {
                    camera_fb_t *f = esp_camera_fb_get();
                    if (f) esp_camera_fb_return(f);
                }
            }

            /* 连拍 3 帧，挑梯度最大（最清晰）的一帧，降低手持微晃造成的模糊 */
            if (s_shot_buf && s_shot_buf_size) {
                uint32_t best_score = 0;
                size_t best_len = 0;
                int best_w = 0, best_h = 0;
                pixformat_t best_fmt = PIXFORMAT_RGB565;
                for (int i = 0; i < 3; i++) {
                    fb = esp_camera_fb_get();
                    if (!fb) continue;
                    if (fb->len <= s_shot_buf_size) {
                        uint32_t sc = frame_sharpness(fb);
                        if (sc > best_score) {
                            memcpy(s_shot_buf, fb->buf, fb->len);
                            best_score = sc;
                            best_len = fb->len;
                            best_w = fb->width;
                            best_h = fb->height;
                            best_fmt = fb->format;
                        }
                    }
                    esp_camera_fb_return(fb);
                }
                if (best_len) {
                    camera_fb_t cf = {0};
                    cf.buf = s_shot_buf;
                    cf.len = best_len;
                    cf.width = best_w;
                    cf.height = best_h;
                    cf.format = best_fmt;
                    ok = save_photo(&cf, name, sizeof(name));
                }
            }

            if (!ok) {
                fb = esp_camera_fb_get();
                ok = (fb != NULL) && save_photo(fb, name, sizeof(name));
                if (fb) esp_camera_fb_return(fb);
            }

            if (gc && mode == CAM_PHOTO_WIDE) {
                gc2145_set_capture_mode(false);
                sensor->set_framesize(sensor, s_framesize);
                vTaskDelay(pdMS_TO_TICKS(150));
                camera_fb_t *f = esp_camera_fb_get();
                if (f) esp_camera_fb_return(f);
            }
        }
        xSemaphoreGive(s_cam_lock);

        if (ok) {
            char msg[48];
            snprintf(msg, sizeof(msg), "已存 %s", name);
            desktop_ui_toast_try(msg);
        } else {
            desktop_ui_toast_try("拍照失败");
        }
    }
    s_task_count--;
    /* 这个任务的栈是 xTaskCreatePinnedToCoreWithCaps 分配的 PSRAM，
     * 必须用 vTaskDeleteWithCaps 释放。 */
    vTaskDeleteWithCaps(NULL);
}

/* ---------------- 对外接口 ---------------- */

bool camera_app_active(void)
{
    return s_active;
}

bool camera_app_sd_ready(void)
{
    return sd_card_is_mounted();
}

/* SD 挂载 + 摄像头初始化比较吃栈，放到独立任务里做，
 * 避免在 LVGL 任务(4KB)或 WebSocket 任务里直接调用导致栈溢出。 */
static esp_err_t camera_try_init(framesize_t fs)
{
    dvp_pwdn(0); /* 摄像头供电 */

    camera_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ledc_channel = LEDC_CHANNEL_1;
    cfg.ledc_timer = LEDC_TIMER_1;
    cfg.pin_pwdn = CAMERA_PIN_PWDN;
    cfg.pin_reset = CAMERA_PIN_RESET;
    cfg.pin_xclk = CAMERA_PIN_XCLK;
    cfg.pin_sccb_sda = -1; /* 复用已经初始化的 I2C0（触摸/音频共用） */
    cfg.pin_sccb_scl = CAMERA_PIN_SIOC;
    cfg.sccb_i2c_port = 0;
    cfg.pin_d0 = CAMERA_PIN_D0;
    cfg.pin_d1 = CAMERA_PIN_D1;
    cfg.pin_d2 = CAMERA_PIN_D2;
    cfg.pin_d3 = CAMERA_PIN_D3;
    cfg.pin_d4 = CAMERA_PIN_D4;
    cfg.pin_d5 = CAMERA_PIN_D5;
    cfg.pin_d6 = CAMERA_PIN_D6;
    cfg.pin_d7 = CAMERA_PIN_D7;
    cfg.pin_vsync = CAMERA_PIN_VSYNC;
    cfg.pin_href = CAMERA_PIN_HREF;
    cfg.pin_pclk = CAMERA_PIN_PCLK;
    cfg.xclk_freq_hz = 24000000;
    cfg.pixel_format = PIXFORMAT_RGB565;
    cfg.frame_size = fs;
    cfg.jpeg_quality = 12;
    cfg.fb_count = 3;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed at %s: %s",
                 fs == FRAMESIZE_VGA ? "VGA" : "QVGA", esp_err_to_name(err));
        dvp_pwdn(1);
        return err;
    }
    s_cam_cfg = cfg;
    s_cam_cfg_valid = true;

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "sensor PID=0x%04x", s->id.PID);
        if (s->id.PID == GC0308_PID) {
            s->set_hmirror(s, 1);
        }
    }
    return ESP_OK;
}

/* 运行时重配相机（高清 2MP / 恢复 VGA 预览） */
static esp_err_t camera_apply_config(framesize_t fs, int fb_count)
{
    if (!s_cam_cfg_valid) return ESP_ERR_INVALID_STATE;
    camera_config_t cfg = s_cam_cfg;
    cfg.frame_size = fs;
    cfg.fb_count = fb_count;
    esp_err_t err = esp_camera_reconfigure(&cfg);
    if (err == ESP_OK) {
        s_cam_cfg = cfg;
        s_framesize = fs;
    }
    return err;
}

static bool camera_hw_init(void)
{
    /* TF 卡不一定插着，挂载失败不影响预览，只影响拍照 */
    if (!sd_card_is_mounted()) sd_card_mount();
    /* 趁相机还没分配缓冲、内部 DMA 堆还没碎片化时把 DCIM 建好，
     * 否则拍照时 mkdir 可能因 FATFS 拿不到 DMA 缓冲而失败。 */
    if (sd_card_is_mounted()) {
        const char *dir = photo_dir();
        ESP_LOGI(TAG, "photo dir: %s", dir);
    }

    if (!s_cam_inited) {
        /* 摄像头与触摸/音频共用 I2C0：把 BSP 的总线句柄交给 SCCB */
        g_sccb_bus_handle = bsp_i2c_get_bus();
        if (!g_sccb_bus_handle) {
            ESP_LOGE(TAG, "I2C bus is not ready");
            snprintf(s_last_err, sizeof(s_last_err), "I2C");
            return false;
        }

        /* QVGA (320x240) 直接用，不再传 VGA 再缩：
         * 1) PSRAM DMA 带宽降到 1/4，避免 RGB565 帧在传输中丢字节（横向彩色噪点带）；
         * 2) 屏幕就是 320x240，预览不需要缩放；
         * 3) VGA 版本在这块板子上实测会出现部分行错位。 */
        /* 预览用 VGA Windowing：约 28fps，曝光短，手持拍照更清晰。
         * 广角照片由拍照时临时切 Subsample 实现（相机界面可切换）。 */
        framesize_t fs = FRAMESIZE_VGA;
        esp_err_t err = camera_try_init(fs);
        if (err != ESP_OK) {
            snprintf(s_last_err, sizeof(s_last_err), "摄像头");
            return false;
        }
        s_cam_inited = true;
        s_framesize = fs;
        ESP_LOGI(TAG, "camera ready (%s, preview %dx%d RGB565)",
                 fs == FRAMESIZE_VGA ? "VGA" : "QVGA", PREVIEW_W, PREVIEW_H);
    }
    return true;
}

static void camera_init_task(void *arg)
{
    (void)arg;
    s_init_ok = camera_hw_init();
    xSemaphoreGive(s_init_sem);
    /* 这个任务的栈由 xTaskCreatePinnedToCoreWithCaps 分配，必须用 WithCaps 删除 */
    vTaskDeleteWithCaps(NULL);
}

bool camera_app_start(lv_obj_t *preview_img)
{
    if (s_active) return true;
    s_last_err[0] = 0;

    /* 上一次 stop 如果因为写卡/编码超时，先等旧任务彻底退出，避免重复任务 */
    for (int i = 0; i < 400 && s_task_count > 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_task_count > 0) {
        ESP_LOGE(TAG, "previous camera tasks still running (%d)", s_task_count);
        snprintf(s_last_err, sizeof(s_last_err), "旧任务");
        return false;
    }
    if (!preview_img) {
        snprintf(s_last_err, sizeof(s_last_err), "预览");
        return false;
    }

    s_img = preview_img;
    s_src_set = false;

    /* 预览缓冲放 PSRAM（320×240×2 = 150 KB） */
    if (!s_pv_buf) {
        s_pv_buf = (uint8_t *)heap_caps_malloc(PREVIEW_W * PREVIEW_H * 2,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pv_buf) {
            s_pv_buf = (uint8_t *)heap_caps_malloc(PREVIEW_W * PREVIEW_H * 2,
                                                   MALLOC_CAP_8BIT);
        }
        if (!s_pv_buf) {
            ESP_LOGE(TAG, "no memory for preview buffer");
            snprintf(s_last_err, sizeof(s_last_err), "内存");
            s_img = NULL;
            return false;
        }
        memset(&s_dsc, 0, sizeof(s_dsc));
        s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_dsc.header.w = PREVIEW_W;
        s_dsc.header.h = PREVIEW_H;
        s_dsc.data_size = PREVIEW_W * PREVIEW_H * 2;
        s_dsc.data = s_pv_buf;
    }

    if (!s_init_sem) s_init_sem = xSemaphoreCreateBinary();
    if (!s_init_sem) {
        ESP_LOGE(TAG, "init semaphore create failed");
        snprintf(s_last_err, sizeof(s_last_err), "内存");
        s_img = NULL;
        return false;
    }
    s_init_ok = false;
    xSemaphoreTake(s_init_sem, 0); /* 清掉超时遗留的旧信号 */

    /* 相机初始化任务（SD/FATFS + esp_camera_init）很吃栈，
     * 放 PSRAM，避免内部 RAM 碎片导致 8KB 栈分配失败。 */
    if (xTaskCreatePinnedToCoreWithCaps(camera_init_task, "cam_init", 8192, NULL, 5,
                                        NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "init task create failed");
        snprintf(s_last_err, sizeof(s_last_err), "初始化");
        s_img = NULL;
        return false;
    }
    if (xSemaphoreTake(s_init_sem, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGE(TAG, "camera init timeout");
        snprintf(s_last_err, sizeof(s_last_err), "超时");
        s_img = NULL;
        return false;
    }
    if (!s_init_ok) {
        if (!s_last_err[0]) snprintf(s_last_err, sizeof(s_last_err), "初始化");
        s_img = NULL;
        return false;
    }

    /* 拍照挑帧缓冲（VGA RGB565 = 600KB，放 PSRAM） */
    if (!s_shot_buf) {
        s_shot_buf_size = (size_t)640 * 480 * 2;
        s_shot_buf = (uint8_t *)heap_caps_malloc(s_shot_buf_size,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_shot_buf) {
            s_shot_buf = (uint8_t *)heap_caps_malloc(s_shot_buf_size, MALLOC_CAP_8BIT);
        }
        if (!s_shot_buf) {
            s_shot_buf_size = 0;
            ESP_LOGW(TAG, "no memory for photo pick buffer");
        }
    }

    if (!s_cam_lock) s_cam_lock = xSemaphoreCreateMutex();
    if (!s_shot_sem) s_shot_sem = xSemaphoreCreateBinary();
    if (!s_cam_lock || !s_shot_sem) {
        ESP_LOGE(TAG, "semaphore create failed");
        snprintf(s_last_err, sizeof(s_last_err), "内存");
        s_img = NULL;
        return false;
    }

    s_run = true;
    s_active = true;
    s_task_count = 0;

    /* 预览任务优先用内部 4KB 栈（和加相册之前一致）；失败再退到 PSRAM。
     * 相册把 4KB JPEG 工作缓冲从内部 RAM 挪走，就是为了把这里让出来。 */
    s_preview_psram = false;
    if (xTaskCreatePinnedToCore(preview_task, "cam_preview", 2560, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGW(TAG, "preview task internal stack failed, retry PSRAM");
        s_preview_psram = true;
        if (xTaskCreatePinnedToCoreWithCaps(preview_task, "cam_preview", 2560, NULL, 4,
                                            NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGE(TAG, "preview task create failed");
            snprintf(s_last_err, sizeof(s_last_err), "预览");
            s_run = false;
            s_active = false;
            s_img = NULL;
            return false;
        }
    }
    s_task_count = 1;

    /* 内部 RAM 很紧张（相机初始化后 8KB 栈都分不出来），拍照任务
     * 的 8KB 栈放到 PSRAM；JPEG 编码需要比预览任务更大的栈。 */
    if (xTaskCreatePinnedToCoreWithCaps(capture_task, "cam_capture", 8192, NULL, 6,
                                        NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "capture task create failed");
        snprintf(s_last_err, sizeof(s_last_err), "拍照");
        s_run = false;
        s_active = false;
        for (int i = 0; i < 100 && s_task_count > 0; i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        s_img = NULL;
        return false;
    }
    s_task_count = 2;

    s_last_err[0] = 0;
    ESP_LOGI(TAG, "camera app started (SD %s) int_free=%u dma_block=%u",
             sd_card_is_mounted() ? "ready" : "missing",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    return true;
}

void camera_app_stop(void)
{
    if (!s_active && !s_run && !s_cam_inited && !s_pv_buf) return;

    s_run = false;
    s_active = false;

    /* 唤醒可能正在等拍照信号的 capture 任务 */
    if (s_shot_sem) xSemaphoreGive(s_shot_sem);

    for (int i = 0; i < 400 && s_task_count > 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_task_count > 0) {
        ESP_LOGE(TAG, "camera tasks did not exit (%d), skip deinit", s_task_count);
        s_img = NULL;
        return;
    }

    if (s_cam_inited) {
        esp_camera_deinit();
        s_cam_inited = false;
        dvp_pwdn(1); /* 摄像头掉电 */
    }

    /* 互斥量/信号量保留复用：避免 stop 与 BOOT 拍照、LVGL 任务之间
     * 出现“检查完 s_shot_sem 又刚好被删除”的竞态。它们只占几十字节。 */
    if (s_pv_buf) {
        heap_caps_free(s_pv_buf);
        s_pv_buf = NULL;
    }
    if (s_shot_buf) {
        heap_caps_free(s_shot_buf);
        s_shot_buf = NULL;
        s_shot_buf_size = 0;
    }
    memset(&s_dsc, 0, sizeof(s_dsc));
    s_img = NULL;
    s_src_set = false;
    ESP_LOGI(TAG, "camera app stopped");
}

void camera_app_set_photo_mode(int mode)
{
    if (mode < CAM_PHOTO_CLEAR || mode > CAM_PHOTO_HD) mode = CAM_PHOTO_CLEAR;
    s_photo_mode = mode;
}

bool camera_app_take_photo(void)
{
    if (!s_active || !s_shot_sem) return false;
    return xSemaphoreGive(s_shot_sem) == pdTRUE;
}

const char *camera_app_last_error(void)
{
    return s_last_err[0] ? s_last_err : "无";
}
