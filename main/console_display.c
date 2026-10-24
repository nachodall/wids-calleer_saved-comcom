#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "console_display.h"
#include "known_devices.h"
#include "font5x7.h"

#define PIN_SCK 36
#define PIN_MOSI 37
#define PIN_CS 38
#define PIN_DC 39
#define PIN_RST 40
#define LCD_HOST SPI2_HOST
#define LCD_W 128
#define LCD_H 64

static uint8_t fb[LCD_W * LCD_H / 8];
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t flush_done;
static void draw_text(int x0, int y0, const char *s);


// ---------- display ----------

// Callback del driver SPI: avisa que el framebuffer ya se mandó entero al OLED.
// Corre en una interrupcion (ISR), por eso usa la version FromISR del semaforo.
// Los parametros los exige la libreria; no se usan.
// Devuelve true si desperto una tarea de mayor prioridad (para que haga yield).
static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(flush_done, &woken);
    return woken == pdTRUE;
}

// Inicia el OLED SSD1306 (128x64) por SPI con los pines PIN_*.
// Crea el semaforo flush_done, configura el bus y el panel, deja la pantalla
// en blanco y la prende. Aborta (ESP_ERROR_CHECK) si algun paso falla.
// Hay que llamarla una vez, antes de redraw().
void console_display_init(void)
{
    flush_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(flush_done ? ESP_OK : ESP_ERR_NO_MEM);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(fb),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = on_trans_done,
        .flags.dc_low_on_param = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    esp_lcd_panel_ssd1306_config_t ssd_cfg = {.height = LCD_H};
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .bits_per_pixel = 1,
        .vendor_config = &ssd_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    memset(fb, 0, sizeof(fb));
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb));
    ESP_ERROR_CHECK(xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000)) == pdTRUE
                    ? ESP_OK : ESP_ERR_TIMEOUT);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
}

// Prende el pixel (x, y) en el framebuffer fb. No toca el display: hay que
// llamar a redraw() para verlo. Ignora coordenadas fuera de la pantalla.
// El OLED agrupa 8 filas por byte (bit 0 = fila de arriba), de ahi el y / 8.
static void set_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) {
        return;
    }
    fb[(y / 8) * LCD_W + x] |= 1 << (y % 8);
}

// Dibuja el texto s en el framebuffer con la fuente 5x7, desde (x0, y0).
// Cada letra ocupa 6 px de ancho (5 + 1 de espacio) y cada fila 9 px de alto.
// Recorta al borde de la pantalla. Los caracteres fuera de ASCII 32..126
// salen como '?', incluidos SSID binarios o UTF-8.
static void draw_text(int x0, int y0, const char *s)
{
    int x = x0, y = y0;
    for (; *s; s++, x += 6) {
        if (x + 5 > LCD_W) {
            return;
        }
        if (y + 7 > LCD_H) {
            return;
        }
        char c = (*s < 32 || *s > 126) ? '?' : *s;
        const uint8_t *g = FONT[c - 32];
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1 << row)) {
                    set_pixel(x + col, y + row);
                }
            }
        }
    }
}

void console_display_radio_error(esp_err_t error)
{
    char line[40];
    memset(fb, 0, sizeof(fb));
    snprintf(line, sizeof(line), "ERROR RADIO CH:%d", ESPNOW_CANAL);
    draw_text(0, 0, line);
    draw_text(0, 18, esp_err_to_name(error));
    draw_text(0, 36, "VER MONITOR SERIE");
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb));
    ESP_ERROR_CHECK(xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000)) == pdTRUE
                    ? ESP_OK : ESP_ERR_TIMEOUT);
}

static void format_mac(char *out, size_t size, const uint8_t *mac)
{
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void draw_alert(const console_entry_t *entry)
{
    const evento_t *event = &entry->event;
    char line[64];
    draw_text(0, 0, event->tipo == ALERTA_DEAUTH ? "! DEAUTH" : "! EVIL TWIN");
    // Invertir la primera banda destaca la alerta sin modificar el framebuffer
    // mientras el SPI lo usa: todas las transferencias se esperan al terminar.
    for (size_t i = 0; i < LCD_W; i++) {
        fb[i] ^= 0xff;
    }
    snprintf(line, sizeof(line), "CH:%u RSSI:%d S:%u", event->canal, event->rssi, event->severidad);
    draw_text(0, 9, line);
    if (event->tipo == ALERTA_EVILTWIN) {
        // SSID tiene 32 bytes en el contrato y puede no terminar en NUL.
        snprintf(line, sizeof(line), "%.*s", SSID_MAX, event->ssid);
        draw_text(0, 18, line);
        if (strnlen(line, SSID_MAX) > 21) {
            draw_text(0, 27, line + 21);
        }
    } else {
        draw_text(0, 18, "BSSID AFECTADO:");
    }
    format_mac(line, sizeof(line), event->bssid);
    draw_text(0, event->tipo == ALERTA_EVILTWIN ? 36 : 27, line);
    if (event->tipo == ALERTA_DEAUTH) {
        const char *name = NULL;
        for (size_t i = 0; KNOWN_DEVICES[i].name; i++) {
            if (memcmp(KNOWN_DEVICES[i].mac, event->victima, 6) == 0) {
                name = KNOWN_DEVICES[i].name;
                break;
            }
        }
        if (name) {
            snprintf(line, sizeof(line), "VIC:%s", name);
        } else {
            char mac[18];
            format_mac(mac, sizeof(mac), event->victima);
            snprintf(line, sizeof(line), "VIC:%s", mac);
        }
        draw_text(0, 36, line);
    }
    snprintf(line, sizeof(line), "%s:%u %s",
             event->tipo == ALERTA_DEAUTH ? "FPS" : "SCORE",
             event->intensidad, entry->acknowledged ? "ACK" : "PEND");
    draw_text(0, 45, line);
    draw_text(0, 56, "SW1 ACK  > LOG");
}

void console_display_draw(const console_state_t *state, uint32_t now_ms)
{
    char line[64];
    memset(fb, 0, sizeof(fb));
    if (!state->count) {
        draw_text(0, 0, "WIDS B - CONSOLA");
        draw_text(0, 18, "ESPERANDO ALERTAS...");
        snprintf(line, sizeof(line), "ESP-NOW CH:%d %.4s", ESPNOW_CANAL, TEAM_TAG);
        draw_text(0, 27, line);
        draw_text(0, 45, "SW1 ACK  SW2 LIMPIAR");
        draw_text(0, 56, "< ATAQUE  > LOG");
    } else if (state->log_mode) {
        snprintf(line, sizeof(line), "LOG %u/%u",
                 (unsigned)state->selected + 1, (unsigned)state->count);
        draw_text(0, 0, line);
        size_t first = (state->selected / 5) * 5;
        for (size_t row = 0; row < 5; row++) {
            const console_entry_t *entry = console_entry(state, first + row);
            if (!entry) {
                break;
            }
            snprintf(line, sizeof(line), "%c%lus %s C%u%s",
                     first + row == state->selected ? '>' : ' ',
                     (unsigned long)((now_ms - entry->received_ms) / 1000),
                     entry->event.tipo == ALERTA_DEAUTH ? "DEAUTH" : "TWIN",
                     entry->event.canal, entry->acknowledged ? " A" : " !");
            draw_text(0, 9 + row * 9, line);
        }
        draw_text(0, 56, "^v SEL SW4 VER < ACT");
    } else {
        draw_alert(console_entry(state, state->selected));
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb));
    ESP_ERROR_CHECK(xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000)) == pdTRUE
                    ? ESP_OK : ESP_ERR_TIMEOUT);
}

