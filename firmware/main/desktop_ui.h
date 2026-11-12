#pragma once

/* 桌面 Launcher：
 * - 待机显示 时钟 + AI/小游戏/记事本/设置/音乐/相机
 * - 进入 AI 对话时自动隐藏，露出原来的对话界面
 * - 网页控制台可以通过 desktop_ui_open_app("note") 等远程打开应用
 */
void desktop_ui_init(void);

/* 从桌面进入 AI 界面（隐藏桌面，显示对话页） */
void desktop_ui_enter_ai(void);
/* 从 AI 界面返回桌面 */
void desktop_ui_exit_ai(void);

/* 供服务端/网页控制台远程调用（内部自己加 LVGL 锁）
 * name: desktop / games / 2048 / snake / note / settings / music / camera
 */
void desktop_ui_open_app(const char *name);

/* 亮度（设置页和网页控制台共用） */
void desktop_ui_set_brightness(int v);
int desktop_ui_get_brightness(void);

/* 在桌面弹一条提示（网页控制台“显示文字”用） */
void desktop_ui_toast(const char *text);

/* 相机任务用：拿不到 LVGL 锁就放弃这条提示，避免关机/返回时互相等待 */
void desktop_ui_toast_try(const char *text);

/* 记事本拼音输入：服务端返回的候选（0x1f 分隔的 UTF-8 字符串） */
void desktop_ui_pinyin_result(int id, const char *packed);
