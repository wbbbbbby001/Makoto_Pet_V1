#include "lvgl.h"

extern const uint8_t icon_ai_bin_start[] asm("_binary_icon_ai_bin_start");
const lv_img_dsc_t icon_ai = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_ai_bin_start,
};

extern const uint8_t icon_games_bin_start[] asm("_binary_icon_games_bin_start");
const lv_img_dsc_t icon_games = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_games_bin_start,
};

extern const uint8_t icon_note_bin_start[] asm("_binary_icon_note_bin_start");
const lv_img_dsc_t icon_note = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_note_bin_start,
};

extern const uint8_t icon_settings_bin_start[] asm("_binary_icon_settings_bin_start");
const lv_img_dsc_t icon_settings = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_settings_bin_start,
};

extern const uint8_t icon_music_bin_start[] asm("_binary_icon_music_bin_start");
const lv_img_dsc_t icon_music = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_music_bin_start,
};

extern const uint8_t icon_camera_bin_start[] asm("_binary_icon_camera_bin_start");
const lv_img_dsc_t icon_camera = {
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 48,
    .header.h = 48,
    .data_size = 48 * 48 * 3,
    .data = icon_camera_bin_start,
};
