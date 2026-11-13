/* 316x126 蜡笔小新键盘背景，由 tools/make_shinchan_assets.py 生成。 */
#include "lvgl.h"

extern const uint8_t shinchan_kb_bin_start[] asm("_binary_shinchan_kb_bin_start");

const lv_img_dsc_t shinchan_kb = {
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 316,
    .header.h = 126,
    .data_size = 316 * 126 * 2,
    .data = shinchan_kb_bin_start,
};
