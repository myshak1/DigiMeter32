#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* jedna ramka pomiarowa z ADC (napiecia na wejsciu ADC, przed odjeciem zera) */
typedef struct {
    float fwd_avg, ref_avg;   /* srednia w ramce [V] */
    float fwd_pk,  ref_pk;    /* maksimum w ramce (z krotkiego usrednienia) [V] */
} meas_frame_t;

typedef struct {
    const char *name;                         /* "INT" / "ADS" */
    esp_err_t (*init)(void);
    esp_err_t (*read_frame)(meas_frame_t *f, uint32_t frame_ms);
} meas_backend_t;

extern const meas_backend_t meas_backend_internal;
extern const meas_backend_t meas_backend_ads1115;

typedef struct {
    uint32_t seq;
    float v_fwd_raw, v_ref_raw;   /* srednie napiecie przed odjeciem zera [V] */
    float v_fwd, v_ref;           /* po odjeciu zera [V] */
    float p_fwd_avg, p_ref_avg;   /* moc srednia (wygladzona) [W] */
    float p_fwd_pep, p_ref_pep;   /* moc szczytowa z podtrzymaniem [W] */
    float p_fwd, p_ref;           /* wartosci wg wybranego trybu AVG/PEP */
    float swr, rho;
    float swr_min;                /* najnizszy SWR w biezacej sesji nadawania */
    bool  tx;                     /* wykryto nadawanie */
    bool  swr_valid;              /* swr policzony teraz */
    bool  swr_stale;              /* pokazujemy ostatni SWR po zakonczeniu nadawania */
    bool  alarm;
    bool  uncal;                  /* aktywny profil nie ma kalibracji FWD */
    bool  fault;                  /* brak danych z ADC - wartosci nieaktualne */
    bool  no_adc;                 /* ADS1115 nie wykryty (tryb "tylko ADS") */
    bool  pep_mode;
    const char *backend;
    char  prof_name[12];
} meas_result_t;

esp_err_t meas_start(void);
void      meas_get(meas_result_t *out);
/* usrednia napiecia przez podany czas (raw = przed odjeciem zera) */
esp_err_t meas_average(uint32_t ms, float *vf, float *vr, bool raw);
/* strumien CSV na konsoli; 0 = wylacz */
void      meas_set_stream(uint32_t period_ms);
