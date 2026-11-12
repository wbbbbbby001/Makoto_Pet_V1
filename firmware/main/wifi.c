#include "wifi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app.h"
#include "ui_app.h"

static const char *TAG = "wifi";

#define NVS_NS          "makoto"
#define WIFI_CONN_BIT   BIT0
#define WIFI_FAIL_BIT   BIT1
#define MAX_RETRY       6

static EventGroupHandle_t s_eg = NULL;
static volatile bool s_connected = false;
static char s_ssid[33] = {0};
static int s_retry = 0;
static bool s_started_ap = false;

/* 配网页面 */
static const char PORTAL_HTML[] =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Makoto Pet 配网</title>"
"<style>body{font-family:system-ui,-apple-system,'Microsoft YaHei';background:#0b1020;"
"color:#eaf2ff;display:flex;justify-content:center;padding:24px}"
".card{background:#14263a;border-radius:16px;padding:24px;max-width:380px;width:100%}"
"h2{margin:0 0 6px;font-size:20px}p{color:#8fa6bd;font-size:13px}"
"label{display:block;margin:16px 0 6px;font-size:13px;color:#9fc4e8}"
"input{width:100%;box-sizing:border-box;padding:12px;border-radius:10px;border:1px solid #26425e;"
"background:#0e1a28;color:#fff;font-size:16px}"
"button{width:100%;margin-top:20px;padding:13px;border:0;border-radius:10px;background:#1f8fff;"
"color:#fff;font-size:16px;font-weight:600}</style></head><body><div class='card'>"
"<h2>橘真琴 · 配网</h2><p>连上 Makoto-Pet 热点后，在这里填写家里的 Wi-Fi 和电脑的语音中枢地址。</p>"
"<form method='get' action='/save'>"
"<label>Wi-Fi 名称 (SSID)</label><input name='ssid' required>"
"<label>Wi-Fi 密码</label><input name='pass' type='password'>"
"<label>语音中枢地址</label><input name='server' placeholder='ws://192.168.1.100:8765/ws/device'>"
"<p style='margin-top:8px'>服务端地址可留空，使用固件编译时的默认值。</p>"
"<button type='submit'>保存并重启</button>"
"</form></div></body></html>";

static void url_decode(char *s)
{
    char *d = s;
    while (*s) {
        if (*s == '+') {
            *d++ = ' ';
            s++;
        } else if (*s == '%' && isxdigit((unsigned char)s[1]) &&
                   isxdigit((unsigned char)s[2])) {
            char hex[3] = {s[1], s[2], 0};
            *d++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *d++ = *s++;
        }
    }
    *d = 0;
}

static esp_err_t portal_root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t portal_save_get(httpd_req_t *req)
{
    char query[512] = {0};
    char ssid[128] = {0};
    char pass[128] = {0};
    char server[160] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
    }
    httpd_query_key_value(query, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(query, "pass", pass, sizeof(pass));
    httpd_query_key_value(query, "server", server, sizeof(server));
    url_decode(ssid);
    url_decode(pass);
    url_decode(server);

    if (ssid[0]) {
        nvs_handle_t nvs;
        if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "ssid", ssid);
            nvs_set_str(nvs, "pass", pass);
            if (server[0]) nvs_set_str(nvs, "server", server);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        ESP_LOGI(TAG, "provisioned ssid=%s server=%s", ssid, server);
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<meta charset='utf-8'><body style='background:#0b1020;color:#eaf2ff;"
        "font-family:sans-serif;padding:40px'><h3>已保存，设备正在重启…</h3></body>");
    vTaskDelay(pdMS_TO_TICKS(1800));
    esp_restart();
    return ESP_OK;
}

static void start_http_portal(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = portal_root_get};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_GET, .handler = portal_save_get};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
}

static void start_ap_portal(void)
{
    if (s_started_ap) return;
    s_started_ap = true;

    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Makoto-Pet-%02X%02X",
             mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    strcpy((char *)ap.ap.password, "makoto1234");
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGW(TAG, "AP mode: %s / makoto1234, open http://192.168.4.1",
             ap.ap.ssid);
    ui_set_wifi(false, NULL);
    ui_set_hint("连热点 Makoto-Pet-xxxx，打开 192.168.4.1 配网");
    start_http_portal();
}

static bool s_sntp_started = false;

/* 联网后校时，桌面左上角的时钟才是真实时间 */
static void start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    setenv("TZ", "CST-8", 1);   /* 北京时间 UTC+8 */
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (CST-8)");
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry < MAX_RETRY) {
            s_retry++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry = 0;
        s_connected = true;
        /* 桌宠一直插电：关闭 Wi-Fi 省电，减少周期性延迟 */
        esp_wifi_set_ps(WIFI_PS_NONE);
        start_sntp();
        xEventGroupSetBits(s_eg, WIFI_CONN_BIT);
    }
}

static bool load_nvs_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    bool ok = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t n = ssid_len;
        if (nvs_get_str(nvs, "ssid", ssid, &n) == ESP_OK && ssid[0]) {
            n = pass_len;
            if (nvs_get_str(nvs, "pass", pass, &n) != ESP_OK) pass[0] = 0;
            ok = true;
        }
        nvs_close(nvs);
    }
    return ok;
}

void wifi_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    if (!load_nvs_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        snprintf(ssid, sizeof(ssid), "%s", CONFIG_MAKOTO_WIFI_SSID);
        snprintf(pass, sizeof(pass), "%s", CONFIG_MAKOTO_WIFI_PASSWORD);
    }

    if (!ssid[0]) {
        ESP_LOGW(TAG, "no wifi credentials");
        esp_netif_init();
        esp_event_loop_create_default();
        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
        start_ap_portal();
        return;
    }

    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    s_eg = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t wc = {0};
    {
        size_t sl = strlen(ssid);
        if (sl > sizeof(wc.sta.ssid)) sl = sizeof(wc.sta.ssid);
        memcpy(wc.sta.ssid, ssid, sl);
        size_t pl = strlen(pass);
        if (pl > sizeof(wc.sta.password)) pl = sizeof(wc.sta.password);
        memcpy(wc.sta.password, pass, pl);
    }
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to %s ...", ssid);
    ui_set_hint("正在连接 Wi-Fi…");

    EventBits_t bits = xEventGroupWaitBits(s_eg, WIFI_CONN_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONN_BIT) {
        ui_set_wifi(true, s_ssid);
        ui_set_hint("");
    } else {
        ESP_LOGW(TAG, "connect failed, start AP portal");
        start_ap_portal();
    }
}

bool wifi_is_connected(void)
{
    return s_connected;
}

const char *wifi_get_ssid(void)
{
    return s_ssid;
}

/* 擦除 NVS 里的 Wi-Fi/服务端配置（设置页“重新配网”用） */
void wifi_reset_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_erase_key(h, "server");
        nvs_commit(h);
        nvs_close(h);
    }
}