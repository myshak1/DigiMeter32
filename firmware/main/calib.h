#pragma once
#include "app_config.h"

/*
 * Przeliczenie napiecia detektora na moc.
 *
 * Detektor diodowy daje napiecie w przyblizeniu proporcjonalne do
 * amplitudy RF, czyli do sqrt(P). Dlatego interpolujemy liniowo w
 * przestrzeni (V, sqrt(P)) pomiedzy punktami kalibracji. Ponizej
 * pierwszego punktu linia idzie do (0 V, 0 W), powyzej ostatniego
 * ekstrapolujemy nachyleniem ostatniego odcinka.
 *
 * Gdy tabela jest pusta, stosujemy model kwadratowy:
 *     P = fs_w * (V / fs_v)^2
 */
float cal_volts_to_watts(const cal_table_t *t, float v, float fs_v, float fs_w);

/* odwrotnosc: jakie napiecie odpowiada mocy w (wg tabeli) */
float cal_watts_to_volts(const cal_table_t *t, float w, float fs_v, float fs_w);

/* mnozy napiecia wszystkich punktow przez k (zmiana czulosci sprzegacza) */
void  cal_scale_volts(cal_table_t *t, float k);

/* dodaje/zastepuje punkt; zwraca 0 gdy OK, <0 przy bledzie
 * (-1 pelna tabela, -2 bledne wartosci, -3 zaburzona monotonicznosc) */
int   cal_add_point(cal_table_t *t, float v, float w);
int   cal_del_point(cal_table_t *t, int idx);

/* SWR z mocy padajacej i odbitej; rho zwracane przez wskaznik (moze byc NULL) */
float cal_swr(float pf, float pr, float *rho_out);
