#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "console_state.h"

static void test_protocol(void)
{
    evento_t event = {
        .tipo = ALERTA_EVILTWIN, .canal = 13, .rssi = -90,
        .severidad = 3, .intensidad = 123, .ts = 0x12345678,
    };
    memset(event.ssid, 'X', sizeof(event.ssid));
    uint8_t wire[1 + TAG_LEN + sizeof(evento_t)] = {0};
    memcpy(wire + 1, TEAM_TAG, TAG_LEN);
    memcpy(wire + 1 + TAG_LEN, &event, sizeof(event));
    evento_t decoded;
    const size_t length = TAG_LEN + sizeof(evento_t);
    // Contrato real: 4 bytes de tag + 54 de evento, incluso sin alineacion.
    assert(length == 58);
    assert(console_decode(wire + 1, length, &decoded));
    assert(memcmp(&decoded, &event, sizeof(event)) == 0);
    for (size_t n = 0; n < length; n++) {
        assert(!console_decode(wire + 1, n, &decoded));
    }
    assert(!console_decode(wire, sizeof(wire), &decoded));
    assert(!console_decode(NULL, length, &decoded));
    assert(!console_decode(wire + 1, length, NULL));
    wire[1] = 'Z';
    assert(!console_decode(wire + 1, length, &decoded));
    wire[1] = TEAM_TAG[0];
    event.tipo = 99;
    memcpy(wire + 1 + TAG_LEN, &event, sizeof(event));
    assert(!console_decode(wire + 1, length, &decoded));
    event.tipo = ALERTA_DEAUTH;
    event.canal = 0;
    memcpy(wire + 1 + TAG_LEN, &event, sizeof(event));
    assert(!console_decode(wire + 1, length, &decoded));
    event.canal = 14;
    memcpy(wire + 1 + TAG_LEN, &event, sizeof(event));
    assert(!console_decode(wire + 1, length, &decoded));
    event.canal = 1;
    event.severidad = 4;
    memcpy(wire + 1 + TAG_LEN, &event, sizeof(event));
    assert(!console_decode(wire + 1, length, &decoded));
}

static void test_history(void)
{
    console_state_t state = {0};
    assert(!console_entry(&state, 0));
    console_acknowledge(&state);
    for (unsigned i = 0; i < 40; i++) {
        console_entry_t entry = {.event = {.ts = i}, .received_ms = i * 1000};
        state.log_mode = true;
        state.selected = 5;
        console_push(&state, &entry);
        assert(!state.log_mode && state.selected == 0);
        assert(!console_entry(&state, 0)->acknowledged);
    }
    assert(state.count == 16);
    for (size_t i = 0; i < 16; i++) {
        assert(console_entry(&state, i)->event.ts == 39 - i);
    }
    assert(!console_entry(&state, 16));
    state.log_mode = true;
    state.selected = 7;
    console_acknowledge(&state);
    assert(console_entry(&state, 7)->acknowledged);
    assert(!console_entry(&state, 0)->acknowledged);
    // SW4 abre el detalle: ACK conserva la seleccion del historial.
    state.log_mode = false;
    console_acknowledge(&state);
    assert(console_entry(&state, 7)->acknowledged);
    assert(!console_entry(&state, 0)->acknowledged);
    console_clear(&state);
    assert(state.count == 0 && state.selected == 0 && !state.log_mode);
    assert(!console_entry(&state, 0));
}

int main(void)
{
    test_protocol();
    test_history();
    puts("OK: protocolo ESP-NOW, historial circular, ACK y limpieza");
}
