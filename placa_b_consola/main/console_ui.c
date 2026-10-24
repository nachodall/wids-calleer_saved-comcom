#include "console_ui.h"
#include "joystick_orientation.h"
#include "font5x7.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

const ui_entry_t *console_ui_entry(const console_ui_t *ui, size_t offset)
{
    if (offset >= ui->count) return NULL;
    return &ui->history[(ui->head + UI_HISTORY_CAPACITY - 1 - offset) % UI_HISTORY_CAPACITY];
}

void console_ui_receive(console_ui_t *ui, const evento_t *event,
                        uint64_t received_ms, uint64_t now_ms)
{
    ui->history[ui->head] = (ui_entry_t){.event = *event, .received_ms = received_ms};
    ui->head = (ui->head + 1) % UI_HISTORY_CAPACITY;
    if (ui->count < UI_HISTORY_CAPACITY) ui->count++;
    ui->total++;
    ui->selected = 0;
    ui->detail_page = 0;
    ui->detail_from_history = false;
    ui->screen = UI_DETAIL;
    // Una rafaga actualiza el aviso sin reiniciar indefinidamente la animacion.
    if (!ui->animating) ui->animation_ms = now_ms;
    ui->animating = true;
}

void console_ui_tick(console_ui_t *ui, uint64_t now_ms)
{
    if (ui->animating && now_ms - ui->animation_ms >= UI_ALERT_DURATION_MS)
        ui->animating = false;
}

void console_ui_action(console_ui_t *ui, int direction)
{
    if (direction < JOY_MENU_UP || direction > JOY_MENU_RIGHT) return;
    if (ui->animating) {
        ui->animating = false; // Primer gesto descubre el detalle; no ejecuta otra accion.
        return;
    }
    switch (ui->screen) {
    case UI_MENU:
        if (direction == JOY_MENU_UP) ui->menu_item = (ui->menu_item + 3) % 4;
        if (direction == JOY_MENU_DOWN) ui->menu_item = (ui->menu_item + 1) % 4;
        if (direction == JOY_MENU_RIGHT) {
            static const ui_screen_t screens[] = {UI_DETAIL, UI_HISTORY, UI_JOYSTICK, UI_RADIO};
            ui->screen = screens[ui->menu_item];
            ui->selected = 0;
            ui->detail_page = 0;
            ui->detail_from_history = false;
        }
        break;
    case UI_HISTORY:
        if (direction == JOY_MENU_UP && ui->selected > 0) ui->selected--;
        if (direction == JOY_MENU_DOWN && ui->selected + 1 < ui->count) ui->selected++;
        if (direction == JOY_MENU_RIGHT && ui->count) {
            ui->screen = UI_DETAIL;
            ui->detail_page = 0;
            ui->detail_from_history = true;
        }
        if (direction == JOY_MENU_LEFT) ui->screen = UI_MENU;
        break;
    case UI_DETAIL:
        if (direction == JOY_MENU_LEFT)
            ui->screen = ui->detail_from_history ? UI_HISTORY : UI_MENU;
        if ((direction == JOY_MENU_UP || direction == JOY_MENU_DOWN) && ui->count)
            ui->detail_page ^= 1;
        if (direction == JOY_MENU_RIGHT) ui->screen = UI_HISTORY;
        break;
    case UI_JOYSTICK:
    case UI_RADIO:
        if (direction == JOY_MENU_LEFT) ui->screen = UI_MENU;
        break;
    }
}

static void pixel(uint8_t *fb, int x, int y)
{
    if (x >= 0 && x < UI_WIDTH && y >= 0 && y < UI_HEIGHT)
        fb[y / 8 * UI_WIDTH + x] |= (uint8_t)(1u << (y % 8));
}

static void rect(uint8_t *fb, int x, int y, int w, int h, bool fill)
{
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            if (fill || row == 0 || row == h - 1 || col == 0 || col == w - 1)
                pixel(fb, x + col, y + row);
}

static void invert(uint8_t *fb, int y, int h)
{
    for (int row = y; row < y + h && row < UI_HEIGHT; row++)
        for (int x = 0; x < UI_WIDTH; x++)
            fb[row / 8 * UI_WIDTH + x] ^= (uint8_t)(1u << (row % 8));
}

static void text(uint8_t *fb, int x, int y, const char *s, int scale)
{
    for (; *s && x + 5 * scale <= UI_WIDTH; s++, x += 6 * scale) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (FONT[c - 32][col] & (1u << row))
                    rect(fb, x + col * scale, y + row * scale, scale, scale, true);
    }
}

static void header(uint8_t *fb, const char *title, const char *right)
{
    text(fb, 3, 1, title, 1);
    text(fb, UI_WIDTH - 3 - (int)strlen(right) * 6, 1, right, 1);
    invert(fb, 0, 10);
}

static void footer(uint8_t *fb, const char *hint)
{
    rect(fb, 0, 54, UI_WIDTH, 1, true);
    text(fb, 1, 57, hint, 1);
}

static const char *type_name(const evento_t *event)
{
    return event->tipo == ALERTA_DEAUTH ? "DEAUTH!!!" : "E.T.! >:(";
}

static void mac_text(uint8_t *fb, int y, const uint8_t *mac)
{
    char line[24];
    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    text(fb, 3, y, line, 1);
}

static void age_text(char *out, size_t size, uint64_t age_ms)
{
    uint64_t seconds = age_ms / 1000;
    if (seconds < 60) snprintf(out, size, "%" PRIu64 "s", seconds);
    else if (seconds < 3600) snprintf(out, size, "%" PRIu64 "m", seconds / 60);
    else if (seconds < 86400) snprintf(out, size, "%" PRIu64 "h", seconds / 3600);
    else snprintf(out, size, "%" PRIu64 "d", seconds / 86400);
}

static void draw_menu(const console_ui_t *ui, const ui_status_t *status, uint8_t *fb)
{
    char line[32];
    header(fb, "WIDS / B", status->radio_error ? "ERROR" : "RADIO");
    static const char *const labels[] = {"MONITOR", "HISTORIAL", "JOYSTICK", "ESTADO RADIO"};
    for (unsigned i = 0; i < 4; i++) {
        int y = 12 + (int)i * 10;
        snprintf(line, sizeof(line), "%c %s", ui->menu_item == i ? '>' : ' ', labels[i]);
        text(fb, 3, y, line, 1);
        if (i == 1) {
            snprintf(line, sizeof(line), "%02u", (unsigned)ui->count);
            text(fb, 110, y, line, 1);
        }
        if (ui->menu_item == i) invert(fb, y - 1, 9);
    }
    footer(fb, "^v ELEGIR  > ENTRAR");
}

static void draw_history(const console_ui_t *ui, uint64_t now, uint8_t *fb)
{
    char line[32], age[16];
    snprintf(line, sizeof(line), "%02u/%02u", ui->count ? (unsigned)ui->selected + 1 : 0,
             (unsigned)ui->count);
    header(fb, "HISTORIAL", line);
    if (!ui->count) {
        text(fb, 13, 22, "SIN ALERTAS AUN", 1);
        text(fb, 13, 36, "ESPERANDO A LA A", 1);
    } else {
        size_t first = ui->selected / 4 * 4;
        for (size_t row = 0; row < 4; row++) {
            const ui_entry_t *entry = console_ui_entry(ui, first + row);
            if (!entry) break;
            age_text(age, sizeof(age), now - entry->received_ms);
            snprintf(line, sizeof(line), "%c %-6s S%u %5.5s",
                     ui->selected == first + row ? '>' : ' ',
                     entry->event.tipo == ALERTA_DEAUTH ? "DEAUTH" : "TWIN",
                     entry->event.severidad, age);
            int y = 12 + (int)row * 10;
            text(fb, 1, y, line, 1);
            if (ui->selected == first + row) invert(fb, y - 1, 9);
        }
        rect(fb, 125, 12, 2, 39, false);
        int thumb = 12 + (ui->count > 1 ? (int)(ui->selected * 33 / (ui->count - 1)) : 0);
        rect(fb, 123, thumb, 5, 6, true);
    }
    footer(fb, "< MENU  ^v SEL  > VER");
}

static void draw_detail(const console_ui_t *ui, const ui_status_t *status,
                        uint64_t now, uint8_t *fb)
{
    const ui_entry_t *entry = console_ui_entry(ui, ui->selected);
    if (!entry) {
        header(fb, "MONITOR", status->radio_error ? "ERROR" : "ESPERA");
        text(fb, 10, 18, status->radio_error ? "RADIO NO DISPONIBLE" : "ESPERANDO ALERTAS", 1);
        char line[32];
        snprintf(line, sizeof(line), "ESP-NOW / CANAL %d", ESPNOW_CANAL);
        text(fb, 10, 31, line, 1);
        text(fb, 10, 43, status->radio_error ? "VER ESTADO RADIO" : "RECEPTOR PREPARADO", 1);
        footer(fb, "< MENU       > LOG");
        return;
    }
    const evento_t *event = &entry->event;
    char line[48], age[16];
    header(fb, type_name(event), ui->detail_page ? "2/2" : "1/2");
    if (ui->detail_page == 0) {
        snprintf(line, sizeof(line), "S%u CH:%02u RSSI:%d", event->severidad, event->canal, event->rssi);
        text(fb, 3, 12, line, 1);
        if (event->tipo == ALERTA_EVILTWIN) {
            char ssid[SSID_MAX + 1];
            memcpy(ssid, event->ssid, SSID_MAX);
            ssid[SSID_MAX] = 0;
            snprintf(line, sizeof(line), "%.20s", ssid[0] ? ssid : "(SSID VACIO)");
            text(fb, 3, 23, line, 1);
            if (strlen(ssid) > 20) text(fb, 3, 33, ssid + 20, 1);
        } else {
            text(fb, 3, 23, "BSSID AFECTADO", 1);
            mac_text(fb, 33, event->bssid);
        }
        age_text(age, sizeof(age), now - entry->received_ms);
        snprintf(line, sizeof(line), "%s:%u HACE %s",
                 event->tipo == ALERTA_DEAUTH ? "FPS" : "SC", event->intensidad, age);
        text(fb, 3, 44, line, 1);
    } else {
        text(fb, 3, 12, "BSSID", 1);
        mac_text(fb, 22, event->bssid);
        if (event->tipo == ALERTA_DEAUTH) {
            text(fb, 3, 33, "VICTIMA", 1);
            mac_text(fb, 44, event->victima);
        } else {
            text(fb, 3, 33, "TIEMPO PLACA A (MS)", 1);
            snprintf(line, sizeof(line), "%" PRIu32, event->ts);
            text(fb, 3, 44, line, 1);
        }
    }
    footer(fb, "< VOLVER ^v PAG > LOG");
}

static void draw_joystick(const ui_status_t *status, uint8_t *fb)
{
    header(fb, "JOYSTICK", joystick_screen_direction(status->stable_contacts));
    static const int pins[] = {10, 11, 12, 9};
    static const char names[] = {'R', 'D', 'L', 'U'};
    for (int i = 0; i < 4; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%c%02d R%d S%d N%" PRIu32, names[i], pins[i],
                 !(status->raw_contacts & (1u << i)), !(status->stable_contacts & (1u << i)),
                 status->presses[i]);
        text(fb, 3, 12 + i * 10, line, 1);
    }
    footer(fb, "< MENU  0=PULSADO");
}

static void draw_radio(const console_ui_t *ui, const ui_status_t *status, uint8_t *fb)
{
    char line[48];
    header(fb, "RADIO", status->radio_error ? "ERROR" : "LISTA");
    snprintf(line, sizeof(line), "CH:%d TAG:%.4s", ESPNOW_CANAL, TEAM_TAG);
    text(fb, 3, 12, line, 1);
    snprintf(line, sizeof(line), "RX:%" PRIu64 " LOG:%u", ui->total, (unsigned)ui->count);
    text(fb, 3, 23, line, 1);
    snprintf(line, sizeof(line), "COLA PERDIDOS:%" PRIu32, status->dropped);
    text(fb, 3, 33, line, 1);
    text(fb, 3, 44, status->radio_error ? status->radio_error : "ESCUCHA / SOLO RX", 1);
    footer(fb, "< MENU  LOG EN RAM");
}

static void draw_notification(const console_ui_t *ui, uint64_t now, uint8_t *fb)
{
    const ui_entry_t *entry = console_ui_entry(ui, 0);
    if (!entry) return;
    uint64_t elapsed = now - ui->animation_ms;
    if (elapsed > UI_ALERT_DURATION_MS) elapsed = UI_ALERT_DURATION_MS;
    header(fb, "! NUEVA ALERTA", "RX");
    // Apertura de esquinas con desaceleracion; el texto queda quieto y legible.
    int remaining = elapsed < 500 ? 500 - (int)elapsed : 0;
    int inset = remaining * remaining * 25 / (500 * 500);
    int left = 3 + inset, right = 124 - inset;
    for (int side = 0; side < 2; side++) {
        int x = side ? right : left;
        rect(fb, x, 14, 1, 5, true);
        rect(fb, x, 44, 1, 3, true);
        rect(fb, side ? x - 7 : x, 14, 8, 1, true);
        rect(fb, side ? x - 7 : x, 46, 8, 1, true);
    }
    const char *title = type_name(&entry->event);
    text(fb, (UI_WIDTH - (int)strlen(title) * 12 + 2) / 2, 20, title, 2);
    char line[32];
    snprintf(line, sizeof(line), "SEV %u / CANAL %02u", entry->event.severidad, entry->event.canal);
    text(fb, 16, 37, line, 1);
    // Barra temporal continua, sin destellos de pantalla completa ni delays.
    rect(fb, 3, 50, 122, 3, false);
    rect(fb, 4, 51, (int)(elapsed * 120 / UI_ALERT_DURATION_MS), 1, true);
    text(fb, 7, 57, "JOYSTICK > DETALLE", 1);
}

void console_ui_render(const console_ui_t *ui, const ui_status_t *status,
                       uint64_t now_ms, uint8_t frame[UI_FRAME_BYTES])
{
    memset(frame, 0, UI_FRAME_BYTES);
    if (ui->animating) {
        draw_notification(ui, now_ms, frame);
        return;
    }
    switch (ui->screen) {
    case UI_MENU: draw_menu(ui, status, frame); break;
    case UI_DETAIL: draw_detail(ui, status, now_ms, frame); break;
    case UI_HISTORY: draw_history(ui, now_ms, frame); break;
    case UI_JOYSTICK: draw_joystick(status, frame); break;
    case UI_RADIO: draw_radio(ui, status, frame); break;
    }
}
