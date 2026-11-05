#pragma once
/*
 * Konfiguracja i kalibracja trzymane w NVS (jeden blob).
 */
#include <stdint.h>
#include <stdbool.h>

#define CAL_MAX_POINTS   12
#define CAL_PROFILES     4
#define PROFILE_NAME_LEN 12

typedef struct {
    float v;    /* napiecie na wejsciu ADC po odjeciu zera [V] */
    float w;    /* moc [W] */
} cal_point_t;

typedef struct {
    uint8_t     n;
    cal_point_t pt[CAL_MAX_POINTS];   /* posortowane rosnaco wg v */
} cal_table_t;

typedef struct {
    char        name[PROFILE_NAME_LEN];
    cal_table_t fwd;
    cal_table_t ref;    /* pusta = uzyj tabeli FWD (sprzegacz symetryczny) */
} cal_profile_t;

typedef enum {
    ADC_MODE_AUTO = 0,      /* ADS1115 jesli wykryty, inaczej wbudowany */
    ADC_MODE_INTERNAL = 1,
    ADC_MODE_ADS1115 = 2,
} adc_mode_t;

typedef struct {
    uint32_t      magic;
    uint32_t      version;
    float         zero_fwd_v;
    float         zero_ref_v;
    cal_profile_t prof[CAL_PROFILES];
    uint8_t       active_prof;
    uint8_t       pep_mode;      /* 0 = AVG, 1 = PEP (szczytowa z podtrzymaniem) */
    uint8_t       adc_mode;      /* adc_mode_t */
    uint8_t       backlight;     /* 5..100 % */
    float         min_power_w;   /* ponizej: brak nadawania, SWR nie liczony */
    float         alarm_swr;     /* prog alarmu */
    uint16_t      hold_ms;       /* podtrzymanie wartosci szczytowej */
    float         smooth;        /* 0 = brak, 0.95 = mocne usrednianie */
    float         fs_v;          /* model bez kalibracji: V przy pelnej skali */
    float         fs_w;          /*                       W przy pelnej skali */
} app_config_t;

void  config_init(void);
/* kopia biezacej konfiguracji (bezpieczne z kazdego zadania) */
void  config_get(app_config_t *out);
/* licznik zmian - pozwala tanio sprawdzic, czy trzeba odswiezyc kopie */
uint32_t config_revision(void);
/* edycja: lock zwraca wskaznik, unlock(save) opcjonalnie zapisuje do NVS */
app_config_t *config_lock(void);
void  config_unlock(bool save);
void  config_set_defaults(app_config_t *c);
