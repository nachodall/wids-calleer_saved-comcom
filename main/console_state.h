#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "evento.h"

#define HISTORY_CAPACITY 16

typedef struct {
    evento_t event;
    uint8_t source[6];
    uint32_t received_ms;
    bool acknowledged;
} console_entry_t;

typedef struct {
    console_entry_t entries[HISTORY_CAPACITY];
    size_t head;
    size_t count;
    size_t selected;
    bool log_mode;
} console_state_t;

bool console_decode(const uint8_t *data, size_t length, evento_t *event);
void console_push(console_state_t *state, const console_entry_t *entry);
const console_entry_t *console_entry(const console_state_t *state, size_t offset);
void console_acknowledge(console_state_t *state);
void console_clear(console_state_t *state);
