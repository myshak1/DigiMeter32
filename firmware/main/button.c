#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_config.h"
#include "button.h"

#define POLL_MS   20
#define LONG_MS   800

static const char *TAG = "button";

static void button_task(void *arg)
{
    int held_ms = 0;
    bool long_done = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
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
            if (held_ms >= 60 && !long_done) {
                app_config_t *c = config_lock();
                c->pep_mode = !c->pep_mode;
                ESP_LOGI(TAG, "tryb %s", c->pep_mode ? "PEP" : "AVG");
                config_unlock(true);
            }
            held_ms = 0;
            long_done = false;
        }
    }
}

void button_start(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_DM32_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
}
