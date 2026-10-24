#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "console_display.h"
#include "joystick_orientation.h"
#include "radio_config.h"

static const char *TAG = "wids_consola";
static QueueHandle_t rx_queue;
static console_state_t state;
static joystick_gesture_t joystick_gesture;

// Estadisticas compartidas con el callback de WiFi; sin log ni OLED en RX.
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t invalid_packets;
static uint32_t dropped_packets;

enum { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ACK, KEY_CLEAR, KEY_MODE, KEY_VIEW, KEY_COUNT };
static const gpio_num_t KEY_PINS[KEY_COUNT] = {9, 11, 12, 10, 14, 21, 47, 48};
static struct {
    int raw;
    int stable;
    uint32_t changed_ms;
} keys[KEY_COUNT];

static uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    console_entry_t entry = {0};
    if (!info || length < 0 || !console_decode(data, (size_t)length, &entry.event)) {
        portENTER_CRITICAL(&stats_lock);
        invalid_packets++;
        portEXIT_CRITICAL(&stats_lock);
        return;
    }
    memcpy(entry.source, info->src_addr, sizeof(entry.source));
    entry.received_ms = millis();
    if (xQueueSend(rx_queue, &entry, 0) != pdTRUE) {
        portENTER_CRITICAL(&stats_lock);
        dropped_packets++;
        portEXIT_CRITICAL(&stats_lock);
    }
}

static esp_err_t espnow_start(void)
{
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
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "Placa B | MAC " MACSTR " | ESP-NOW canal %d | tag %s",
             MAC2STR(mac), ESPNOW_CANAL, TEAM_TAG);
    return ESP_OK;
}

static void buttons_init(void)
{
    gpio_config_t config = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (size_t i = 0; i < KEY_COUNT; i++) {
        config.pin_bit_mask |= 1ULL << KEY_PINS[i];
        keys[i].raw = keys[i].stable = 1;
    }
    ESP_ERROR_CHECK(gpio_config(&config));
}

// Antirrebote por entrada: un flanco de pulsacion tras 40 ms estables.
static bool key_pressed(size_t index, uint32_t now)
{
    int level = gpio_get_level(KEY_PINS[index]);
    if (level != keys[index].raw) {
        keys[index].raw = level;
        keys[index].changed_ms = now;
    }
    if (level != keys[index].stable && now - keys[index].changed_ms >= 40) {
        keys[index].stable = level;
        return level == 0;
    }
    return false;
}

static void accept_entry(const console_entry_t *entry)
{
    console_push(&state, entry);
    const evento_t *event = &entry->event;
    // Precision explicita: el SSID recibido no requiere terminacion NUL.
    ESP_LOGI(TAG, "%s src=" MACSTR " bssid=" MACSTR " victima=" MACSTR
             " ssid=%.*s canal=%u rssi=%d severidad=%u intensidad=%u ts_A=%" PRIu32,
             event->tipo == ALERTA_DEAUTH ? "DEAUTH" : "EVIL TWIN",
             MAC2STR(entry->source), MAC2STR(event->bssid), MAC2STR(event->victima),
             SSID_MAX, event->ssid, event->canal, event->rssi, event->severidad,
             event->intensidad, event->ts);
}

void app_main(void)
{
    rx_queue = xQueueCreate(16, sizeof(console_entry_t));
    ESP_ERROR_CHECK(rx_queue ? ESP_OK : ESP_ERR_NO_MEM);
    console_display_init();
    buttons_init();
    esp_err_t radio_status = espnow_start();
    if (radio_status != ESP_OK) {
        ESP_LOGE(TAG, "Radio no disponible: %s; no se inicia ESP-NOW",
                 esp_err_to_name(radio_status));
        console_display_radio_error(radio_status);
        return;
    }
    uint32_t last_draw = millis();
    uint32_t last_stats = last_draw;
    console_display_draw(&state, last_draw);

    while (true) {
        uint32_t now = millis();
        bool dirty = false;
        bool pressed[KEY_COUNT];
        for (size_t i = 0; i < KEY_COUNT; i++) {
            pressed[i] = key_pressed(i, now);
            dirty |= pressed[i];
        }
        uint8_t contacts = (keys[KEY_RIGHT].stable == 0 ? JOY_HW_RIGHT : 0) |
                           (keys[KEY_DOWN].stable == 0 ? JOY_HW_DOWN : 0) |
                           (keys[KEY_LEFT].stable == 0 ? JOY_HW_LEFT : 0) |
                           (keys[KEY_UP].stable == 0 ? JOY_HW_UP : 0);
        int direction = joystick_menu_poll(&joystick_gesture, contacts, now);
        pressed[KEY_UP] = direction == JOY_MENU_UP;
        pressed[KEY_DOWN] = direction == JOY_MENU_DOWN;
        pressed[KEY_LEFT] = direction == JOY_MENU_LEFT;
        pressed[KEY_RIGHT] = direction == JOY_MENU_RIGHT;
        dirty |= direction != JOY_MENU_NONE;
        if (pressed[KEY_LEFT]) {
            state.log_mode = false;
            state.selected = 0;
        }
        if (pressed[KEY_RIGHT]) {
            state.log_mode = true;
        }
        if (pressed[KEY_MODE]) {
            state.log_mode = !state.log_mode;
            state.selected = 0;
        }
        if (state.log_mode && pressed[KEY_UP] && state.selected > 0) {
            state.selected--;
        }
        if (state.log_mode && pressed[KEY_DOWN] && state.selected + 1 < state.count) {
            state.selected++;
        }
        if (pressed[KEY_ACK]) {
            console_acknowledge(&state);
            ESP_LOGI(TAG, "ACK local del evento seleccionado");
        }
        if (pressed[KEY_CLEAR]) {
            console_clear(&state);
            ESP_LOGI(TAG, "Historial borrado");
        }
        if (pressed[KEY_VIEW]) {
            state.log_mode = false;
        }

        console_entry_t entry;
        // Limite por ciclo: el trafico continuo no bloquea los controles.
        for (size_t i = 0; i < 16 && xQueueReceive(rx_queue, &entry, 0) == pdTRUE; i++) {
            accept_entry(&entry);
            dirty = true;
        }
        if (now - last_stats >= 5000) {
            uint32_t invalid, dropped;
            portENTER_CRITICAL(&stats_lock);
            invalid = invalid_packets;
            dropped = dropped_packets;
            portEXIT_CRITICAL(&stats_lock);
            if (invalid || dropped) {
                ESP_LOGW(TAG, "Paquetes descartados: invalidos=%" PRIu32 " cola_llena=%" PRIu32,
                         invalid, dropped);
            }
            last_stats = now;
        }
        if (dirty || (state.log_mode && now - last_draw >= 1000)) {
            console_display_draw(&state, now);
            last_draw = now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
