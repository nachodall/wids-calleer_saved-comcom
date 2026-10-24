#include "alert_led.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define LED_PIN GPIO_NUM_1
#define SW2_PIN GPIO_NUM_21
#define SW2_DEBOUNCE_MS 40

static uint64_t alert_until_ms;
static uint64_t sw2_changed_ms;
static int sw2_raw = 1;
static int sw2_stable = 1;
static bool led_on;

static void set_led(bool on)
{
    if (on == led_on) return;
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, on ? 0 : 1));
    led_on = on;
    ESP_LOGI("led", "IO1 %s", on ? "ON" : "OFF");
}

void alert_led_init(void)
{
    // LED activo en bajo: preparar nivel alto (apagado) antes de habilitar la salida.
    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 1));
    gpio_config_t output = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << SW2_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    ESP_LOGI("led", "LED=IO1 activo bajo; SW2=IO21 activo bajo (debug)");
}

void alert_led_notify(uint64_t now_ms)
{
    alert_until_ms = now_ms + ALERT_LED_HOLD_MS;
    set_led(true);
}

void alert_led_poll(uint64_t now_ms)
{
    int level = gpio_get_level(SW2_PIN);
    if (level != sw2_raw) {
        sw2_raw = level;
        sw2_changed_ms = now_ms;
    }
    if (level != sw2_stable && now_ms - sw2_changed_ms >= SW2_DEBOUNCE_MS) {
        sw2_stable = level;
        ESP_LOGI("led", "SW2 %s", level == 0 ? "PRESS (debug LED)" : "RELEASE");
    }
    // Soltar SW2 no cancela el aviso luminoso de una alerta reciente.
    set_led(sw2_stable == 0 || now_ms < alert_until_ms);
}
