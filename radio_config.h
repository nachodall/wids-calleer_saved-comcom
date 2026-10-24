#pragma once
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "evento.h"

// El dominio por defecto de ESP-IDF limita a 1..11. Configurar el rango
// 1..13 de Argentina, sin adoptar el pais de un AP (no nos conectamos a uno).
static inline void wids_radio_country_init(void)
{
    const wifi_country_t country = {
        .cc = "AR",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
}

static inline esp_err_t wids_radio_listen(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), "radio", "Desactivar ahorro WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20),
                        "radio", "Configurar HT20");
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    // El arranque de la tarea WiFi puede seguir en curso al volver de start.
    // Reintentar de forma acotada; no afirmar que estamos en 13 sin comprobarlo.
    for (unsigned attempt = 0; attempt < 10; attempt++) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_channel(ESPNOW_CANAL, WIFI_SECOND_CHAN_NONE),
                            "radio", "Fijar canal ESP-NOW");
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_RETURN_ON_ERROR(esp_wifi_get_channel(&primary, &secondary),
                            "radio", "Leer canal ESP-NOW");
        // En HT20 el secundario no define un segundo canal operativo.
        // No exigir su valor para aceptar el canal principal correcto.
        if (primary == ESPNOW_CANAL) {
            ESP_LOGI("radio", "Canal solicitado=%d principal=%u secundario=%d HT20",
                     ESPNOW_CANAL, primary, (int)secondary);
            return ESP_OK;
        }
    }
    ESP_LOGE("radio", "No se pudo fijar canal: solicitado=%d principal=%u secundario=%d HT20",
             ESPNOW_CANAL, primary, (int)secondary);
    return ESP_ERR_INVALID_STATE;
}
