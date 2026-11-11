#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_config.h"
#include "button.h"

#define POLL_MS     20
#define LONG_MS     800
#define SW_STABLE   3        /* przelacznik: 3 jednakowe odczyty = 60 ms */

#define HAVE_MODE_SW (CONFIG_DM32_MODE_SW_GPIO >= 0)

static const char *TAG = "button";

bool button_mode_switch_present(void)
{
    return HAVE_MODE_SW;
}

#if HAVE_MODE_SW
/* odczyt przelacznika: 1 = PEP, 0 = AVG */
static int mode_sw_read(void)
{
    bool closed = gpio_get_level(CONFIG_DM32_MODE_SW_GPIO) == 0;
#if CONFIG_DM32_MODE_SW_CLOSED_PEP
    return closed ? 1 : 0;
#else
    return closed ? 0 : 1;
#endif
}

static void mode_apply(int pep)
{
    app_config_t *c = config_lock();
    bool changed = c->pep_mode != pep;
    c->pep_mode = pep;
    /* bez zapisu do flash - po starcie tryb i tak czytany jest z przelacznika */
    config_unlock(false);
    if (changed) {
        ESP_LOGI(TAG, "przelacznik: tryb %s", pep ? "PEP" : "AVG");
    }
}
#endif

static void button_task(void *arg)
{
    int held_ms = 0;
    bool long_done = false;
#if HAVE_MODE_SW
    int sw_state = mode_sw_read();
    int sw_last = sw_state, sw_cnt = 0;
    mode_apply(sw_state);
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

#if HAVE_MODE_SW
        int s = mode_sw_read();
        sw_cnt = (s == sw_last) ? sw_cnt + 1 : 0;
        sw_last = s;
        if (sw_cnt >= SW_STABLE && s != sw_state) {
            sw_state = s;
            mode_apply(s);
        }
#endif

        bool pressed = gpio_get_level(CONFIG_DM32_BUTTON_GPIO) == 0;
        if (pressed) {
            held_ms += POLL_MS;
            if (!long_done && held_ms >= LONG_MS) {
                long_done = true;
                app_config_t *c = config_lock();
                c->active_prof = (c->active_prof + 1) % CAL_PROFILES;
                ESP_LOGI(TAG, "profil %d (%s)", c->active_prof, c->prof[c->active_prof].name);
                config_unlock(true);
            }
        } else {
#if !HAVE_MODE_SW
            if (held_ms >= 60 && !long_done) {
                app_config_t *c = config_lock();
                c->pep_mode = !c->pep_mode;
                ESP_LOGI(TAG, "tryb %s", c->pep_mode ? "PEP" : "AVG");
                config_unlock(true);
            }
#endif
            held_ms = 0;
            long_done = false;
        }
    }
}

void button_start(void)
{
    uint64_t mask = 1ULL << CONFIG_DM32_BUTTON_GPIO;
#if HAVE_MODE_SW
    mask |= 1ULL << CONFIG_DM32_MODE_SW_GPIO;
#endif
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
#if HAVE_MODE_SW
    ESP_LOGI(TAG, "przycisk GPIO%d, przelacznik AVG/PEP GPIO%d",
             CONFIG_DM32_BUTTON_GPIO, CONFIG_DM32_MODE_SW_GPIO);
#endif
}
