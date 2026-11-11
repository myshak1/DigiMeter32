/*
 * Konsola szeregowa (USB) do kalibracji i ustawien.
 * Wpisz "help", zeby zobaczyc liste polecen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "esp_console.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "button.h"
#include "calib.h"
#include "meas.h"
#include "lcd_ili948x.h"
#include "console_cmds.h"

#define CAPTURE_MS 2000

static const char *s_adc_names[] = { "auto", "int", "ads" };

static cal_table_t *table_by_name(app_config_t *c, const char *ch)
{
    cal_profile_t *p = &c->prof[c->active_prof];
    if (!strcasecmp(ch, "fwd")) {
        return &p->fwd;
    }
    if (!strcasecmp(ch, "ref")) {
        return &p->ref;
    }
    return NULL;
}

static void print_table(const char *name, const cal_table_t *t)
{
    printf("  %s: %d pkt\n", name, t->n);
    for (int i = 0; i < t->n; i++) {
        printf("    [%d] %.4f V -> %.2f W\n", i, t->pt[i].v, t->pt[i].w);
    }
}

static const char *cal_err(int e)
{
    switch (e) {
    case -1: return "tabela pelna (max 12 punktow)";
    case -2: return "bledne wartosci (napiecie za male lub moc <= 0)";
    case -3: return "punkt zaburza monotonicznosc (wieksza moc musi dawac wieksze napiecie)";
    default: return "blad";
    }
}

/* ---------------- status ---------------- */
static int cmd_status(int argc, char **argv)
{
    meas_result_t r;
    meas_get(&r);
    app_config_t c;
    config_get(&c);
    printf("ADC: %s   LCD: %s   profil %d '%s'   tryb %s\n", r.backend, lcd_driver_name(),
           c.active_prof + 1, r.prof_name, r.pep_mode ? "PEP" : "AVG");
    if (r.no_adc) {
        printf("BRAK ADS1115 - sprawdz zasilanie 3,3 V, SDA=%d, SCL=%d, ADDR do GND\n",
               CONFIG_DM32_I2C_SDA, CONFIG_DM32_I2C_SCL);
    }
    if (r.fault) {
        printf("BLAD: brak danych z ADC - ponizsze wartosci sa nieaktualne\n");
    }
    printf("U FWD: %.4f V (surowe %.4f, zero %.4f)\n", r.v_fwd, r.v_fwd_raw, c.zero_fwd_v);
    printf("U REF: %.4f V (surowe %.4f, zero %.4f)\n", r.v_ref, r.v_ref_raw, c.zero_ref_v);
    printf("P FWD: %.2f W avg, %.2f W pep\n", r.p_fwd_avg, r.p_fwd_pep);
    printf("P REF: %.2f W avg, %.2f W pep\n", r.p_ref_avg, r.p_ref_pep);
    if (r.swr_valid) {
        printf("SWR:   %.2f  (rho %.3f)\n", r.swr, r.rho);
    } else {
        printf("SWR:   --- (brak nadawania lub moc < %.2f W)\n", c.min_power_w);
    }
    if (r.uncal) {
        printf("UWAGA: profil bez kalibracji FWD - moc liczona z modelu %.2f V = %.0f W\n", c.fs_v, c.fs_w);
    }
    return 0;
}

/* ---------------- zero ---------------- */
static int cmd_zero(int argc, char **argv)
{
    printf("Pomiar zera (bez nadawania!) przez %d ms...\n", CAPTURE_MS);
    float vf, vr;
    if (meas_average(CAPTURE_MS, &vf, &vr, true) != ESP_OK) {
        printf("blad pomiaru - brak odczytu z przetwornika (czy ADS1115 jest podlaczony?)\n");
        return 1;
    }
    if (vf > 0.2f || vr > 0.2f) {
        printf("Napiecia za duze (%.3f / %.3f V) - czy nadajnik jest wylaczony?\n", vf, vr);
        return 1;
    }
    app_config_t *c = config_lock();
    c->zero_fwd_v = vf;
    c->zero_ref_v = vr;
    config_unlock(true);
    printf("Zero zapisane: FWD %.4f V, REF %.4f V\n", vf, vr);
    return 0;
}

/* ---------------- cal ---------------- */
static int cmd_cal(int argc, char **argv)
{
    if (argc < 2 || !strcasecmp(argv[1], "list")) {
        app_config_t c;
        config_get(&c);
        const cal_profile_t *p = &c.prof[c.active_prof];
        printf("Profil %d '%s'\n", c.active_prof + 1, p->name);
        print_table("FWD", &p->fwd);
        print_table("REF", &p->ref);
        if (p->ref.n == 0) {
            printf("  (REF pusty - uzywana tabela FWD)\n");
        }
        return 0;
    }

    /* cal fwd|ref <W> - pomiar przy nadawaniu znana moca */
    if (argc == 3 && (!strcasecmp(argv[1], "fwd") || !strcasecmp(argv[1], "ref"))) {
        float w = strtof(argv[2], NULL);
        printf("Nadawaj teraz %.2f W (nosna). Pomiar %d ms...\n", w, CAPTURE_MS);
        float vf, vr;
        if (meas_average(CAPTURE_MS, &vf, &vr, false) != ESP_OK) {
            printf("blad pomiaru - brak odczytu z przetwornika (czy ADS1115 jest podlaczony?)\n");
            return 1;
        }
        bool fwd = !strcasecmp(argv[1], "fwd");
        float v = fwd ? vf : vr;
        if (v < 0.005f) {
            printf("Brak sygnalu na kanale %s (%.4f V)\n", argv[1], v);
            if (!fwd) {
                printf("Przy kalibracji REF zamien gniazda TX i ANT miernika!\n");
            }
            return 1;
        }
        app_config_t *c = config_lock();
        int e = cal_add_point(table_by_name(c, argv[1]), v, w);
        config_unlock(e == 0);
        if (e) {
            printf("Odrzucono: %s\n", cal_err(e));
            return 1;
        }
        printf("Dodano %s: %.4f V -> %.2f W\n", argv[1], v, w);
        return 0;
    }

    /* cal scale <W> - jeden punkt przeskalowuje cala krzywa aktywnego profilu */
    if (argc == 3 && !strcasecmp(argv[1], "scale")) {
        float w = strtof(argv[2], NULL);
        if (!(w > 0.0f)) {
            printf("Podaj moc w W, np. cal scale 10\n");
            return 1;
        }
        app_config_t c;
        config_get(&c);
        if (c.prof[c.active_prof].fwd.n == 0) {
            printf("Aktywny profil nie ma krzywej FWD. Najpierw: profile copy <z> %d\n",
                   c.active_prof + 1);
            return 1;
        }
        printf("Nadawaj teraz %.2f W (nosna). Pomiar %d ms...\n", w, CAPTURE_MS);
        float vf, vr;
        if (meas_average(CAPTURE_MS, &vf, &vr, false) != ESP_OK) {
            printf("blad pomiaru - brak odczytu z przetwornika (czy ADS1115 jest podlaczony?)\n");
            return 1;
        }
        const cal_profile_t *p = &c.prof[c.active_prof];
        float vpred = cal_watts_to_volts(&p->fwd, w, c.fs_v, c.fs_w);
        if (vf < 0.005f || vpred < 0.005f) {
            printf("Brak sygnalu FWD (%.4f V)\n", vf);
            return 1;
        }
        float k = vf / vpred;
        if (k < 0.3f || k > 3.0f) {
            printf("Wspolczynnik %.3f poza zakresem 0.3-3 - sprawdz moc i profil\n", k);
            return 1;
        }
        app_config_t *cw = config_lock();
        cal_scale_volts(&cw->prof[cw->active_prof].fwd, k);
        cal_scale_volts(&cw->prof[cw->active_prof].ref, k);
        config_unlock(true);
        printf("Zmierzono %.4f V, krzywa przewidywala %.4f V. Przeskalowano x%.3f "
               "(moc wg starej krzywej: %.2f W)\n", vf, vpred, k,
               cal_volts_to_watts(&p->fwd, vf, c.fs_v, c.fs_w));
        return 0;
    }

    /* cal set fwd|ref <V> <W> - wpis reczny */
    if (argc == 5 && !strcasecmp(argv[1], "set")) {
        app_config_t *c = config_lock();
        cal_table_t *t = table_by_name(c, argv[2]);
        int e = t ? cal_add_point(t, strtof(argv[3], NULL), strtof(argv[4], NULL)) : -2;
        config_unlock(e == 0);
        if (e) {
            printf("Odrzucono: %s\n", cal_err(e));
        }
        return e ? 1 : 0;
    }

    /* cal del fwd|ref <n> */
    if (argc == 4 && !strcasecmp(argv[1], "del")) {
        app_config_t *c = config_lock();
        cal_table_t *t = table_by_name(c, argv[2]);
        int e = t ? cal_del_point(t, atoi(argv[3])) : -2;
        config_unlock(e == 0);
        printf(e ? "Nie ma takiego punktu\n" : "Usunieto\n");
        return e ? 1 : 0;
    }

    /* cal clear fwd|ref|all */
    if (argc == 3 && !strcasecmp(argv[1], "clear")) {
        app_config_t *c = config_lock();
        cal_profile_t *p = &c->prof[c->active_prof];
        bool all = !strcasecmp(argv[2], "all");
        if (all || !strcasecmp(argv[2], "fwd")) {
            p->fwd.n = 0;
        }
        if (all || !strcasecmp(argv[2], "ref")) {
            p->ref.n = 0;
        }
        config_unlock(true);
        printf("Wyczyszczono\n");
        return 0;
    }

    printf("Uzycie: cal [list] | cal fwd|ref <W> | cal set fwd|ref <V> <W> | "
           "cal del fwd|ref <n> | cal clear fwd|ref|all | cal scale <W>\n");
    return 1;
}

/* ---------------- profile ---------------- */
static int cmd_profile(int argc, char **argv)
{
    if (argc == 1) {
        app_config_t c;
        config_get(&c);
        for (int i = 0; i < CAL_PROFILES; i++) {
            printf("%c %d '%s'  FWD %d pkt, REF %d pkt\n", i == c.active_prof ? '*' : ' ', i + 1,
                   c.prof[i].name, c.prof[i].fwd.n, c.prof[i].ref.n);
        }
        return 0;
    }
    /* profile copy <z> <do> - kopiuje kalibracje, nazwa docelowa zostaje */
    if (argc == 4 && !strcasecmp(argv[1], "copy")) {
        int src = atoi(argv[2]) - 1, dst = atoi(argv[3]) - 1;
        if (src < 0 || src >= CAL_PROFILES || dst < 0 || dst >= CAL_PROFILES || src == dst) {
            printf("Uzycie: profile copy <1-%d> <1-%d>\n", CAL_PROFILES, CAL_PROFILES);
            return 1;
        }
        app_config_t *c = config_lock();
        c->prof[dst].fwd = c->prof[src].fwd;
        c->prof[dst].ref = c->prof[src].ref;
        config_unlock(true);
        printf("Skopiowano kalibracje profilu %d do %d\n", src + 1, dst + 1);
        return 0;
    }
    int n = atoi(argv[1]) - 1;
    if (n < 0 || n >= CAL_PROFILES) {
        printf("Profil 1..%d\n", CAL_PROFILES);
        return 1;
    }
    app_config_t *c = config_lock();
    if (argc >= 4 && !strcasecmp(argv[2], "name")) {
        strncpy(c->prof[n].name, argv[3], PROFILE_NAME_LEN - 1);
        c->prof[n].name[PROFILE_NAME_LEN - 1] = 0;
    } else {
        c->active_prof = n;
    }
    config_unlock(true);
    printf("OK\n");
    return 0;
}

/* ---------------- set ---------------- */
static void print_settings(const app_config_t *c)
{
    printf("mode   %s%s\n", c->pep_mode ? "pep" : "avg",
           button_mode_switch_present() ? "  (przelacznik)" : "");
    printf("minpwr %.2f W\n", c->min_power_w);
    printf("alarm  %.2f\n", c->alarm_swr);
    printf("hold   %u ms\n", c->hold_ms);
    printf("smooth %.2f\n", c->smooth);
    printf("bl     %u %%\n", c->backlight);
    printf("adc    %s\n", s_adc_names[c->adc_mode < 3 ? c->adc_mode : 0]);
    printf("fs     %.3f V %.0f W  (model bez kalibracji)\n", c->fs_v, c->fs_w);
}

/* liczba z zakresu [lo, hi]; caly argument musi byc liczba */
static bool parse_float(const char *s, float lo, float hi, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != 0 || !isfinite(v) || v < lo || v > hi) {
        printf("Bledna wartosc '%s' - dozwolone %g..%g\n", s, lo, hi);
        return false;
    }
    *out = v;
    return true;
}

static int cmd_set(int argc, char **argv)
{
    if (argc == 1) {
        app_config_t c;
        config_get(&c);
        print_settings(&c);
        return 0;
    }
    const char *k = argv[1];
    if (argc < 3) {
        printf("Brak wartosci\n");
        return 1;
    }

    /* najpierw walidacja, potem jeden zapis pod blokada */
    float v = 0, v2 = 0;
    int m = -1;
    bool known = true, valid = true;
    if (!strcasecmp(k, "mode")) {
        if (button_mode_switch_present()) {
            printf("Tryb AVG/PEP ustawia przelacznik (GPIO%d).\n", CONFIG_DM32_MODE_SW_GPIO);
            return 1;
        }
        m = !strcasecmp(argv[2], "pep") ? 1 : !strcasecmp(argv[2], "avg") ? 0 : -1;
        valid = m >= 0;
    } else if (!strcasecmp(k, "minpwr")) {
        valid = parse_float(argv[2], 0.01f, 1000.0f, &v);
    } else if (!strcasecmp(k, "alarm")) {
        valid = parse_float(argv[2], 1.1f, 50.0f, &v);
    } else if (!strcasecmp(k, "hold")) {
        valid = parse_float(argv[2], 0.0f, 10000.0f, &v);
    } else if (!strcasecmp(k, "smooth")) {
        valid = parse_float(argv[2], 0.0f, 0.95f, &v);
    } else if (!strcasecmp(k, "bl")) {
        valid = parse_float(argv[2], 5.0f, 100.0f, &v);
    } else if (!strcasecmp(k, "adc")) {
        for (int i = 0; i < 3; i++) {
            if (!strcasecmp(argv[2], s_adc_names[i])) {
                m = i;
            }
        }
        valid = m >= 0;
    } else if (!strcasecmp(k, "fs")) {
        valid = argc >= 4 && parse_float(argv[2], 0.05f, 6.5f, &v) &&
                parse_float(argv[3], 0.1f, 10000.0f, &v2);
    } else {
        known = false;
    }
    if (!known || !valid) {
        printf("Nieznane ustawienie lub wartosc. Wpisz 'help set'.\n");
        return 1;
    }

    bool restart = false;
    app_config_t *c = config_lock();
    if (!strcasecmp(k, "mode")) {
        c->pep_mode = m;
    } else if (!strcasecmp(k, "minpwr")) {
        c->min_power_w = v;
    } else if (!strcasecmp(k, "alarm")) {
        c->alarm_swr = v;
    } else if (!strcasecmp(k, "hold")) {
        c->hold_ms = (uint16_t)v;
    } else if (!strcasecmp(k, "smooth")) {
        c->smooth = v;
    } else if (!strcasecmp(k, "bl")) {
        c->backlight = (uint8_t)v;
        lcd_set_backlight(c->backlight);
#if CONFIG_DM32_LCD_PIN_BL < 0
        printf("Uwaga: podswietlenie jest na stale (GPIO podswietlenia = -1), jasnosc nie zmieni sie.\n");
#endif
    } else if (!strcasecmp(k, "adc")) {
        c->adc_mode = m;
        restart = true;
    } else {
        c->fs_v = v;
        c->fs_w = v2;
    }
    config_unlock(true);
    printf("OK\n");
    if (restart) {
        printf("Restart, zeby zmienic zrodlo pomiaru...\n");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
    return 0;
}

/* ---------------- stream / export / defaults / restart ---------------- */
static int cmd_stream(int argc, char **argv)
{
    if (argc < 2 || !strcasecmp(argv[1], "off")) {
        meas_set_stream(0);
        return 0;
    }
    int ms = atoi(argv[1]);
    if (ms < 20) {
        ms = 20;
    }
    printf("# D,czas_ms,U_fwd_raw,U_ref_raw,U_fwd,U_ref,P_fwd,P_ref,SWR\n");
    meas_set_stream(ms);
    return 0;
}

static int cmd_export(int argc, char **argv)
{
    app_config_t c;
    config_get(&c);
    printf("# kopia ustawien - wklej z powrotem do konsoli, zeby odtworzyc\n");
    printf("set minpwr %.2f\nset alarm %.2f\nset hold %u\nset smooth %.2f\nset bl %u\nset fs %.3f %.1f\n",
           c.min_power_w, c.alarm_swr, c.hold_ms, c.smooth, c.backlight, c.fs_v, c.fs_w);
    for (int i = 0; i < CAL_PROFILES; i++) {
        const cal_profile_t *p = &c.prof[i];
        printf("profile %d name %s\nprofile %d\ncal clear all\n", i + 1, p->name, i + 1);
        for (int j = 0; j < p->fwd.n; j++) {
            printf("cal set fwd %.5f %.3f\n", p->fwd.pt[j].v, p->fwd.pt[j].w);
        }
        for (int j = 0; j < p->ref.n; j++) {
            printf("cal set ref %.5f %.3f\n", p->ref.pt[j].v, p->ref.pt[j].w);
        }
    }
    printf("profile %d\n", c.active_prof + 1);
    printf("# zero: FWD %.5f V, REF %.5f V (wykonaj 'zero' ponownie po przywroceniu)\n",
           c.zero_fwd_v, c.zero_ref_v);
    return 0;
}

static int cmd_defaults(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "yes")) {
        printf("To skasuje cala kalibracje. Potwierdz: defaults yes\n");
        return 1;
    }
    app_config_t *c = config_lock();
    config_set_defaults(c);
    config_unlock(true);
    lcd_set_backlight(100);
    printf("Przywrocono ustawienia domyslne\n");
    return 0;
}

static int cmd_restart(int argc, char **argv)
{
    esp_restart();
    return 0;
}

esp_err_t console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "dm32>";
    rc.max_cmdline_length = 128;

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&hw, &rc, &repl), "console", "repl");
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl), "console", "repl");
#else
#error "Nieobslugiwany typ konsoli"
#endif

    const esp_console_cmd_t cmds[] = {
        { .command = "status", .help = "Biezace napiecia, moce i SWR", .func = cmd_status },
        { .command = "zero", .help = "Zapamietaj napiecia spoczynkowe (bez nadawania)", .func = cmd_zero },
        { .command = "cal",
          .help = "Kalibracja aktywnego profilu:\n"
                  "  cal                      - lista punktow\n"
                  "  cal fwd <W>              - nadawaj znana moca i zapisz punkt FWD\n"
                  "  cal ref <W>              - jw. dla REF (zamien gniazda TX/ANT!)\n"
                  "  cal set fwd|ref <V> <W>  - wpisz punkt recznie\n"
                  "  cal del fwd|ref <n>      - usun punkt\n"
                  "  cal clear fwd|ref|all    - wyczysc\n"
                  "  cal scale <W>            - nadawaj znana moca; przeskaluj cala krzywa",
          .func = cmd_cal },
        { .command = "profile",
          .help = "Profile kalibracji (np. pasma):\n"
                  "  profile               - lista\n"
                  "  profile <1-4>         - wybierz\n"
                  "  profile <1-4> name X  - nazwij\n"
                  "  profile copy <z> <do> - skopiuj kalibracje",
          .func = cmd_profile },
        { .command = "set",
          .help = "Ustawienia (bez argumentow - wyswietl):\n"
                  "  set mode avg|pep     set minpwr <W>     set alarm <SWR>\n"
                  "  set hold <ms>        set smooth <0-0.95>  set bl <5-100>\n"
                  "  set adc auto|int|ads (restart)   set fs <V> <W>",
          .func = cmd_set },
        { .command = "stream", .help = "stream <ms>|off - wysylaj pomiary jako CSV", .func = cmd_stream },
        { .command = "export", .help = "Wypisz kalibracje i ustawienia jako polecenia (kopia)", .func = cmd_export },
        { .command = "defaults", .help = "defaults yes - ustawienia fabryczne", .func = cmd_defaults },
        { .command = "restart", .help = "Restart urzadzenia", .func = cmd_restart },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmds[i]), "console", "register");
    }
    esp_console_register_help_command();
    return esp_console_start_repl(repl);
}
