#include "sdkconfig.h"
#include "status_led.h"

#if CONFIG_DM32_RGB_ENABLE

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "meas.h"

#define LED_TIMER    LEDC_TIMER_1          /* TIMER_0 zajety przez podswietlenie LCD */
#define LED_MODE     LEDC_LOW_SPEED_MODE
#define LED_RES      LEDC_TIMER_10_BIT
#define LED_MAX      1023
#define PERIOD_MS    40

static const char *TAG = "rgb";
static const int s_pins[3] = { CONFIG_DM32_RGB_PIN_R, CONFIG_DM32_RGB_PIN_G, CONFIG_DM32_RGB_PIN_B };
static const ledc_channel_t s_ch[3] = { LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3 };

typedef struct {
    uint8_t r, g, b;
} rgb_t;

static void set_rgb(rgb_t c, int percent)
{
    const uint8_t v[3] = { c.r, c.g, c.b };
    for (int i = 0; i < 3; i++) {
        uint32_t duty = (uint32_t)v[i] * LED_MAX / 255;
        duty = duty * (uint32_t)CONFIG_DM32_RGB_BRIGHTNESS / 100;
        duty = duty * (uint32_t)percent / 100;
        ledc_set_duty(LED_MODE, s_ch[i], duty);
        ledc_update_duty(LED_MODE, s_ch[i]);
    }
}

static void led_task(void *arg)
{
    /* tylko czyste kolory - bez mieszania */
    const rgb_t GREEN = { 0, 255, 0 };
    const rgb_t RED = { 255, 0, 0 };
    const rgb_t BLUE = { 0, 0, 255 };
    const rgb_t OFF = { 0, 0, 0 };

    while (1) {
        meas_result_t r;
        meas_get(&r);
        if (r.seq == 0) {
            set_rgb(OFF, 100);          /* pomiar jeszcze nie wystartowal */
        } else if (r.no_adc || r.fault) {
            set_rgb(BLUE, 100);         /* brak ADS1115 / brak danych z ADC */
        } else if (r.tx) {
            set_rgb(RED, 100);          /* nadawanie */
        } else {
            set_rgb(GREEN, 100);        /* gotowosc */
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

void status_led_start(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LED_MODE,
        .duty_resolution = LED_RES,
        .timer_num = LED_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&t) != ESP_OK) {
        ESP_LOGE(TAG, "timer LEDC");
        return;
    }
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t c = {
            .gpio_num = s_pins[i],
            .speed_mode = LED_MODE,
            .channel = s_ch[i],
            .timer_sel = LED_TIMER,
            .duty = 0,
            .hpoint = 0,
#if CONFIG_DM32_RGB_COMMON_ANODE
            .flags.output_invert = 1,
#endif
        };
        ledc_channel_config(&c);
    }
    xTaskCreate(led_task, "rgb", 2560, NULL, 3, NULL);
    ESP_LOGI(TAG, "dioda RGB: R=%d G=%d B=%d", s_pins[0], s_pins[1], s_pins[2]);
}

#else

void status_led_start(void)
{
}

#endif
