/* 主桌面背景：320x240 RGB565，由 tools/make_app_bgs.py 从 01.jpg 生成。
 * 数据通过 CMake 的 EMBED_FILES 打进固件，不占 RAM。 */
#include "lvgl.h"

extern const uint8_t desktop_bg_bin_start[] asm("_binary_desktop_bg_bin_start");

const lv_img_dsc_t desktop_bg = {
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 320,
    .header.h = 240,
    .data_size = 320 * 240 * 2,
    .data = desktop_bg_bin_start,
};
