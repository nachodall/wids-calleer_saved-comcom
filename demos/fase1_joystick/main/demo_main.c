#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "demo_display.h"
#include "joystick.h"
#include "demo_alert.h"
#include "joystick_orientation.h"

static evento_t alert;
static bool have_alert;
static esp_err_t radio_status;

static void draw_joystick(void)
{
    char rows[7][40];
    const char *lines[7];
    if (radio_status != ESP_OK) {
        snprintf(rows[0], sizeof(rows[0]), "ERROR RADIO CH:%d", ESPNOW_CANAL);
        snprintf(rows[1], sizeof(rows[1]), "%.21s", esp_err_to_name(radio_status));
        snprintf(rows[2], sizeof(rows[2]), "VER MONITOR SERIE");
    } else if (have_alert) {
        snprintf(rows[0], sizeof(rows[0]), "RX %s %s",
                 alert.tipo == ALERTA_DEAUTH ? "DEAUTH" : "TWIN",
                 joystick_screen_direction(joystick_contacts()));
        snprintf(rows[1], sizeof(rows[1]), "C%u %ddB S%u %s%u", alert.canal, alert.rssi, alert.severidad,
                 alert.tipo == ALERTA_DEAUTH ? "FPS" : "SC", alert.intensidad);
        if (alert.tipo == ALERTA_EVILTWIN) {
            snprintf(rows[2], sizeof(rows[2]), "%.21s", alert.ssid);
        } else {
            snprintf(rows[2], sizeof(rows[2]), "%02X:%02X:%02X:%02X:%02X:%02X",
                     alert.bssid[0], alert.bssid[1], alert.bssid[2],
                     alert.bssid[3], alert.bssid[4], alert.bssid[5]);
        }
    } else {
        snprintf(rows[0], sizeof(rows[0]), "ESPERA %s", joystick_screen_direction(joystick_contacts()));
        snprintf(rows[1], sizeof(rows[1]), "ESP-NOW CH:%d", ESPNOW_CANAL);
        snprintf(rows[2], sizeof(rows[2]), "0=PULSADO 1=SUELTO");
    }
    for (int i = 0; i < JOY_COUNT; i++) {
        snprintf(rows[i + 3], sizeof(rows[i + 3]), "%c IO%02d R%d S%d N%" PRIu32,
                 JOY_NAMES[i][0], JOY_PINS[i],
                 joystick[i].raw, joystick[i].stable, joystick[i].presses);
    }
    for (int i = 0; i < 7; i++) {
        lines[i] = rows[i];
    }
    demo_display_draw(lines);
}

void app_main(void)
{
    joystick_init();
    demo_display_init();
    radio_status = demo_alert_init();
    if (radio_status != ESP_OK) {
        ESP_LOGE("demo", "Radio no disponible: %s; joystick sigue activo, sin reiniciar",
                 esp_err_to_name(radio_status));
    }
    draw_joystick();
    uint32_t last_draw = 0;
    uint32_t last_status = 0;
    bool dirty = false;
    uint8_t previous_contacts = 0;
    joystick_gesture_t gesture = {0};
    while (true) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        dirty |= joystick_poll(now);
        uint8_t contacts = joystick_contacts();
        if (contacts != previous_contacts) {
            ESP_LOGI("demo", "CORREGIDO 135 IZQ: %s", joystick_direction(contacts));
            previous_contacts = contacts;
        }
        int menu = joystick_menu_poll(&gesture, contacts, now);
        if (menu != JOY_MENU_NONE) {
            static const char *const names[] = {"NONE", "UP", "DOWN", "LEFT", "RIGHT"};
            ESP_LOGI("demo", "MENU: %s", names[menu]);
        }
        if (demo_alert_poll(&alert)) {
            have_alert = true;
            dirty = true;
        }
        if (now - last_status >= 1000) {
            last_status = now;
            ESP_LOGI("demo", "RAW R=%d D=%d L=%d U=%d | ESTABLE R=%d D=%d L=%d U=%d",
                     joystick[0].raw, joystick[1].raw, joystick[2].raw, joystick[3].raw,
                     joystick[0].stable, joystick[1].stable, joystick[2].stable, joystick[3].stable);
        }
        if (dirty && now - last_draw >= 50) {
            draw_joystick();
            last_draw = now;
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
