/*
 * Pomiar przez ADS1115 (16 bit, I2C). Pomiary pojedyncze 860 S/s
 * na przemian FWD/REF, z automatycznym doborem wzmocnienia (PGA)
 * osobno dla kazdego kanalu - maly sygnal REF dostaje wieksze
 * wzmocnienie, co poprawia odczyt SWR bliskiego 1:1.
 */
#include <stdlib.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "meas.h"

#define REG_CONV   0x00
#define REG_CFG    0x01
#define PGA_MIN    1        /* +-4.096 V - najnizsze uzywane wzmocnienie (zasilanie 3.3 V) */
#define PGA_MAX    5        /* +-0.256 V */
#define RAW_HIGH   30000    /* powyzej: zmniejsz wzmocnienie */
#define RAW_LOW    13000    /* ponizej: zwieksz wzmocnienie */

static const char *TAG = "ads1115";
static const float s_fs[6] = { 6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f };

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static int s_pga[2] = { PGA_MIN, PGA_MIN };
static const int s_ain[2] = { CONFIG_DM32_ADS_FWD_CH, CONFIG_DM32_ADS_REF_CH };

static esp_err_t write_reg(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_master_transmit(s_dev, b, sizeof(b), 20);
}

static esp_err_t read_reg(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, 20);
    *v = ((uint16_t)b[0] << 8) | b[1];
    return err;
}

/* jedna konwersja; zwraca surowa wartosc ze znakiem */
static esp_err_t convert(int ain, int pga, int16_t *raw)
{
    uint16_t cfg = 0x8000                       /* OS: start */
                 | (uint16_t)((4 + ain) << 12)  /* MUX: AINx - GND */
                 | (uint16_t)(pga << 9)
                 | 0x0100                       /* tryb pojedynczy */
                 | (7 << 5)                     /* 860 S/s */
                 | 0x0003;                      /* komparator wylaczony */
    ESP_RETURN_ON_ERROR(write_reg(REG_CFG, cfg), TAG, "cfg");
    vTaskDelay(1);
    for (int i = 0; i < 5; i++) {
        uint16_t st;
        ESP_RETURN_ON_ERROR(read_reg(REG_CFG, &st), TAG, "status");
        if (st & 0x8000) {
            uint16_t v;
            ESP_RETURN_ON_ERROR(read_reg(REG_CONV, &v), TAG, "conv");
            *raw = (int16_t)v;
            return ESP_OK;
        }
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

/* konwersja z automatycznym doborem zakresu */
static esp_err_t measure(int idx, float *volts)
{
    int16_t raw;
    ESP_RETURN_ON_ERROR(convert(s_ain[idx], s_pga[idx], &raw), TAG, "convert");
    while (abs(raw) > RAW_HIGH && s_pga[idx] > PGA_MIN) {
        s_pga[idx]--;
        ESP_RETURN_ON_ERROR(convert(s_ain[idx], s_pga[idx], &raw), TAG, "convert");
    }
    *volts = raw * s_fs[s_pga[idx]] / 32768.0f;
    if (abs(raw) < RAW_LOW && s_pga[idx] < PGA_MAX) {
        s_pga[idx]++;               /* nastepny pomiar z wiekszym wzmocnieniem */
    }
    return ESP_OK;
}

static esp_err_t init(void)
{
    i2c_master_bus_config_t bcfg = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_DM32_I2C_SDA,
        .scl_io_num = CONFIG_DM32_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bcfg, &s_bus), TAG, "bus");

    if (i2c_master_probe(s_bus, CONFIG_DM32_ADS1115_ADDR, 50) != ESP_OK) {
        ESP_LOGI(TAG, "nie wykryto pod adresem 0x%02x", CONFIG_DM32_ADS1115_ADDR);
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_DM32_ADS1115_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dcfg, &s_dev), TAG, "dev");
    ESP_LOGI(TAG, "ADS1115 wykryty, FWD=A%d REF=A%d", s_ain[0], s_ain[1]);
    return ESP_OK;
}

static esp_err_t read_frame(meas_frame_t *f, uint32_t frame_ms)
{
    float sum[2] = {0}, mx[2] = {0};
    int   cnt[2] = {0};
    int64_t t_end = esp_timer_get_time() + (int64_t)frame_ms * 1000;

    do {
        for (int idx = 0; idx < 2; idx++) {
            float v;
            if (measure(idx, &v) != ESP_OK) {
                return ESP_FAIL;
            }
            if (v < 0) {
                v = 0;
            }
            sum[idx] += v;
            cnt[idx]++;
            if (v > mx[idx]) {
                mx[idx] = v;
            }
        }
    } while (esp_timer_get_time() < t_end);

    f->fwd_avg = sum[0] / cnt[0];
    f->ref_avg = sum[1] / cnt[1];
    f->fwd_pk = mx[0];
    f->ref_pk = mx[1];
    return ESP_OK;
}

const meas_backend_t meas_backend_ads1115 = {
    .name = "ADS",
    .init = init,
    .read_frame = read_frame,
};
