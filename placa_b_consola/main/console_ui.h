#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "evento.h"

#define UI_WIDTH 128
#define UI_HEIGHT 64
#define UI_FRAME_BYTES (UI_WIDTH * UI_HEIGHT / 8)
#define UI_HISTORY_CAPACITY 32
#define UI_ALERT_DURATION_MS 2200

typedef enum { UI_MENU, UI_DETAIL, UI_HISTORY, UI_JOYSTICK, UI_RADIO } ui_screen_t;

typedef struct {
    evento_t event;
    uint64_t received_ms; // Reloj local de B, independiente de evento.ts (Placa A).
} ui_entry_t;

typedef struct {
    ui_entry_t history[UI_HISTORY_CAPACITY];
    size_t head, count, selected;
    uint64_t total;
    ui_screen_t screen;
    unsigned menu_item, detail_page;
    bool detail_from_history, animating;
    uint64_t animation_ms;
} console_ui_t;

typedef struct {
    const char *radio_error; // NULL: receptor inicializado; no implica enlace con A.
    uint32_t dropped;
    uint8_t raw_contacts, stable_contacts;
    uint32_t presses[4];
} ui_status_t;

const ui_entry_t *console_ui_entry(const console_ui_t *ui, size_t offset);
void console_ui_receive(console_ui_t *ui, const evento_t *event,
                        uint64_t received_ms, uint64_t now_ms);
void console_ui_tick(console_ui_t *ui, uint64_t now_ms);
void console_ui_action(console_ui_t *ui, int direction);
// Renderizador puro: framebuffer SSD1306 por paginas, sin IO ni esperas.
void console_ui_render(const console_ui_t *ui, const ui_status_t *status,
                       uint64_t now_ms, uint8_t frame[UI_FRAME_BYTES]);
