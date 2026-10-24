#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "console_ui.h"
#include "joystick_orientation.h"

static evento_t sample(unsigned id)
{
    evento_t e = {
        .tipo = id % 2 ? ALERTA_EVILTWIN : ALERTA_DEAUTH,
        .canal = 13, .rssi = -128, .severidad = 3,
        .intensidad = 65535, .ts = id,
        .bssid = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .victima = {1, 2, 3, 4, 5, 6},
    };
    // SSID legal de 32 bytes sin terminador.
    memcpy(e.ssid, "RED-LARGA-DE-PRUEBA-0123456789ABCD", SSID_MAX);
    return e;
}

static void test_navigation(void)
{
    console_ui_t ui = {0};
    assert(ui.screen == UI_MENU);
    console_ui_action(&ui, JOY_MENU_UP);
    assert(ui.menu_item == 3);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    assert(ui.screen == UI_RADIO);
    console_ui_action(&ui, JOY_MENU_LEFT);
    console_ui_action(&ui, JOY_MENU_DOWN);
    assert(ui.screen == UI_MENU && ui.menu_item == 0);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    assert(ui.screen == UI_DETAIL && !ui.count);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    console_ui_action(&ui, JOY_MENU_DOWN);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    assert(ui.screen == UI_HISTORY && ui.selected == 0);
    console_ui_action(&ui, JOY_MENU_LEFT);
    console_ui_action(&ui, JOY_MENU_DOWN);
    console_ui_action(&ui, JOY_MENU_DOWN);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    assert(ui.screen == UI_JOYSTICK);
    console_ui_action(&ui, JOY_MENU_LEFT);
    assert(ui.screen == UI_MENU);
}

static void test_history_and_interrupt(void)
{
    console_ui_t ui = {0};
    for (unsigned i = 0; i < 70; i++) {
        evento_t e = sample(i);
        console_ui_receive(&ui, &e, i * 1000, i * 1000);
        console_ui_tick(&ui, i * 1000 + UI_ALERT_DURATION_MS);
    }
    assert(ui.count == UI_HISTORY_CAPACITY && ui.total == 70);
    for (size_t i = 0; i < ui.count; i++)
        assert(console_ui_entry(&ui, i)->event.ts == 69 - i);
    assert(!console_ui_entry(&ui, UI_HISTORY_CAPACITY));
    console_ui_action(&ui, JOY_MENU_RIGHT);
    for (unsigned i = 0; i < 40; i++) console_ui_action(&ui, JOY_MENU_DOWN);
    assert(ui.selected == UI_HISTORY_CAPACITY - 1);
    console_ui_action(&ui, JOY_MENU_RIGHT);
    assert(ui.screen == UI_DETAIL && ui.detail_from_history);
    assert(console_ui_entry(&ui, ui.selected)->event.ts == 38);
    console_ui_action(&ui, JOY_MENU_DOWN);
    assert(ui.detail_page == 1);
    console_ui_action(&ui, JOY_MENU_LEFT);
    assert(ui.screen == UI_HISTORY && ui.selected == 31);
    for (unsigned i = 0; i < 40; i++) console_ui_action(&ui, JOY_MENU_UP);
    assert(ui.selected == 0);
    // Cada pantalla debe ser interrumpida por una alerta real nueva.
    for (int screen = UI_MENU; screen <= UI_RADIO; screen++) {
        ui.screen = screen;
        evento_t e = sample(100 + screen);
        console_ui_receive(&ui, &e, 100000, 100000);
        assert(ui.screen == UI_DETAIL && ui.selected == 0 && ui.detail_page == 0);
        assert(ui.animating && !ui.detail_from_history);
        console_ui_action(&ui, JOY_MENU_LEFT);
        assert(!ui.animating && ui.screen == UI_DETAIL);
        console_ui_action(&ui, JOY_MENU_LEFT);
        assert(ui.screen == UI_MENU);
    }
}

static void test_animation_and_clock(void)
{
    console_ui_t ui = {0};
    evento_t e = sample(1);
    uint64_t start = (uint64_t)UINT32_MAX - 20;
    console_ui_receive(&ui, &e, start, start);
    console_ui_tick(&ui, start + 1000);
    assert(ui.animating);
    // La rafaga mantiene todos los eventos y no prolonga el aviso.
    for (unsigned i = 0; i < 10; i++)
        console_ui_receive(&ui, &e, start + 1000 + i, start + 1000 + i);
    assert(ui.animation_ms == start && ui.count == 11);
    console_ui_tick(&ui, start + UI_ALERT_DURATION_MS - 1);
    assert(ui.animating);
    console_ui_tick(&ui, start + UI_ALERT_DURATION_MS);
    assert(!ui.animating && ui.screen == UI_DETAIL);
    console_ui_receive(&ui, &e, start + 5000, start + 5000);
    assert(ui.animation_ms == start + 5000 && ui.animating);
}

static void render_checked(console_ui_t *ui, ui_status_t *status, uint64_t now)
{
    struct {
        unsigned char before[16];
        uint8_t frame[UI_FRAME_BYTES];
        unsigned char after[16];
    } buffer;
    memset(&buffer, 0xA5, sizeof(buffer));
    console_ui_render(ui, status, now, buffer.frame);
    for (unsigned i = 0; i < 16; i++)
        assert(buffer.before[i] == 0xA5 && buffer.after[i] == 0xA5);
}

static void test_render(void)
{
    console_ui_t ui = {0};
    ui_status_t status = {0};
    for (int screen = UI_MENU; screen <= UI_RADIO; screen++) {
        ui.screen = screen;
        render_checked(&ui, &status, 0);
    }
    for (unsigned id = 0; id < 2; id++) {
        evento_t e = sample(id);
        console_ui_receive(&ui, &e, 5000, 5000);
        uint8_t first[UI_FRAME_BYTES], next[UI_FRAME_BYTES];
        console_ui_render(&ui, &status, 5000, first);
        console_ui_render(&ui, &status, 5250, next);
        assert(memcmp(first, next, sizeof(first)) != 0);
        for (unsigned t = 0; t <= UI_ALERT_DURATION_MS; t += 40)
            render_checked(&ui, &status, 5000 + t);
        console_ui_tick(&ui, 10000);
        for (int screen = UI_MENU; screen <= UI_RADIO; screen++) {
            ui.screen = screen;
            for (unsigned page = 0; page < 2; page++) {
                ui.detail_page = page;
                render_checked(&ui, &status, UINT64_C(5000000000));
            }
        }
    }
    // Texto no ASCII y errores largos no deben salir del framebuffer.
    memset(ui.history[1].event.ssid, 0xFF, SSID_MAX);
    ui.selected = 0;
    ui.detail_page = 0;
    status.radio_error = "ESP_ERR_INVALID_STATE";
    status.dropped = UINT32_MAX;
    for (int screen = UI_MENU; screen <= UI_RADIO; screen++) {
        ui.screen = screen;
        for (unsigned contacts = 0; contacts < 16; contacts++) {
            status.stable_contacts = contacts;
            render_checked(&ui, &status, UINT64_C(5000000000));
        }
    }
}

// Capturas del renderizador real para revisar el OLED sin hardware.
static void snapshot(const char *dir, const char *name, console_ui_t *ui,
                     ui_status_t *status, uint64_t now)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pgm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    uint8_t frame[UI_FRAME_BYTES];
    console_ui_render(ui, status, now, frame);
    fprintf(f, "P5\n128 64\n255\n");
    for (int y = 0; y < UI_HEIGHT; y++)
        for (int x = 0; x < UI_WIDTH; x++)
            fputc(frame[y / 8 * UI_WIDTH + x] & (1u << (y % 8)) ? 255 : 0, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    test_navigation();
    test_history_and_interrupt();
    test_animation_and_clock();
    test_render();
    puts("OK: menu, historial, interrupciones, rafagas, reloj y framebuffer");
    if (argc > 1) {
        console_ui_t ui = {0};
        ui_status_t status = {0};
        snapshot(argv[1], "menu", &ui, &status, 0);
        for (unsigned i = 0; i < 8; i++) {
            evento_t e = sample(i);
            console_ui_receive(&ui, &e, i * 1000, i * 1000);
            console_ui_tick(&ui, i * 1000 + UI_ALERT_DURATION_MS);
        }
        for (unsigned t = 0; t < UI_ALERT_DURATION_MS; t += 40) {
            ui.animating = true;
            ui.animation_ms = 8000;
            char name[32];
            snprintf(name, sizeof(name), "alert-%04u", t);
            snapshot(argv[1], name, &ui, &status, 8000 + t);
        }
        console_ui_tick(&ui, 11000);
        snapshot(argv[1], "detail", &ui, &status, 11000);
        ui.screen = UI_HISTORY;
        ui.selected = 2;
        snapshot(argv[1], "history", &ui, &status, 11000);
        ui.screen = UI_RADIO;
        snapshot(argv[1], "radio", &ui, &status, 11000);
    }
}
