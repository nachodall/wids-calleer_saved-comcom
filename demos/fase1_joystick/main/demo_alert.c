#include "demo_alert.h"
#include "radio_config.h"
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static QueueHandle_t events;
_Static_assert(sizeof(evento_t) == 54, "Contrato ESP-NOW incompatible");
_Static_assert(sizeof(TEAM_TAG) - 1 == TAG_LEN, "Longitud de tag incompatible");

static void receive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len != TAG_LEN + sizeof(evento_t) ||
        memcmp(data, TEAM_TAG, TAG_LEN) != 0) {
        return;
    }
    evento_t event;
    memcpy(&event, data + TAG_LEN, sizeof(event));
    if ((event.tipo != ALERTA_DEAUTH && event.tipo != ALERTA_EVILTWIN) ||
        event.canal < 1 || event.canal > 13 || event.severidad > 3) {
        return;
    }
    // Solo interesa la alerta mas reciente: no hay historial en esta demo.
    xQueueOverwrite(events, &event);
}

esp_err_t demo_alert_init(void)
{
    events = xQueueCreate(1, sizeof(evento_t));
    ESP_ERROR_CHECK(events ? ESP_OK : ESP_ERR_NO_MEM);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wids_radio_country_init();
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    err = wids_radio_listen();
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(receive));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI("alerta", "B1: ESPERANDO ALERTA REAL | MAC=" MACSTR " canal=%d tag=%s",
             MAC2STR(mac), ESPNOW_CANAL, TEAM_TAG);
    return ESP_OK;
}

bool demo_alert_poll(evento_t *event)
{
    if (xQueueReceive(events, event, 0) != pdTRUE) {
        return false;
    }
    ESP_LOGI("alerta", "ESP-NOW RX %s ssid=%.*s CH=%u RSSI=%d S=%u intensidad=%u BSSID=" MACSTR,
             event->tipo == ALERTA_DEAUTH ? "DEAUTH" : "EVIL TWIN",
             SSID_MAX, event->ssid, event->canal, event->rssi, event->severidad,
             event->intensidad, MAC2STR(event->bssid));
    return true;
}
