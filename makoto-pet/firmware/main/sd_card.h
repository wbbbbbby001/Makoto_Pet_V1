#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 挂载 TF 卡（SDMMC 1 线）。重复调用是安全的：
 * 已挂载时直接返回 ESP_OK；之前挂载失败会自动重试。 */
esp_err_t sd_card_mount(void);

bool sd_card_is_mounted(void);

/* 挂载点，固定为 /sdcard */
const char *sd_card_mount_point(void);

#ifdef __cplusplus
}
#endif
