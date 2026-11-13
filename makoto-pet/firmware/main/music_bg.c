/* 音乐播放器背景：320x240 RGB565，由 tools/make_app_bgs.py 从 02.jpg 生成。 */
#include "lvgl.h"

extern const uint8_t music_bg_bin_start[] asm("_binary_music_bg_bin_start");

const lv_img_dsc_t music_bg = {
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 320,
    .header.h = 240,
    .data_size = 320 * 240 * 2,
    .data = music_bg_bin_start,
};
