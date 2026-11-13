#include "note_store.h"

#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define NOTE_NS  "desktop"
#define NOTE_KEY "note"
#define NOTE_MAX 1800

static const char *TAG = "note";

static size_t utf8_truncate(const char *s, size_t len, size_t max_bytes)
{
    if (len <= max_bytes) return len;
    size_t n = max_bytes;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

void note_clear(void)
{
    note_save("");
}

void note_save(const char *text)
{
    if (!text) text = "";
    size_t len = strlen(text);
    len = utf8_truncate(text, len, NOTE_MAX);

    nvs_handle_t h;
    if (nvs_open(NOTE_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed");
        return;
    }
    char *tmp = malloc(len + 1);
    if (!tmp) {
        nvs_close(h);
        return;
    }
    memcpy(tmp, text, len);
    tmp[len] = 0;
    nvs_set_str(h, NOTE_KEY, tmp);
    nvs_commit(h);
    nvs_close(h);
    free(tmp);
}

void note_append(const char *text)
{
    if (!text || !text[0]) return;
    char cur[NOTE_MAX + 1] = {0};
    note_load(cur, sizeof(cur));
    size_t cl = strlen(cur);
    size_t tl = strlen(text);
    size_t need = cl + (cl ? 1 : 0) + tl;
    char *buf = malloc(need + 1);
    if (!buf) return;
    size_t p = 0;
    if (cl) {
        memcpy(buf, cur, cl);
        p = cl;
        if (buf[p - 1] != '\n') buf[p++] = '\n';
    }
    memcpy(buf + p, text, tl);
    p += tl;
    buf[p] = 0;
    note_save(buf);
    free(buf);
}

size_t note_load(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return 0;
    buf[0] = 0;

    nvs_handle_t h;
    if (nvs_open(NOTE_NS, NVS_READONLY, &h) != ESP_OK) return 0;

    size_t len = 0;
    if (nvs_get_str(h, NOTE_KEY, NULL, &len) != ESP_OK || len == 0) {
        nvs_close(h);
        return 0;
    }
    if (len > buf_size) len = buf_size;
    size_t got = len;
    char *tmp = malloc(len);
    if (!tmp) {
        nvs_close(h);
        return 0;
    }
    if (nvs_get_str(h, NOTE_KEY, tmp, &got) != ESP_OK) {
        free(tmp);
        nvs_close(h);
        return 0;
    }
    nvs_close(h);

    size_t n = got ? got - 1 : 0;  /* 去掉结尾 0 */
    if (n >= buf_size) n = buf_size - 1;
    memcpy(buf, tmp, n);
    buf[n] = 0;
    free(tmp);
    return n;
}
