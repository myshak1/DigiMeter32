/*
 * Ekran 480x320 w stylu przyrzadu pomiarowego: odczyty siedmiosegmentowe
 * (DSEG7) z wygaszonymi segmentami w tle, opisy Barlow Semi Condensed,
 * annuncjatory statusu u gory i parametry pomocnicze u dolu.
 *
 *   FORWARD | SWR            - odczyty glowne
 *   REFLECTED | RETURN LOSS  - odczyty pomocnicze
 *   NET | PEP/AVG | SWR MIN | MISMATCH
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lvgl.h"
#include "meas.h"
#include "ui.h"

LV_FONT_DECLARE(font_seg_90);
LV_FONT_DECLARE(font_seg_60);
LV_FONT_DECLARE(font_lbl_15);
LV_FONT_DECLARE(font_val_22);

#define C_BG       0x000000
#define C_HAIR     0x2A2A2A
#define C_GHOST    0x1F1709     /* wygaszone segmenty */
#define C_SEG      0xFFB020     /* odczyt */
#define C_SEG_DIM  0x7A5614     /* ostatni SWR po zakonczeniu nadawania */
#define C_RED      0xFF3B30
#define C_LABEL    0x8E949C
#define C_UNIT     0x5E646B
#define C_TEXT     0xDDE1E6
#define C_ANN_OFF  0x242424     /* zgaszony annuncjator */

#define PAD        16           /* margines wewnatrz kolumny */
#define TOP_H      30
#define ROW_A_Y    TOP_H        /* FORWARD / SWR */
#define ROW_B_Y    162          /* REFLECTED / RETURN LOSS */
#define ROW_C_Y    264          /* pasek dolny */
#define COL_SPLIT  240

/* zgodnosc LVGL 9.3-9.5 i 9.6+ (nowe settery flag) */
#if LVGL_VERSION_MAJOR > 9 || (LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 6)
#define UI_STATIC(o)     do { lv_obj_set_scrollable((o), false); lv_obj_set_clickable((o), false); } while (0)
#else
#define UI_STATIC(o)     lv_obj_remove_flag((o), LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE)
#endif

/* odczyt siedmiosegmentowy: 3 cyfry, kropki maja zerowa szerokosc,
 * wiec napis "8.8.8" pod spodem pokrywa kazda wartosc */
typedef struct {
    lv_obj_t *ghost, *val;
    char      last[8];
    uint32_t  col;
} readout_t;

typedef struct {
    lv_obj_t *l;
    uint32_t  col_on;
    int       on;
} ann_t;

static readout_t s_fwd, s_swr, s_ref, s_rl;
static ann_t     a_avg, a_pep, a_uncal, a_err, a_alarm, a_tx;
static lv_obj_t *s_lamp, *s_prof, *s_src;
static lv_obj_t *s_bot_lbl[4], *s_bot_val[4];
static char      s_bot_last[4][16], s_alt_last[4];
static char      s_prof_last[16], s_src_last[8];
static int       s_lamp_on = -1;

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    return l;
}

/* opis wielkimi literami z rozstrzeleniem, jak na plycie czolowej */
static lv_obj_t *mk_caption(lv_obj_t *parent, int x, int y, uint32_t color, const char *txt)
{
    lv_obj_t *l = mk_label(parent, &font_lbl_15, color, txt);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *mk_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    UI_STATIC(o);
    return o;
}

static void set_text_if(lv_obj_t *l, char *cache, size_t cache_len, const char *txt)
{
    if (strncmp(cache, txt, cache_len) != 0) {
        snprintf(cache, cache_len, "%s", txt);
        lv_label_set_text(l, txt);
    }
}

/* naglowek pola: opis po lewej, jednostka przy prawej krawedzi kolumny */
static void field_header(lv_obj_t *scr, int x0, int x1, int y, const char *name, const char *unit)
{
    mk_caption(scr, x0 + PAD, y, C_LABEL, name);
    if (unit[0]) {
        lv_obj_t *u = mk_caption(scr, 0, y, C_UNIT, unit);
        lv_obj_set_width(u, 60);
        lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_x(u, x1 - PAD - 60);
    }
}

/* odczyt wyrownany do prawej krawedzi kolumny (wg zapalonych segmentow) */
static void readout_create(readout_t *r, lv_obj_t *scr, int x1, int y, const lv_font_t *font)
{
    lv_font_glyph_dsc_t g;
    lv_font_get_glyph_dsc(font, &g, '8', 0);
    int rbear = g.adv_w - g.ofs_x - g.box_w;
    int w = 3 * g.adv_w;
    int x = x1 - PAD + rbear - w;

    r->ghost = mk_label(scr, font, C_GHOST, "8.8.8");
    r->val = mk_label(scr, font, C_SEG, "");
    lv_obj_t *o[2] = { r->ghost, r->val };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_width(o[i], w);
        lv_obj_set_style_text_align(o[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(o[i], x, y);
    }
    r->last[0] = 0;
    r->col = C_SEG;
}

static void readout_set(readout_t *r, const char *txt, uint32_t col)
{
    set_text_if(r->val, r->last, sizeof(r->last), txt);
    if (col != r->col) {
        r->col = col;
        lv_obj_set_style_text_color(r->val, lv_color_hex(col), 0);
    }
}

static void ann_create(ann_t *a, lv_obj_t *parent, const char *txt, uint32_t col_on)
{
    a->l = mk_label(parent, &font_lbl_15, C_ANN_OFF, txt);
    lv_obj_set_style_text_letter_space(a->l, 1, 0);
    a->col_on = col_on;
    a->on = 0;
}

static void ann_set(ann_t *a, bool on)
{
    if ((int)on != a->on) {
        a->on = on;
        lv_obj_set_style_text_color(a->l, lv_color_hex(on ? a->col_on : C_ANN_OFF), 0);
    }
}

/* 3 cyfry znaczace: 0.00-9.99, 10.0-99.9, 100-999 */
static void fmt3(char *buf, size_t n, float v)
{
    if (!(v > 0.0f)) {
        v = 0.0f;
    }
    if (v < 9.995f) {
        snprintf(buf, n, "%.2f", v);
    } else if (v < 99.95f) {
        snprintf(buf, n, "%.1f", v);
    } else if (v < 999.5f) {
        snprintf(buf, n, "%.0f", v);
    } else {
        snprintf(buf, n, "999");
    }
}

static void fmt_power(char *buf, size_t n, float p)
{
    if (p < 0.05f) {
        p = 0.0f;               /* ponizej zakresu - nie pokazuj szumu */
    }
    fmt3(buf, n, p);
}

static void ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    meas_result_t r;
    meas_get(&r);
    if (r.seq == 0) {
        return;
    }
    char buf[16];

    /* --- annuncjatory --- */
    ann_set(&a_avg, !r.pep_mode);
    ann_set(&a_pep, r.pep_mode);
    ann_set(&a_uncal, r.uncal);
    ann_set(&a_err, r.fault || r.no_adc);
    ann_set(&a_alarm, r.alarm);
    ann_set(&a_tx, r.tx);
    if ((int)r.tx != s_lamp_on) {
        s_lamp_on = r.tx;
        lv_obj_set_style_bg_color(s_lamp, lv_color_hex(r.tx ? C_RED : C_ANN_OFF), 0);
    }
    set_text_if(s_prof, s_prof_last, sizeof(s_prof_last), r.prof_name);
    set_text_if(s_src, s_src_last, sizeof(s_src_last), r.backend ? r.backend : "");

    /* --- moc --- */
    fmt_power(buf, sizeof(buf), r.p_fwd);
    readout_set(&s_fwd, buf, C_SEG);
    fmt_power(buf, sizeof(buf), r.p_ref);
    readout_set(&s_ref, buf, C_SEG);

    /* --- SWR i straty odbiciowe --- */
    bool show = r.swr_valid || r.swr_stale;
    uint32_t col = r.alarm ? C_RED : (r.swr_valid ? C_SEG : C_SEG_DIM);
    if (show) {
        fmt3(buf, sizeof(buf), fminf(r.swr, 99.9f));
        readout_set(&s_swr, buf, col);
    } else {
        readout_set(&s_swr, "-.--", C_SEG_DIM);
    }
    if (show && r.rho > 0.001f) {
        fmt3(buf, sizeof(buf), fminf(-20.0f * log10f(r.rho), 99.9f));
        readout_set(&s_rl, buf, r.swr_valid ? C_SEG : C_SEG_DIM);
    } else {
        readout_set(&s_rl, "--.-", C_SEG_DIM);
    }

    /* --- pasek dolny --- */
    char b[4][16], p[8];
    fmt_power(p, sizeof(p), fmaxf(r.p_fwd - r.p_ref, 0.0f));
    snprintf(b[0], sizeof(b[0]), "%s W", p);
    fmt_power(p, sizeof(p), r.pep_mode ? r.p_fwd_avg : r.p_fwd_pep);
    snprintf(b[1], sizeof(b[1]), "%s W", p);
    if (r.swr_min < 50.0f) {
        snprintf(b[2], sizeof(b[2]), "%.2f", r.swr_min);
    } else {
        snprintf(b[2], sizeof(b[2]), "--");
    }
    if (show && r.rho > 0.001f) {
        snprintf(b[3], sizeof(b[3]), "%.2f dB", -10.0f * log10f(1.0f - r.rho * r.rho));
    } else {
        snprintf(b[3], sizeof(b[3]), "-- dB");
    }
    for (int i = 0; i < 4; i++) {
        set_text_if(s_bot_val[i], s_bot_last[i], sizeof(s_bot_last[i]), b[i]);
    }
    /* drugi tryb niz wybrany: w AVG pokazujemy PEP i odwrotnie */
    set_text_if(s_bot_lbl[1], s_alt_last, sizeof(s_alt_last), r.pep_mode ? "AVG" : "PEP");
}

static lv_obj_t *mk_row(lv_obj_t *parent, int x, int w, lv_flex_align_t main)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, 0);
    lv_obj_set_size(o, w, TOP_H);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, 14, 0);
    UI_STATIC(o);
    return o;
}

void ui_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    UI_STATIC(scr);

    /* --- annuncjatory --- */
    lv_obj_t *left = mk_row(scr, PAD, 220, LV_FLEX_ALIGN_START);
    ann_create(&a_avg, left, "AVG", C_TEXT);
    ann_create(&a_pep, left, "PEP", C_TEXT);
    s_prof = mk_label(left, &font_lbl_15, C_TEXT, "");
    lv_obj_set_style_text_letter_space(s_prof, 1, 0);
    s_src = mk_label(left, &font_lbl_15, C_UNIT, "");
    lv_obj_set_style_text_letter_space(s_src, 1, 0);

    lv_obj_t *right = mk_row(scr, 480 - PAD - 240, 240, LV_FLEX_ALIGN_END);
    ann_create(&a_uncal, right, "UNCAL", C_SEG);
    ann_create(&a_err, right, "ADC ERR", C_RED);
    ann_create(&a_alarm, right, "ALARM", C_RED);
    s_lamp = lv_obj_create(right);
    lv_obj_remove_style_all(s_lamp);
    lv_obj_set_size(s_lamp, 8, 8);
    lv_obj_set_style_radius(s_lamp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_lamp, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_lamp, lv_color_hex(C_ANN_OFF), 0);
    lv_obj_set_style_margin_right(s_lamp, -8, 0);
    UI_STATIC(s_lamp);
    ann_create(&a_tx, right, "TX", C_RED);

    /* --- linie podzialu --- */
    mk_rect(scr, 0, TOP_H, 480, 1, C_HAIR);
    mk_rect(scr, 0, ROW_B_Y, 480, 1, C_HAIR);
    mk_rect(scr, 0, ROW_C_Y, 480, 1, C_HAIR);
    mk_rect(scr, COL_SPLIT, TOP_H, 1, ROW_C_Y - TOP_H, C_HAIR);
    for (int i = 1; i < 4; i++) {
        mk_rect(scr, i * 120, ROW_C_Y + 8, 1, 320 - ROW_C_Y - 16, C_HAIR);
    }

    /* --- odczyty glowne --- */
    field_header(scr, 0, COL_SPLIT, ROW_A_Y + 9, "FORWARD", "W");
    field_header(scr, COL_SPLIT, 480, ROW_A_Y + 9, "SWR", "");
    readout_create(&s_fwd, scr, COL_SPLIT, ROW_A_Y + 34, &font_seg_90);
    readout_create(&s_swr, scr, 480, ROW_A_Y + 34, &font_seg_90);

    /* --- odczyty pomocnicze --- */
    field_header(scr, 0, COL_SPLIT, ROW_B_Y + 9, "REFLECTED", "W");
    field_header(scr, COL_SPLIT, 480, ROW_B_Y + 9, "RETURN LOSS", "dB");
    readout_create(&s_ref, scr, COL_SPLIT, ROW_B_Y + 32, &font_seg_60);
    readout_create(&s_rl, scr, 480, ROW_B_Y + 32, &font_seg_60);

    /* --- pasek dolny --- */
    static const char *names[4] = { "NET", "PEP", "SWR MIN", "MISMATCH" };
    for (int i = 0; i < 4; i++) {
        s_bot_lbl[i] = mk_caption(scr, i * 120 + PAD, ROW_C_Y + 7, C_LABEL, names[i]);
        s_bot_val[i] = mk_label(scr, &font_val_22, C_TEXT, "");
        lv_obj_set_pos(s_bot_val[i], i * 120 + PAD, ROW_C_Y + 25);
        s_bot_last[i][0] = 0;
    }
    strcpy(s_alt_last, "PEP");

    lv_timer_create(ui_timer_cb, 50, NULL);
}
