"""Prueba en host la inicializacion real con respuestas simuladas del driver."""
from pathlib import Path
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdint.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define WIFI_PS_NONE 0
#define WIFI_IF_STA 0
#define WIFI_BW20 1
#define WIFI_COUNTRY_POLICY_MANUAL 1
typedef enum { WIFI_SECOND_CHAN_NONE, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
typedef struct { char cc[3]; uint8_t schan, nchan; int8_t max_tx_power; int policy; } wifi_country_t;
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define ESP_RETURN_ON_ERROR(expr, ...) do { int err = (expr); if (err != ESP_OK) return err; } while (0)
// Conservar el chequeo de formatos y argumentos de los logs.
#define ESP_LOGI(tag, ...) do { if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, ...) do { if (0) printf(__VA_ARGS__); } while (0)
#define pdMS_TO_TICKS(ms) (ms)
esp_err_t esp_wifi_set_country(const wifi_country_t *country);
esp_err_t esp_wifi_set_ps(int mode);
esp_err_t esp_wifi_set_bandwidth(int iface, int bandwidth);
esp_err_t esp_wifi_set_channel(uint8_t channel, wifi_second_chan_t secondary);
esp_err_t esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *secondary);
void vTaskDelay(unsigned ticks);
'''
TEST = r'''
#include <stdio.h>
#include "radio_config.h"
static unsigned reads, sets, delays;
static unsigned settle_after;
static uint8_t actual;
static wifi_second_chan_t second;
static esp_err_t set_error, read_error, bandwidth_error;
esp_err_t esp_wifi_set_country(const wifi_country_t *country) {
    assert(country->nchan == 13); return ESP_OK;
}
esp_err_t esp_wifi_set_ps(int mode) { assert(mode == WIFI_PS_NONE); return ESP_OK; }
esp_err_t esp_wifi_set_bandwidth(int iface, int bandwidth) {
    assert(iface == WIFI_IF_STA && bandwidth == WIFI_BW20); return bandwidth_error;
}
esp_err_t esp_wifi_set_channel(uint8_t channel, wifi_second_chan_t secondary) {
    assert(channel == 13 && secondary == WIFI_SECOND_CHAN_NONE); sets++; return set_error;
}
esp_err_t esp_wifi_get_channel(uint8_t *channel, wifi_second_chan_t *secondary) {
    reads++; *channel = reads >= settle_after ? actual : 1; *secondary = second; return read_error;
}
void vTaskDelay(unsigned ticks) { assert(ticks == 20); delays++; }
static void reset(void) {
    reads = sets = delays = 0; settle_after = 1; actual = 13;
    second = WIFI_SECOND_CHAN_NONE; set_error = read_error = bandwidth_error = ESP_OK;
}
int main(void) {
    reset(); wids_radio_country_init(); assert(wids_radio_listen() == ESP_OK); assert(reads == 1);
    // Regresion: principal correcto con secundario distinto no debe abortar HT20.
    reset(); second = WIFI_SECOND_CHAN_BELOW; assert(wids_radio_listen() == ESP_OK);
    reset(); settle_after = 3; assert(wids_radio_listen() == ESP_OK); assert(reads == 3 && delays == 3);
    // Un principal incorrecto nunca se presenta como receptor listo ni reinicia.
    reset(); actual = 1; assert(wids_radio_listen() == ESP_ERR_INVALID_STATE); assert(reads == 10);
    reset(); set_error = ESP_ERR_INVALID_ARG; assert(wids_radio_listen() == set_error); assert(reads == 0);
    reset(); read_error = ESP_ERR_INVALID_STATE; assert(wids_radio_listen() == read_error); assert(reads == 1);
    reset(); bandwidth_error = ESP_ERR_INVALID_STATE; assert(wids_radio_listen() == bandwidth_error); assert(sets == 0);
    puts("OK: HT20, lectura demorada, secundario ignorado y fallos sin abortar");
}
'''

with tempfile.TemporaryDirectory(prefix="wids-radio-test-") as directory:
    tmp = Path(directory)
    (tmp / "freertos").mkdir()
    (tmp / "radio_stub.h").write_text(STUB)
    for name in ("esp_err.h", "esp_check.h", "esp_log.h", "esp_wifi.h", "freertos/FreeRTOS.h", "freertos/task.h"):
        (tmp / name).write_text('#include "radio_stub.h"\n')
    (tmp / "test.c").write_text(TEST)
    binary = tmp / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(tmp), "-I", str(PROJECT), str(tmp / "test.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
