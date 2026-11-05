#pragma once
/*
 * Dioda RGB stanu (tylko czyste kolory, bez mieszania):
 *   zielony   - gotowosc (wlaczone, brak nadawania)
 *   czerwony  - nadawanie
 *   niebieski - brak ADS1115 albo brak danych z niego (sprawdz przewod X1-X2)
 */
void status_led_start(void);
