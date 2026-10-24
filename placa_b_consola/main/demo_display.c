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
#include "demo_display.h"

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
void demo_display_init(void)
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

// El driver conserva su buffer hasta que termina la transferencia SPI.
void demo_display_draw(const uint8_t frame[UI_FRAME_BYTES])
{
    memcpy(fb, frame, sizeof(fb));
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb));
    ESP_ERROR_CHECK(xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000)) == pdTRUE
                    ? ESP_OK : ESP_ERR_TIMEOUT);
}
