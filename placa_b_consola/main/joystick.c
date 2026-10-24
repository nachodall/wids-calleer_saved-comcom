#include "joystick.h"
#include "joystick_orientation.h"
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

const int JOY_PINS[JOY_COUNT] = {10, 11, 12, 9};
const char *const JOY_NAMES[JOY_COUNT] = {"RIGHT", "DOWN", "LEFT", "UP"};
joystick_key_t joystick[JOY_COUNT];

uint8_t joystick_contacts(void)
{
    return (joystick[JOY_RIGHT].stable == 0 ? JOY_HW_RIGHT : 0) |
           (joystick[JOY_DOWN].stable == 0 ? JOY_HW_DOWN : 0) |
           (joystick[JOY_LEFT].stable == 0 ? JOY_HW_LEFT : 0) |
           (joystick[JOY_UP].stable == 0 ? JOY_HW_UP : 0);
}

void joystick_init(void)
{
    gpio_config_t cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < JOY_COUNT; i++) {
        cfg.pin_bit_mask |= 1ULL << JOY_PINS[i];
        joystick[i].raw = joystick[i].stable = 1;
    }
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI("joystick", "RIGHT=IO10 DOWN=IO11 LEFT=IO12 UP=IO9; 0=pulsado 1=suelto");
}

bool joystick_poll(uint32_t now_ms)
{
    bool changed = false;
    for (int i = 0; i < JOY_COUNT; i++) {
        joystick_key_t *key = &joystick[i];
        int level = gpio_get_level(JOY_PINS[i]);
        if (level != key->raw) {
            key->raw = level;
            key->changed_ms = now_ms;
            key->raw_edges++;
            changed = true;
            ESP_LOGI("joystick", "RAW %s IO%d=%d flancos=%" PRIu32,
                     JOY_NAMES[i], JOY_PINS[i], level, key->raw_edges);
        }
        if (level != key->stable && now_ms - key->changed_ms >= 40) {
            key->stable = level;
            if (level == 0) {
                key->presses++;
            }
            changed = true;
            ESP_LOGI("joystick", "%s %s IO%d pulsaciones=%" PRIu32,
                     level == 0 ? "PRESS" : "RELEASE", JOY_NAMES[i], JOY_PINS[i], key->presses);
        }
    }
    return changed;
}
