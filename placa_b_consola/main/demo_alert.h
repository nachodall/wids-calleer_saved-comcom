#pragma once
#include <stdbool.h>
#include "evento.h"
#include "esp_err.h"

esp_err_t demo_alert_init(void);
bool demo_alert_poll(evento_t *event);
