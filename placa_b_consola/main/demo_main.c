#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "demo_display.h"
#include "joystick.h"
#include "demo_alert.h"
#include "joystick_orientation.h"
#include "console_ui.h"
#include "alert_led.h"

static console_ui_t ui;
static uint8_t frame[UI_FRAME_BYTES];

static void draw(ui_status_t *status, uint64_t now)
{
    status->raw_contacts = 0;
    status->stable_contacts = joystick_contacts();
    for (int i = 0; i < JOY_COUNT; i++) {
        if (!joystick[i].raw) status->raw_contacts |= 1u << i;
        status->presses[i] = joystick[i].presses;
    }
    status->dropped = demo_alert_dropped();
    console_ui_render(&ui, status, now, frame);
    demo_display_draw(frame);
}

void app_main(void)
{
    alert_led_init();
    joystick_init();
    demo_display_init();
    esp_err_t radio_status = demo_alert_init();
    ui_status_t status = {0};
    if (radio_status != ESP_OK) {
        status.radio_error = esp_err_to_name(radio_status);
        ESP_LOGE("consola", "Radio no disponible: %s; menu y joystick siguen activos",
                 status.radio_error);
    }
    uint64_t last_draw = (uint64_t)esp_timer_get_time() / 1000;
    draw(&status, last_draw);
    bool dirty = false;
    joystick_gesture_t gesture = {0};
    while (true) {
        uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        dirty |= joystick_poll((uint32_t)now);
        bool was_animating = ui.animating;
        console_ui_tick(&ui, now);
        dirty |= was_animating != ui.animating;
        int action = joystick_menu_poll(&gesture, joystick_contacts(), (uint32_t)now);
        if (action != JOY_MENU_NONE) {
            console_ui_action(&ui, action);
            dirty = true;
        }
        // Trabajo acotado por vuelta: las rafagas no monopolizan el joystick.
        // Procesar RX despues de las acciones da prioridad visual al aviso nuevo.
        for (unsigned i = 0; i < DEMO_ALERT_QUEUE_CAPACITY; i++) {
            evento_t event;
            uint64_t received_ms;
            if (!demo_alert_poll(&event, &received_ms)) break;
            now = (uint64_t)esp_timer_get_time() / 1000;
            alert_led_notify(now);
            console_ui_receive(&ui, &event, received_ms, now);
            dirty = true;
        }
        now = (uint64_t)esp_timer_get_time() / 1000;
        alert_led_poll(now);
        // 25 FPS durante el aviso; refresco de edades/estado cada segundo.
        if (((dirty || ui.animating) && now - last_draw >= 40) ||
            now - last_draw >= 1000) {
            draw(&status, now);
            last_draw = now;
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
