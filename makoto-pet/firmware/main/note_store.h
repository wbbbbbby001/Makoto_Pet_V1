#pragma once

#include <stddef.h>

/* 极简记事本：NVS namespace "desktop" / key "note" */
void note_clear(void);
void note_save(const char *text);
void note_append(const char *text);
/* 返回实际写入的字节数（不含结尾 0） */
size_t note_load(char *buf, size_t buf_size);
