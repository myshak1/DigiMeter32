#include <math.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "meas.h"
#include "calib.h"
#include "app_config.h"

#define FRAME_MS        20      /* 50 ramek/s */
#define SWR_STALE_MS    4000    /* ile pokazywac ostatni SWR po zakonczeniu TX */
#define PEP_DECAY       0.92f   /* opadanie wartosci szczytowej po czasie hold (na ramke) */
#define FAULT_FRAMES    5       /* tyle nieudanych ramek z rzedu = awaria ADC */

static const char *TAG = "meas";

static const meas_backend_t *s_be;
static meas_result_t s_res;
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_stream_ms;

void meas_get(meas_result_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    *out = s_res;
    taskEXIT_CRITICAL(&s_mux);
}

void meas_set_stream(uint32_t period_ms)
{
    s_stream_ms = period_ms;
}

esp_err_t meas_average(uint32_t ms, float *vf, float *vr, bool raw)
{
    meas_result_t r;
    meas_get(&r);
    uint32_t last = r.seq;
    double sf = 0, sr = 0;
    int n = 0;
    int64_t t_end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < t_end) {
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS / 2));
        meas_get(&r);
        if (r.seq != last) {
            last = r.seq;
            if (r.fault || r.no_adc) {
                continue;
            }
            sf += raw ? r.v_fwd_raw : r.v_fwd;
            sr += raw ? r.v_ref_raw : r.v_ref;
            n++;
        }
    }
    if (n == 0) {
        return ESP_ERR_TIMEOUT;
    }
    *vf = (float)(sf / n);
    *vr = (float)(sr / n);
    return ESP_OK;
}

/* wartosc szczytowa z podtrzymaniem i powolnym opadaniem */
typedef struct {
    float   val;
    int64_t t_set;
} peak_hold_t;

static float peak_update(peak_hold_t *ph, float x, int64_t now, uint32_t hold_ms)
{
    if (x >= ph->val) {
        ph->val = x;
        ph->t_set = now;
    } else if (now - ph->t_set > (int64_t)hold_ms * 1000) {
        ph->val *= PEP_DECAY;
        if (ph->val < x) {
            ph->val = x;
        }
    }
    return ph->val;
}

/* publikuje wynik "brak przetwornika": ekran pokazuje blad, alarm wylaczony */
static void publish_missing(uint32_t *seq, const app_config_t *cfg)
{
#if CONFIG_DM32_ALARM_GPIO >= 0
    gpio_set_level(CONFIG_DM32_ALARM_GPIO, 0);
#endif
    meas_result_t r = {
        .seq = ++*seq,
        .swr = 1.0f, .swr_min = 99.0f,
        .pep_mode = cfg->pep_mode != 0,
        .uncal = (cfg->prof[cfg->active_prof].fwd.n == 0),
        .no_adc = true,
        .backend = "ADS",
    };
    strncpy(r.prof_name, cfg->prof[cfg->active_prof].name, sizeof(r.prof_name) - 1);
    taskENTER_CRITICAL(&s_mux);
    s_res = r;
    taskEXIT_CRITICAL(&s_mux);
}

static void meas_task(void *arg)
{
    app_config_t cfg;
    uint32_t     rev = 0;
    float        pf_s = 0, pr_s = 0;
    peak_hold_t  pk_f = {0}, pk_r = {0};
    float        last_swr = 1.0f, last_rho = 0.0f, swr_min = 99.0f;
    int64_t      last_tx_us = 0;
    int64_t      last_stream_us = 0;
    uint32_t     seq = 0;
    uint32_t     fails = 0;
    int64_t      last_probe = 0;

#if CONFIG_DM32_ALARM_GPIO >= 0
    gpio_reset_pin(CONFIG_DM32_ALARM_GPIO);
    gpio_set_direction(CONFIG_DM32_ALARM_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_DM32_ALARM_GPIO, 0);
#endif

    while (1) {
        if (config_revision() != rev) {
            rev = config_revision();
            config_get(&cfg);
        }

        /* tryb "tylko ADS" bez wykrytego ADS1115: co sekunde probuj go wykryc */
        if (s_be == NULL) {
            publish_missing(&seq, &cfg);
            int64_t t = esp_timer_get_time();
            if (t - last_probe > 1000000) {
                last_probe = t;
                if (meas_backend_ads1115.init() == ESP_OK) {
                    s_be = &meas_backend_ads1115;
                    fails = 0;
                    ESP_LOGI(TAG, "ADS1115 wykryty");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        meas_frame_t f;
        if (s_be->read_frame(&f, FRAME_MS) != ESP_OK) {
            if (++fails == FAULT_FRAMES) {
                /* nie pokazuj zamrozonych wartosci i nie trzymaj alarmu */
                ESP_LOGE(TAG, "brak danych z ADC (%s)", s_be->name);
#if CONFIG_DM32_ALARM_GPIO >= 0
                gpio_set_level(CONFIG_DM32_ALARM_GPIO, 0);
#endif
                last_tx_us = 0;
                taskENTER_CRITICAL(&s_mux);
                s_res.seq = ++seq;
                s_res.fault = true;
                s_res.p_fwd_avg = s_res.p_ref_avg = 0.0f;
                s_res.p_fwd_pep = s_res.p_ref_pep = 0.0f;
                s_res.p_fwd = s_res.p_ref = 0.0f;
                s_res.tx = s_res.swr_valid = s_res.swr_stale = s_res.alarm = false;
                taskEXIT_CRITICAL(&s_mux);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (fails >= FAULT_FRAMES) {
            ESP_LOGI(TAG, "dane z ADC wrocily");
        }
        fails = 0;
        int64_t now = esp_timer_get_time();

        const cal_profile_t *pr = &cfg.prof[cfg.active_prof];
        const cal_table_t *tf = &pr->fwd;
        const cal_table_t *tr = pr->ref.n ? &pr->ref : &pr->fwd;

        float vf   = fmaxf(0.0f, f.fwd_avg - cfg.zero_fwd_v);
        float vr   = fmaxf(0.0f, f.ref_avg - cfg.zero_ref_v);
        float vfpk = fmaxf(0.0f, f.fwd_pk  - cfg.zero_fwd_v);
        float vrpk = fmaxf(0.0f, f.ref_pk  - cfg.zero_ref_v);

        float pf   = cal_volts_to_watts(tf, vf,   cfg.fs_v, cfg.fs_w);
        float prr  = cal_volts_to_watts(tr, vr,   cfg.fs_v, cfg.fs_w);
        float pfpk = cal_volts_to_watts(tf, vfpk, cfg.fs_v, cfg.fs_w);
        float prpk = cal_volts_to_watts(tr, vrpk, cfg.fs_v, cfg.fs_w);

        /* wygladzanie wykladnicze dla trybu AVG */
        float a = 1.0f - fminf(fmaxf(cfg.smooth, 0.0f), 0.95f);
        pf_s += a * (pf - pf_s);
        pr_s += a * (prr - pr_s);

        float pf_pep = peak_update(&pk_f, pfpk, now, cfg.hold_ms);
        float pr_pep = peak_update(&pk_r, prpk, now, cfg.hold_ms);

        float pF = cfg.pep_mode ? pf_pep : pf_s;
        float pR = cfg.pep_mode ? pr_pep : pr_s;

        bool tx = pfpk >= cfg.min_power_w || pF >= cfg.min_power_w;
        float swr = last_swr, rho = last_rho;
        bool valid = false, stale = false;

        if (tx && pF >= cfg.min_power_w) {
            /* nowa sesja = pierwszy wazny SWR po przerwie dluzszej niz SWR_STALE_MS;
             * nie zalezy od zbocza tx, bo w AVG tx wyprzedza waznosc SWR, a przy SSB
             * tx znika w przerwach miedzy slowami */
            if (last_tx_us == 0 || now - last_tx_us >= (int64_t)SWR_STALE_MS * 1000) {
                swr_min = 99.0f;
            }
            swr = cal_swr(pF, pR, &rho);
            last_swr = swr;
            last_rho = rho;
            last_tx_us = now;
            valid = true;
            if (swr < swr_min) {
                swr_min = swr;
            }
        } else if (last_tx_us && now - last_tx_us < (int64_t)SWR_STALE_MS * 1000) {
            stale = true;
        }

        bool alarm = valid && swr >= cfg.alarm_swr;
#if CONFIG_DM32_ALARM_GPIO >= 0
        gpio_set_level(CONFIG_DM32_ALARM_GPIO, alarm ? 1 : 0);
#endif

        meas_result_t r = {
            .seq = ++seq,
            .v_fwd_raw = f.fwd_avg, .v_ref_raw = f.ref_avg,
            .v_fwd = vf, .v_ref = vr,
            .p_fwd_avg = pf_s, .p_ref_avg = pr_s,
            .p_fwd_pep = pf_pep, .p_ref_pep = pr_pep,
            .p_fwd = pF, .p_ref = pR,
            .swr = swr, .rho = rho, .swr_min = swr_min,
            .tx = tx, .swr_valid = valid, .swr_stale = stale,
            .alarm = alarm,
            .uncal = (pr->fwd.n == 0),
            .pep_mode = cfg.pep_mode != 0,
            .backend = s_be->name,
        };
        strncpy(r.prof_name, pr->name, sizeof(r.prof_name) - 1);

        taskENTER_CRITICAL(&s_mux);
        s_res = r;
        taskEXIT_CRITICAL(&s_mux);

        uint32_t sm = s_stream_ms;
        if (sm && now - last_stream_us >= (int64_t)sm * 1000) {
            last_stream_us = now;
            printf("D,%lld,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
                   now / 1000, f.fwd_avg, f.ref_avg, vf, vr, pF, pR, valid ? swr : 0.0f);
        }
    }
}

esp_err_t meas_start(void)
{
    app_config_t cfg;
    config_get(&cfg);

    s_be = NULL;
    if (cfg.adc_mode == ADC_MODE_AUTO || cfg.adc_mode == ADC_MODE_ADS1115) {
        if (meas_backend_ads1115.init() == ESP_OK) {
            s_be = &meas_backend_ads1115;
        } else if (cfg.adc_mode == ADC_MODE_ADS1115) {
            /* tryb "tylko ADS": nie mierzymy niepodlaczonych wejsc ESP,
             * zadanie pomiarowe bedzie ponawiac wykrywanie co sekunde */
            ESP_LOGE(TAG, "ADS1115 nie odpowiada (SDA=%d, SCL=%d, adres 0x%02x) - sprawdz polaczenia",
                     CONFIG_DM32_I2C_SDA, CONFIG_DM32_I2C_SCL, CONFIG_DM32_ADS1115_ADDR);
        }
    }
    if (s_be == NULL && cfg.adc_mode != ADC_MODE_ADS1115) {
        esp_err_t err = meas_backend_internal.init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "init wbudowanego ADC: %s", esp_err_to_name(err));
            return err;
        }
        s_be = &meas_backend_internal;
    }
    ESP_LOGI(TAG, "zrodlo pomiaru: %s", s_be ? s_be->name : "brak (czekam na ADS1115)");
    s_res.backend = s_be ? s_be->name : "ADS";

    BaseType_t ok = xTaskCreate(meas_task, "meas", 4096, NULL, 6, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
