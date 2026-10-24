// Configuracion de radio para la Placa B (consola). RECONSTRUIDO a partir del uso en
// demo_alert.c y del README (el archivo original no estaba en el repo; conviene
// reconciliarlo con la version del autor de la demo). Fija el pais (Argentina,
// canales 1-13) y deja la radio escuchando en ESPNOW_CANAL, verificando con
// reintentos acotados que el canal principal haya quedado bien. Si no lo consigue,
// devuelve error (el llamador muestra ERROR RADIO sin reiniciar).
#pragma once

#include "evento.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pais: Argentina, canales 1 a 13. Llamar despues de esp_wifi_init y antes de start.
static inline void wids_radio_country_init(void)
{
    wifi_country_t pais = {
        .cc = "AR",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&pais));
}

// Deja la radio en ESPNOW_CANAL (canal secundario NONE = 20 MHz / HT20) y verifica,
// con reintentos acotados, que el canal principal haya quedado en ESPNOW_CANAL.
// Llamar despues de esp_wifi_start. Devuelve ESP_OK si lo consiguio, o el error.
static inline esp_err_t wids_radio_listen(void)
{
    esp_err_t err = ESP_FAIL;
    for (int intento = 0; intento < 5; intento++) {
        err = esp_wifi_set_channel(ESPNOW_CANAL, WIFI_SECOND_CHAN_NONE);
        if (err == ESP_OK) {
            uint8_t primario = 0;
            wifi_second_chan_t secundario = WIFI_SECOND_CHAN_NONE;
            err = esp_wifi_get_channel(&primario, &secundario);
            if (err == ESP_OK && primario == ESPNOW_CANAL) {
                ESP_LOGI("radio", "Canal solicitado=%d principal=%d secundario=%d HT20",
                         ESPNOW_CANAL, primario, secundario);
                return ESP_OK;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGE("radio", "No se pudo fijar el canal %d (ultimo err=%s)",
             ESPNOW_CANAL, esp_err_to_name(err));
    return err == ESP_OK ? ESP_FAIL : err;
}
