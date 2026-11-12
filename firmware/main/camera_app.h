#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 打开相机：初始化 GC0308、TF 卡，并把预览画到 preview_img（320×240）。
 * 返回 false 表示摄像头初始化失败。 */
bool camera_app_start(lv_obj_t *preview_img);

/* 关闭相机：停掉预览/拍照任务，释放摄像头。 */
void camera_app_stop(void);

bool camera_app_active(void);
bool camera_app_sd_ready(void);

/* 请求拍一张照片（异步，立刻返回）。相机未打开时返回 false。 */
bool camera_app_take_photo(void);

/* 拍照模式：
 * 0 = 清晰（Windowing，短曝光，适合手持）
 * 1 = 广角（Subsample，视野大，手持可能糊）
 * 2 = 高清 2MP（1600×1200，自拍延时，需要放稳） */
enum {
    CAM_PHOTO_CLEAR = 0,
    CAM_PHOTO_WIDE = 1,
    CAM_PHOTO_HD = 2,
};
void camera_app_set_photo_mode(int mode);

/* 最近一次打开相机失败的原因（很短，用于屏幕提示/网页状态） */
const char *camera_app_last_error(void);

#ifdef __cplusplus
}
#endif
