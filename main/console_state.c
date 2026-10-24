#include "console_state.h"
#include <string.h>

_Static_assert(sizeof(evento_t) == 54, "El contrato ESP-NOW cambio");
_Static_assert(sizeof(TEAM_TAG) - 1 == TAG_LEN, "Tag incompatible");

bool console_decode(const uint8_t *data, size_t length, evento_t *event)
{
    if (!data || !event || length != TAG_LEN + sizeof(evento_t) ||
        memcmp(data, TEAM_TAG, TAG_LEN) != 0) {
        return false;
    }
    evento_t decoded;
    memcpy(&decoded, data + TAG_LEN, sizeof(decoded));
    if ((decoded.tipo != ALERTA_DEAUTH && decoded.tipo != ALERTA_EVILTWIN) ||
        decoded.canal < 1 || decoded.canal > 13 || decoded.severidad > 3) {
        return false;
    }
    *event = decoded;
    return true;
}

const console_entry_t *console_entry(const console_state_t *state, size_t offset)
{
    if (offset >= state->count) {
        return NULL;
    }
    return &state->entries[(state->head + HISTORY_CAPACITY - 1 - offset) % HISTORY_CAPACITY];
}

void console_push(console_state_t *state, const console_entry_t *entry)
{
    state->entries[state->head] = *entry;
    state->entries[state->head].acknowledged = false;
    state->head = (state->head + 1) % HISTORY_CAPACITY;
    if (state->count < HISTORY_CAPACITY) {
        state->count++;
    }
    state->selected = 0;
    state->log_mode = false;
}

void console_acknowledge(console_state_t *state)
{
    if (state->count) {
        size_t offset = state->selected;
        state->entries[(state->head + HISTORY_CAPACITY - 1 - offset) % HISTORY_CAPACITY].acknowledged = true;
    }
}

void console_clear(console_state_t *state)
{
    memset(state, 0, sizeof(*state));
}
