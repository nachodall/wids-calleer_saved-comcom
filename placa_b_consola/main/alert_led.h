#pragma once

#include <stdint.h>

#define ALERT_LED_HOLD_MS 2200

void alert_led_init(void);
// Llamar desde el bucle principal por cada alerta valida recibida.
void alert_led_notify(uint64_t now_ms);
// Lee SW2 con antirrebote y actualiza el LED sin esperas.
void alert_led_poll(uint64_t now_ms);
