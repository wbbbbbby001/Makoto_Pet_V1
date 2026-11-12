#include "net_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "nvs.h"

#include "app.h"
#include "audio_io.h"
#include "camera_app.h"
#include "desktop_ui.h"
#include "esp32_s3_szp.h"
#include "music_app.h"
#include "note_store.h"
#include "ui_app.h"
#include "wifi.h"
#include "esp_system.h"

static const char *TAG = "ws";

static esp_websocket_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
/* 文本帧重组缓冲放 PSRAM，别占内部 RAM（相机预览栈很吃紧） */
#define WS_TEXT_BUF_SIZE 8192
static char *s_text_buf = NULL;

static const char *jstr(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

static int jint(const cJSON *root, const char *key, int def)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : def;
}

static double jnum(const cJSON *root, const char *key, double def)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return (item && cJSON_IsNumber(item)) ? item->valuedouble : def;
}

static void send_hello(void)
{
    net_ws_send_text("{\"type\":\"hello\",\"device\":\"szpi-s3\",\"fw\":\"1.1.0\","
                     "\"cap\":[\"pcm16\",\"touch\",\"display\",\"desktop\",\"note\","
                     "\"camera\",\"music\"]}");
}

/* 上报设备状态给网页控制台 */
static void send_status(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddNumberToObject(root, "volume", audio_get_volume());
    cJSON_AddNumberToObject(root, "brightness", desktop_ui_get_brightness());
    cJSON_AddStringToObject(root, "wifi", wifi_get_ssid() ? wifi_get_ssid() : "");
    cJSON_AddNumberToObject(root, "state", (int)app_get_state());
    cJSON_AddNumberToObject(root, "camera", camera_app_active() ? 1 : 0);
    cJSON_AddStringToObject(root, "camera_err", camera_app_last_error());
    char *s = cJSON_PrintUnformatted(root);
    if (s) {
        net_ws_send_text(s);
        free(s);
    }
    cJSON_Delete(root);
}

static void handle_text(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        ESP_LOGW(TAG, "bad json");
        return;
    }
    const char *type = jstr(root, "type");
    if (!type) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "ready") == 0) {
        const char *voice_name = jstr(root, "voice_name");
        if (voice_name) ui_set_voice(voice_name);
        ui_set_hint("已连接语音中枢");
        app_set_state(ST_IDLE);
        send_status();

    } else if (strcmp(type, "state") == 0) {
        const char *st = jstr(root, "state");
        if (st) {
            if (strcmp(st, "listening") == 0) app_set_state(ST_LISTENING);
            else if (strcmp(st, "thinking") == 0) app_set_state(ST_THINKING);
            else if (strcmp(st, "idle") == 0) app_set_state(ST_IDLE);
        }

    } else if (strcmp(type, "asr") == 0) {
        const char *text = jstr(root, "text");
        if (text) {
            ui_set_user_text(text);
            ui_set_hint("");
        }

    } else if (strcmp(type, "reply") == 0) {
        const char *text = jstr(root, "text");
        if (text) ui_set_ai_text(text);
        ui_set_hint("");

    } else if (strcmp(type, "tool") == 0) {
        const char *name = jstr(root, "name");
        if (name && strcmp(name, "web_search") == 0) {
            ui_set_hint("正在联网搜索…");
        } else if (name && name[0]) {
            ui_set_hint(name);
        }

    } else if (strcmp(type, "tts_start") == 0) {
        uint32_t sr = (uint32_t)jint(root, "sample_rate", 32000);
        audio_play_begin(sr);
        app_set_state(ST_SPEAKING);

    } else if (strcmp(type, "tts_end") == 0) {
        audio_play_end();

    } else if (strcmp(type, "tts_stop") == 0) {
        audio_play_reset();
        app_set_state(ST_IDLE);

    } else if (strcmp(type, "music_start") == 0) {
        uint32_t sr = (uint32_t)jint(root, "sample_rate", 16000);
        /* 音乐先收满 2.5 秒再出声；TTS 仍然用 audio_play_begin 立即播 */
        audio_play_begin_buffered(sr, 2500);
        app_set_state(ST_IDLE);
        music_app_on_start(jstr(root, "title"), jstr(root, "artist"),
                           jstr(root, "source"), jstr(root, "license"),
                           jnum(root, "duration", 0), jnum(root, "position", 0),
                           jint(root, "index", 0), jint(root, "total", 0));

    } else if (strcmp(type, "music_state") == 0) {
        const char *st = jstr(root, "state");
        if (st) {
            if (!strcmp(st, "paused")) audio_play_pause();
            else if (!strcmp(st, "playing")) audio_play_resume();
            music_app_on_state(st);
        }

    } else if (strcmp(type, "music_progress") == 0) {
        music_app_on_progress(jnum(root, "position", 0), jnum(root, "duration", 0));

    } else if (strcmp(type, "music_end") == 0) {
        music_app_on_end(NULL);
        audio_play_end();

    } else if (strcmp(type, "music_stop") == 0) {
        audio_play_reset();
        music_app_on_state("stopped");
        app_set_state(ST_IDLE);

    } else if (strcmp(type, "music_error") == 0) {
        const char *m = jstr(root, "message");
        audio_play_reset();
        music_app_on_end(m ? m : "播放失败");
        ui_set_hint(m ? m : "音乐播放失败");
        app_set_state(ST_IDLE);

    } else if (strcmp(type, "voice") == 0) {
        const char *name = jstr(root, "name");
        if (name) {
            ui_set_voice(name);
            ui_set_hint("音色已切换");
        }

    } else if (strcmp(type, "error") == 0) {
        const char *msg = jstr(root, "message");
        ESP_LOGW(TAG, "server error: %s", msg ? msg : "?");
        ui_set_hint(msg ? msg : "服务端出错");
        app_set_state(ST_IDLE);

    } else if (strcmp(type, "cmd") == 0) {
        /* 网页控制台 / AI 工具下发的设备指令 */
        const char *cmd = jstr(root, "cmd");
        if (cmd) {
            if (!strcmp(cmd, "volume")) {
                audio_set_volume(jint(root, "value", 70));
            } else if (!strcmp(cmd, "brightness")) {
                desktop_ui_set_brightness(jint(root, "value", 70));
            } else if (!strcmp(cmd, "open_app")) {
                const char *app_name = jstr(root, "value");
                if (app_name) desktop_ui_open_app(app_name);
            } else if (!strcmp(cmd, "note_append")) {
                const char *text = jstr(root, "text");
                if (text) note_append(text);
            } else if (!strcmp(cmd, "note_clear")) {
                note_clear();
            } else if (!strcmp(cmd, "photo")) {
                camera_app_take_photo();
            } else if (!strcmp(cmd, "text")) {
                const char *text = jstr(root, "text");
                if (text) {
                    ui_set_hint(text);
                    desktop_ui_toast(text);
                }
            } else if (!strcmp(cmd, "reboot")) {
                esp_restart();
            }
            send_status();
        }

    } else if (strcmp(type, "pinyin_result") == 0) {
        /* 服务端返回记事本拼音候选，用 0x1f 分隔后交给 UI */
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "candidates");
        char *packed = (char *)calloc(1, 4096);
        if (packed) {
            size_t used = 0;
            if (cJSON_IsArray(arr)) {
                const cJSON *it = NULL;
                cJSON_ArrayForEach(it, arr) {
                    if (!cJSON_IsString(it) || !it->valuestring) continue;
                    size_t l = strlen(it->valuestring);
                    if (used + l + 2 >= 4096) break;
                    memcpy(packed + used, it->valuestring, l);
                    used += l;
                    packed[used++] = 0x1f;
                    packed[used] = 0;
                }
            }
            desktop_ui_pinyin_result(jint(root, "id", -1), packed);
            free(packed);
        }

    } else if (strcmp(type, "status") == 0) {
        /* 设备自己上报的状态，服务端保存 */

    } else if (strcmp(type, "ping") == 0) {
        net_ws_send_text("{\"type\":\"pong\"}");
    }

    cJSON_Delete(root);
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                             void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected");
        s_connected = true;
        send_hello();
        send_status();
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        ui_set_hint("连接断开，正在重连…");
        app_set_state(ST_ERROR);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x1) {          /* text：可能被拆成多个分片 */
            static size_t s_text_len = 0;
            size_t off = (size_t)data->payload_offset;
            size_t n = (size_t)data->data_len;
            size_t total = (size_t)data->payload_len;
            if (!s_text_buf) break;
            if (off == 0) s_text_len = 0;
            if (off < WS_TEXT_BUF_SIZE && n <= WS_TEXT_BUF_SIZE - off) {
                memcpy(s_text_buf + off, data->data_ptr, n);
                s_text_len = off + n;
                if (total <= off + n) {
                    handle_text(s_text_buf, (int)s_text_len);
                    s_text_len = 0;
                }
            } else {
                s_text_len = 0;
            }
        } else if (data->op_code == 0x2) {   /* binary = PCM16，分片全部按顺序播放 */
            audio_play_chunk(data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "ws error");
        if (!s_connected) {
            app_set_state(ST_ERROR);
        }
        break;

    default:
        break;
    }
}

static char s_uri[160];

static void load_uri(void)
{
    snprintf(s_uri, sizeof(s_uri), "%s", CONFIG_MAKOTO_SERVER_URI);
    nvs_handle_t nvs;
    if (nvs_open("makoto", NVS_READONLY, &nvs) == ESP_OK) {
        size_t n = sizeof(s_uri);
        if (nvs_get_str(nvs, "server", s_uri, &n) != ESP_OK) {
            snprintf(s_uri, sizeof(s_uri), "%s", CONFIG_MAKOTO_SERVER_URI);
        }
        nvs_close(nvs);
    }
}

void net_ws_start(void)
{
    load_uri();
    if (!s_text_buf) {
        s_text_buf = heap_caps_malloc(WS_TEXT_BUF_SIZE,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_text_buf) {
            ESP_LOGW(TAG, "text buf in psram failed, fallback internal");
            s_text_buf = heap_caps_malloc(WS_TEXT_BUF_SIZE, MALLOC_CAP_8BIT);
        }
        if (!s_text_buf) ESP_LOGE(TAG, "text buf alloc failed");
    }
    esp_websocket_client_config_t cfg = {
        .uri = s_uri,
        .reconnect_timeout_ms = 2000,
        .network_timeout_ms = 10000,
        .buffer_size = 4096,
        .task_stack = 6144,
        .ping_interval_sec = 20,
    };

    ESP_LOGI(TAG, "connecting %s", s_uri);
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "client init failed");
        app_set_state(ST_ERROR);
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(s_client);
}

bool net_ws_is_connected(void)
{
    return s_connected && s_client;
}

int net_ws_send_text(const char *json)
{
    if (!net_ws_is_connected() || !json) return -1;
    int n = esp_websocket_client_send_text(s_client, json, strlen(json),
                                           pdMS_TO_TICKS(1000));
    if (n < 0) ESP_LOGW(TAG, "send text failed");
    return n;
}

int net_ws_send_bin(const void *data, size_t len)
{
    if (!net_ws_is_connected() || !data || !len) return -1;
    return esp_websocket_client_send_bin(s_client, (const char *)data, len,
                                         pdMS_TO_TICKS(2000));
}

void net_ws_send_pcm(const int16_t *pcm, size_t samples)
{
    if (!net_ws_is_connected() || !pcm || !samples) return;
    net_ws_send_bin(pcm, samples * sizeof(int16_t));
}

int net_ws_send_music_cmd(const char *action)
{
    if (!action || !action[0]) return -1;
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "type", "music_cmd");
    cJSON_AddStringToObject(root, "action", action);
    char *s = cJSON_PrintUnformatted(root);
    int r = s ? net_ws_send_text(s) : -1;
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return r;
}

int net_ws_send_pinyin(unsigned id, const char *text, int offset)
{
    if (!text || !text[0]) return -1;
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "type", "pinyin");
    cJSON_AddNumberToObject(root, "id", (double)id);
    cJSON_AddNumberToObject(root, "offset", (double)offset);
    cJSON_AddStringToObject(root, "text", text);
    char *s = cJSON_PrintUnformatted(root);
    int r = s ? net_ws_send_text(s) : -1;
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return r;
}

