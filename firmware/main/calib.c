#include <math.h>
#include <string.h>
#include "calib.h"

float cal_volts_to_watts(const cal_table_t *t, float v, float fs_v, float fs_w)
{
    if (!(v > 0.0f)) {
        return 0.0f;
    }
    if (t == NULL || t->n == 0) {
        float r = v / (fs_v > 0.01f ? fs_v : 2.4f);
        return fs_w * r * r;
    }

    float x0 = 0.0f, y0 = 0.0f;
    for (int i = 0; i < t->n; i++) {
        float x1 = t->pt[i].v;
        float y1 = sqrtf(t->pt[i].w);
        if (v <= x1) {
            float y = y0 + (y1 - y0) * (v - x0) / (x1 - x0);
            return y * y;
        }
        x0 = x1;
        y0 = y1;
    }

    /* ekstrapolacja nachyleniem ostatniego odcinka */
    int n = t->n;
    float xl = t->pt[n - 1].v, yl = sqrtf(t->pt[n - 1].w);
    float xp = (n >= 2) ? t->pt[n - 2].v : 0.0f;
    float yp = (n >= 2) ? sqrtf(t->pt[n - 2].w) : 0.0f;
    float slope = (yl - yp) / (xl - xp);
    float y = yl + slope * (v - xl);
    return y * y;
}

float cal_watts_to_volts(const cal_table_t *t, float w, float fs_v, float fs_w)
{
    if (!(w > 0.0f)) {
        return 0.0f;
    }
    /* funkcja V->P jest rosnaca, wiec wystarczy bisekcja */
    float lo = 0.0f, hi = 0.1f;
    while (cal_volts_to_watts(t, hi, fs_v, fs_w) < w && hi < 100.0f) {
        hi *= 2.0f;
    }
    for (int i = 0; i < 40; i++) {
        float mid = 0.5f * (lo + hi);
        if (cal_volts_to_watts(t, mid, fs_v, fs_w) < w) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5f * (lo + hi);
}

void cal_scale_volts(cal_table_t *t, float k)
{
    for (int i = 0; i < t->n; i++) {
        t->pt[i].v *= k;
    }
}

static bool is_monotonic(const cal_table_t *t)
{
    for (int i = 1; i < t->n; i++) {
        if (!(t->pt[i].v > t->pt[i - 1].v) || !(t->pt[i].w > t->pt[i - 1].w)) {
            return false;
        }
    }
    return true;
}

int cal_add_point(cal_table_t *t, float v, float w)
{
    if (!(v > 0.002f) || !(w > 0.0f) || !isfinite(v) || !isfinite(w)) {
        return -2;
    }
    cal_table_t tmp = *t;

    /* punkt o tej samej mocy (+-1%) zastepuje stary */
    for (int i = 0; i < tmp.n; i++) {
        if (fabsf(tmp.pt[i].w - w) <= 0.01f * w) {
            memmove(&tmp.pt[i], &tmp.pt[i + 1], (tmp.n - i - 1) * sizeof(cal_point_t));
            tmp.n--;
            break;
        }
    }
    if (tmp.n >= CAL_MAX_POINTS) {
        return -1;
    }
    int pos = 0;
    while (pos < tmp.n && tmp.pt[pos].v < v) {
        pos++;
    }
    memmove(&tmp.pt[pos + 1], &tmp.pt[pos], (tmp.n - pos) * sizeof(cal_point_t));
    tmp.pt[pos].v = v;
    tmp.pt[pos].w = w;
    tmp.n++;

    if (!is_monotonic(&tmp)) {
        return -3;
    }
    *t = tmp;
    return 0;
}

int cal_del_point(cal_table_t *t, int idx)
{
    if (idx < 0 || idx >= t->n) {
        return -2;
    }
    memmove(&t->pt[idx], &t->pt[idx + 1], (t->n - idx - 1) * sizeof(cal_point_t));
    t->n--;
    return 0;
}

float cal_swr(float pf, float pr, float *rho_out)
{
    float rho = 0.0f;
    if (pf > 0.0f && pr > 0.0f) {
        rho = sqrtf(pr / pf);
    }
    if (rho > 0.98f) {
        rho = 0.98f;        /* SWR ~ 99 - poza sensownym zakresem */
    }
    if (rho_out) {
        *rho_out = rho;
    }
    return (1.0f + rho) / (1.0f - rho);
}
