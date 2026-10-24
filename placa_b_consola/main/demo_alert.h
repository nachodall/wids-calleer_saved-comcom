#pragma once
#include <stdbool.h>
#include "evento.h"
#include "esp_err.h"

esp_err_t demo_alert_init(void);
#define DEMO_ALERT_QUEUE_CAPACITY 32
bool demo_alert_poll(evento_t *event, uint64_t *received_ms);
uint32_t demo_alert_dropped(void);
