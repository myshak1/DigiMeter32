#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "lcd_ili948x.h"
#include "display.h"

#define DRAW_LINES 32   /* wysokosc paska renderowania LVGL */

static const char *TAG = "display";
static SemaphoreHandle_t s_lock;
static lv_display_t *s_disp;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    if (lcd_draw(a->x1, a->y1, a->x2, a->y2, (const uint16_t *)px) != ESP_OK) {
        lv_display_flush_ready(disp);
    }
}

static void lcd_done(void *arg)
{
    lv_display_flush_ready((lv_display_t *)arg);
}

bool display_lock(int timeout_ms)
{
    TickType_t t = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lock, t) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

static void lvgl_task(void *arg)
{
    while (1) {
        uint32_t wait = 10;
        if (display_lock(-1)) {
            wait = lv_timer_handler();
            display_unlock();
        }
        if (wait < 5) {
            wait = 5;
        } else if (wait > 30) {
            wait = 30;
        }
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

esp_err_t display_start(void)
{
    const size_t px = LCD_H_RES * DRAW_LINES;
    ESP_RETURN_ON_ERROR(lcd_init(px), TAG, "lcd");

    s_lock = xSemaphoreCreateRecursiveMutex();
    lv_init();
    lv_tick_set_cb(tick_ms);

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    size_t bytes = px * sizeof(uint16_t);
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(b1 && b2, ESP_ERR_NO_MEM, TAG, "bufory LVGL");
    lv_display_set_buffers(s_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lcd_set_done_cb(lcd_done, s_disp);

    BaseType_t ok = xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
