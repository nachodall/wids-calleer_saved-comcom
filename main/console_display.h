#pragma once
#include "console_state.h"
#include "esp_err.h"

void console_display_init(void);
void console_display_radio_error(esp_err_t error);
void console_display_draw(const console_state_t *state, uint32_t now_ms);
