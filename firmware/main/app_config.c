#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_config.h"

#define CFG_MAGIC   0x444D3332u   /* 'DM32' */
#define CFG_VERSION 2u   /* 2: fabryczna kalibracja SP2KMO */
#define NVS_NS      "dm32"
#define NVS_KEY     "cfg"

static const char *TAG = "config";
static app_config_t      s_cfg;
static SemaphoreHandle_t s_mtx;
static volatile uint32_t s_rev = 1;

#if CONFIG_DM32_DEFAULT_CAL
/*
 * Pomiary SP2KMO, 2026-11-01: IC-7000, sztuczne obciazenie 50 om,
 * miernik w pozycji FUNCTION = SWR, napiecie na czerwonym przewodzie
 * (FWD) wzgledem masy, z wlutowana plytka bufora.
 *
 *   kHz     5 W    10 W   25 W   50 W   100 W
 *   50313          2.19          5.89   8.24
 *   28074          2.21          6.03   8.40
 *   21074          2.24          6.08   9.02
 *   14074          2.21          6.03   8.92
 *    7074   1.48   2.19   4.24   6.03   8.87
 *    3573          2.15          5.68   8.42
 *    1840          1.82          4.00   4.97
 *
 * 80-6 m: przy 10 W rozrzut +-2 %, wiec jedna krzywa (z 40 m).
 * 160 m: wyraznie nizej i silne splaszczenie przy 50/100 W - wstepnie
 * krzywa z 40 m przeskalowana wg punktu 10 W (1.82 / 2.19).
 */
typedef struct { float w, v_wire; } wire_pt_t;
static const wire_pt_t s_curve_40m[] = {
    { 5.0f, 1.48f }, { 10.0f, 2.19f }, { 25.0f, 4.24f }, { 50.0f, 6.03f }, { 100.0f, 8.87f },
};
#define K160 (1.82f / 2.19f)

static void load_curve(cal_table_t *t, float scale)
{
    const float div = (float)CONFIG_DM32_DIV_R_BOT /
                      ((float)CONFIG_DM32_DIV_R_TOP + (float)CONFIG_DM32_DIV_R_BOT);
    t->n = 0;
    for (size_t i = 0; i < sizeof(s_curve_40m) / sizeof(s_curve_40m[0]); i++) {
        t->pt[t->n].v = s_curve_40m[i].v_wire * scale * div;
        t->pt[t->n].w = s_curve_40m[i].w;
        t->n++;
    }
}
#endif

void config_set_defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = CFG_MAGIC;
    c->version = CFG_VERSION;
    for (int i = 0; i < CAL_PROFILES; i++) {
        snprintf(c->prof[i].name, PROFILE_NAME_LEN, "P%d", i + 1);
    }
#if CONFIG_DM32_DEFAULT_CAL
    strcpy(c->prof[0].name, "KF 80-6m");
    load_curve(&c->prof[0].fwd, 1.0f);
    strcpy(c->prof[1].name, "160m");
    load_curve(&c->prof[1].fwd, K160);
    strcpy(c->prof[2].name, "2m");
#endif
    c->active_prof = 0;
    c->pep_mode = 0;
#if CONFIG_DM32_ADC_DEFAULT_INT
    c->adc_mode = ADC_MODE_INTERNAL;
#elif CONFIG_DM32_ADC_DEFAULT_AUTO
    c->adc_mode = ADC_MODE_AUTO;
#else
    c->adc_mode = ADC_MODE_ADS1115;
#endif
    c->backlight = 100;
    c->min_power_w = 0.5f;
    c->alarm_swr = 3.0f;
    c->hold_ms = 1500;
    c->smooth = 0.6f;
    c->fs_v = CONFIG_DM32_DEFAULT_FS_MV / 1000.0f;
    c->fs_w = CONFIG_DM32_DEFAULT_FS_W;
}

/* tabela niezgodna z zalozeniami calib.c (zakres, monotonicznosc) jest czyszczona */
static bool sanitize_table(cal_table_t *t)
{
    bool ok = t->n <= CAL_MAX_POINTS;
    for (int i = 0; ok && i < t->n; i++) {
        const cal_point_t *p = &t->pt[i];
        ok = isfinite(p->v) && isfinite(p->w) && p->v > 0.0f && p->w > 0.0f &&
             (i == 0 || (p->v > t->pt[i - 1].v && p->w > t->pt[i - 1].w));
    }
    if (!ok) {
        t->n = 0;
    }
    return ok;
}

static bool float_ok(float x, float lo, float hi)
{
    return isfinite(x) && x >= lo && x <= hi;
}

/* naprawia pola spoza zakresu (uszkodzony blob) wartosciami domyslnymi */
static void sanitize(app_config_t *c)
{
    app_config_t d;
    config_set_defaults(&d);
    int bad = 0;

    for (int i = 0; i < CAL_PROFILES; i++) {
        cal_profile_t *p = &c->prof[i];
        if (memchr(p->name, 0, PROFILE_NAME_LEN) == NULL) {
            memcpy(p->name, d.prof[i].name, PROFILE_NAME_LEN);
            bad++;
        }
        bad += !sanitize_table(&p->fwd);
        bad += !sanitize_table(&p->ref);
    }
#define FIX(cond, field) do { if (!(cond)) { c->field = d.field; bad++; } } while (0)
    FIX(c->active_prof < CAL_PROFILES, active_prof);
    FIX(c->pep_mode <= 1, pep_mode);
    FIX(c->adc_mode <= ADC_MODE_ADS1115, adc_mode);
    FIX(c->backlight >= 5 && c->backlight <= 100, backlight);
    FIX(float_ok(c->zero_fwd_v, 0.0f, 1.0f), zero_fwd_v);
    FIX(float_ok(c->zero_ref_v, 0.0f, 1.0f), zero_ref_v);
    FIX(float_ok(c->min_power_w, 0.01f, 1000.0f), min_power_w);
    FIX(float_ok(c->alarm_swr, 1.1f, 50.0f), alarm_swr);
    FIX(c->hold_ms <= 10000, hold_ms);
    FIX(float_ok(c->smooth, 0.0f, 0.95f), smooth);
    FIX(float_ok(c->fs_v, 0.05f, 6.5f), fs_v);
    FIX(float_ok(c->fs_w, 0.1f, 10000.0f), fs_w);
#undef FIX
    if (bad) {
        ESP_LOGW(TAG, "poprawiono %d pol konfiguracji spoza zakresu", bad);
    }
}

static void save_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &s_cfg, sizeof(s_cfg));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "zapis NVS nieudany: %s", esp_err_to_name(err));
    }
}

void config_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    config_set_defaults(&s_cfg);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        app_config_t tmp;
        size_t len = sizeof(tmp);
        if (nvs_get_blob(h, NVS_KEY, &tmp, &len) == ESP_OK && len == sizeof(tmp) &&
            tmp.magic == CFG_MAGIC && tmp.version == CFG_VERSION) {
            s_cfg = tmp;
            ESP_LOGI(TAG, "konfiguracja wczytana z NVS");
        } else {
            ESP_LOGW(TAG, "brak/niezgodna konfiguracja w NVS - wartosci domyslne");
        }
        nvs_close(h);
    }
    sanitize(&s_cfg);
}

void config_get(app_config_t *out)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    *out = s_cfg;
    xSemaphoreGive(s_mtx);
}

uint32_t config_revision(void)
{
    return s_rev;
}

app_config_t *config_lock(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    return &s_cfg;
}

void config_unlock(bool save)
{
    if (save) {
        save_nvs();
    }
    s_rev++;
    xSemaphoreGive(s_mtx);
}
