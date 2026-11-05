#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t display_start(void);
/* blokada LVGL - wymagana przy wywolaniach lv_* spoza watku LVGL */
bool display_lock(int timeout_ms);
void display_unlock(void);
