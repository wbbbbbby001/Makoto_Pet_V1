/* 28x28 蜡笔小新头像，由 tools/make_shinchan_assets.py 生成。 */
#include "lvgl.h"

extern const uint8_t shinchan_icon_bin_start[] asm("_binary_shinchan_icon_bin_start");

const lv_img_dsc_t shinchan_icon = {
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 28,
    .header.h = 28,
    .data_size = 28 * 28 * 2,
    .data = shinchan_icon_bin_start,
};
