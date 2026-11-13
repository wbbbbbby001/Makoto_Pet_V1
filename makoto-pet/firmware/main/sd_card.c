#include "sd_card.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "sdcard";

/* 立创实战派 TF 卡槽：SDMMC 1 线模式（和官方 03-micro_sd 例程一致） */
#define SD_PIN_CLK  GPIO_NUM_47
#define SD_PIN_CMD  GPIO_NUM_48
#define SD_PIN_D0   GPIO_NUM_21

#define SD_MOUNT_POINT "/sdcard"

static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

const char *sd_card_mount_point(void)
{
    return SD_MOUNT_POINT;
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_card_mount(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        /* 不自动格式化：卡里可能有别的资料，格式化交给用户自己决定 */
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "mounting %s ...", SD_MOUNT_POINT);
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed (%s)，可能是没插卡或卡未格式化",
                 esp_err_to_name(err));
        s_card = NULL;
        /* esp_vfs_fat_sdmmc_mount 失败时内部已经 deinit 了 host，
         * 下一次调用会重新初始化，可以直接重试。 */
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "TF card mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}
