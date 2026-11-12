#pragma once

#include <stdbool.h>

/* 连接 STA；没有配置或连不上时，自动开热点配网（192.168.4.1） */
void wifi_start(void);
bool wifi_is_connected(void);
const char *wifi_get_ssid(void);
/* 擦除 NVS 里的 Wi-Fi/服务器配置，下次开机进入配网热点 */
void wifi_reset_credentials(void);
