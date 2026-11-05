/*
 * Pomiar wbudowanym ADC (ESP32 lub ESP32-C6) w trybie ciaglym (DMA).
 * 2 kanaly x 10 kS/s, srednia i maksimum w ramce. Maksimum liczone
 * z blokow po 8 probek, zeby szum pojedynczych probek nie zawyzal PEP.
 */
#include <string.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "meas.h"

#define SAMPLE_HZ    20000      /* lacznie dla obu kanalow */
#define READ_LEN     256
#define PK_BLOCK     8
#define ADC_ATTEN    ADC_ATTEN_DB_12

static const char *TAG = "adc_int";

static adc_continuous_handle_t s_h;
static adc_channel_t           s_ch[2];
static adc_cali_handle_t       s_cali[2];
static uint8_t                 s_buf[READ_LEN] __attribute__((aligned(4)));
static adc_continuous_data_t   s_parsed[READ_LEN / SOC_ADC_DIGI_RESULT_BYTES];

static float raw_to_volts(int idx, float raw)
{
    if (raw < 0) {
        raw = 0;
    }
    int r0 = (int)raw;
    if (r0 >= 4095) {
        r0 = 4094;
    }
    float frac = raw - r0;
    if (s_cali[idx]) {
        int mv0 = 0, mv1 = 0;
        adc_cali_raw_to_voltage(s_cali[idx], r0, &mv0);
        adc_cali_raw_to_voltage(s_cali[idx], r0 + 1, &mv1);
        return (mv0 + frac * (mv1 - mv0)) / 1000.0f;
    }
    return raw * 3.3f / 4095.0f;    /* brak kalibracji fabrycznej - przyblizenie */
}

static esp_err_t init(void)
{
    const int gpios[2] = { CONFIG_DM32_ADC_FWD_GPIO, CONFIG_DM32_ADC_REF_GPIO };
    for (int i = 0; i < 2; i++) {
        adc_unit_t unit;
        ESP_RETURN_ON_ERROR(adc_continuous_io_to_channel(gpios[i], &unit, &s_ch[i]), TAG,
                            "GPIO%d nie jest wejsciem ADC", gpios[i]);
        ESP_RETURN_ON_FALSE(unit == ADC_UNIT_1, ESP_ERR_INVALID_ARG, TAG, "wymagany ADC1");
    }

    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = 2048,
        .conv_frame_size = READ_LEN,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&hcfg, &s_h), TAG, "new_handle");

    adc_digi_pattern_config_t pat[2] = {0};
    for (int i = 0; i < 2; i++) {
        pat[i].atten = ADC_ATTEN;
        pat[i].channel = s_ch[i] & 0x7;
        pat[i].unit = ADC_UNIT_1;
        pat[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    adc_continuous_config_t dcfg = {
        .pattern_num = 2,
        .adc_pattern = pat,
        .sample_freq_hz = SAMPLE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_config(s_h, &dcfg), TAG, "config");

    for (int i = 0; i < 2; i++) {
        esp_err_t cerr = ESP_ERR_NOT_SUPPORTED;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cc = {
            .unit_id = ADC_UNIT_1,
            .chan = s_ch[i],
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_12,
        };
        cerr = adc_cali_create_scheme_curve_fitting(&cc, &s_cali[i]);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t lc = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_12,
        };
        cerr = adc_cali_create_scheme_line_fitting(&lc, &s_cali[i]);
#endif
        if (cerr != ESP_OK) {
            s_cali[i] = NULL;
            ESP_LOGW(TAG, "brak kalibracji eFuse ADC - dokladnosc ograniczona");
        }
    }

    ESP_RETURN_ON_ERROR(adc_continuous_start(s_h), TAG, "start");
    ESP_LOGI(TAG, "ADC1 kanaly %d (FWD) i %d (REF), %d S/s", s_ch[0], s_ch[1], SAMPLE_HZ);
    return ESP_OK;
}

static esp_err_t read_frame(meas_frame_t *f, uint32_t frame_ms)
{
    uint32_t sum[2] = {0}, cnt[2] = {0};
    uint32_t blk_sum[2] = {0}, blk_n[2] = {0};
    float    blk_max[2] = {0};
    int64_t  t_end = esp_timer_get_time() + (int64_t)frame_ms * 1000;
    int      guard = 0;

    while ((esp_timer_get_time() < t_end || cnt[0] == 0 || cnt[1] == 0) && guard < 64) {
        uint32_t n = 0;
        esp_err_t err = adc_continuous_read(s_h, s_buf, READ_LEN, &n, 50);
        guard++;
        if (err != ESP_OK) {
            continue;
        }
        uint32_t np = 0;
        if (adc_continuous_parse_data(s_h, s_buf, n, s_parsed, &np) != ESP_OK) {
            continue;
        }
        for (uint32_t i = 0; i < np; i++) {
            if (!s_parsed[i].valid) {
                continue;
            }
            uint32_t ch = s_parsed[i].channel;
            uint32_t d = s_parsed[i].raw_data;
            int idx = (ch == (uint32_t)s_ch[0]) ? 0 : (ch == (uint32_t)s_ch[1]) ? 1 : -1;
            if (idx < 0) {
                continue;
            }
            sum[idx] += d;
            cnt[idx]++;
            blk_sum[idx] += d;
            if (++blk_n[idx] == PK_BLOCK) {
                float m = (float)blk_sum[idx] / PK_BLOCK;
                if (m > blk_max[idx]) {
                    blk_max[idx] = m;
                }
                blk_sum[idx] = 0;
                blk_n[idx] = 0;
            }
        }
    }
    if (cnt[0] == 0 || cnt[1] == 0) {
        return ESP_ERR_TIMEOUT;
    }
    float avg0 = (float)sum[0] / cnt[0];
    float avg1 = (float)sum[1] / cnt[1];
    f->fwd_avg = raw_to_volts(0, avg0);
    f->ref_avg = raw_to_volts(1, avg1);
    f->fwd_pk  = raw_to_volts(0, blk_max[0] > avg0 ? blk_max[0] : avg0);
    f->ref_pk  = raw_to_volts(1, blk_max[1] > avg1 ? blk_max[1] : avg1);
    return ESP_OK;
}

const meas_backend_t meas_backend_internal = {
    .name = "INT",
    .init = init,
    .read_frame = read_frame,
};
