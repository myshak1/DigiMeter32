#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define LCD_H_RES 480
#define LCD_V_RES 320

typedef void (*lcd_done_cb_t)(void *arg);

/* max_pixels: najwiekszy obszar przesylany jednym wywolaniem lcd_draw() */
esp_err_t lcd_init(size_t max_pixels);
void      lcd_set_done_cb(lcd_done_cb_t cb, void *arg);
void      lcd_set_backlight(uint8_t percent);
/* Asynchronicznie wysyla obszar (wspolrzedne wlacznie) z bufora RGB565.
 * Po zakonczeniu wywoluje callback z lcd_set_done_cb() (z przerwania). */
esp_err_t lcd_draw(int x1, int y1, int x2, int y2, const uint16_t *rgb565);
const char *lcd_driver_name(void);
