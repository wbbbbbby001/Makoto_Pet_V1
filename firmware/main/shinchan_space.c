/* 28x28 空格键小新全身（透明底），由 tools/make_shinchan_assets.py 生成。 */
#include "lvgl.h"

extern const uint8_t shinchan_space_bin_start[] asm("_binary_shinchan_space_bin_start");

const lv_img_dsc_t shinchan_space = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 28,
    .header.h = 28,
    .data_size = 28 * 28 * 3,
    .data = shinchan_space_bin_start,
};
