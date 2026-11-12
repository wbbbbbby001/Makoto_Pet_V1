#pragma once

#include <stdbool.h>
#include "app.h"

void ui_init(void);

void ui_update_state(app_state_t st);
void ui_set_user_text(const char *text);
void ui_set_ai_text(const char *text);
void ui_set_hint(const char *text);
void ui_set_mic_level(int level);
void ui_set_wifi(bool connected, const char *ssid);
void ui_set_voice(const char *name);
