/*
 * DigiMeter32 by SP2KMO
 * Cyfrowy odczyt mocy i SWR z analogowego reflektometru.
 * ESP32 (WROOM-32D, tez C6) + LCD 4" 480x320 SPI (ILI9488/ILI9486/ST7796S) + LVGL.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "app_config.h"
#include "display.h"
#include "lcd_ili948x.h"
#include "ui.h"
#include "meas.h"
#include "button.h"
#include "console_cmds.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    config_init();
    app_config_t cfg;
    config_get(&cfg);

    ESP_ERROR_CHECK(display_start());
    if (display_lock(-1)) {
        ui_create();
        display_unlock();
    }
    lcd_set_backlight(cfg.backlight);

    err = meas_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pomiar nie wystartowal: %s", esp_err_to_name(err));
    }
    button_start();

    ESP_LOGI(TAG, "gotowe - wpisz 'help' w konsoli");
    ESP_ERROR_CHECK(console_start());
}
