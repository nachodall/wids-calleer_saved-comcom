#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { JOY_RIGHT, JOY_DOWN, JOY_LEFT, JOY_UP, JOY_COUNT };

typedef struct {
    int raw;
    int stable;
    uint32_t raw_edges;
    uint32_t presses;
    uint32_t changed_ms;
} joystick_key_t;

extern joystick_key_t joystick[JOY_COUNT];
extern const char *const JOY_NAMES[JOY_COUNT];
extern const int JOY_PINS[JOY_COUNT];

void joystick_init(void);
// Devuelve true al cambiar un nivel crudo o confirmar un cambio estable.
bool joystick_poll(uint32_t now_ms);
uint8_t joystick_contacts(void);
