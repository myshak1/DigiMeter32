#pragma once
/*
 * Przycisk chwilowy: dlugo (>0,8 s) = nastepny profil kalibracji.
 * Przelacznik AVG/PEP (wlacz/wylacz do masy, CONFIG_DM32_MODE_SW_GPIO):
 * zwarty = PEP, rozwarty = AVG (albo odwrotnie wg menuconfig).
 * Bez przelacznika (GPIO = -1) krotkie nacisniecie przycisku zmienia AVG/PEP.
 */
void button_start(void);

/* true, gdy tryb AVG/PEP ustawia przelacznik sprzetowy */
bool button_mode_switch_present(void);
