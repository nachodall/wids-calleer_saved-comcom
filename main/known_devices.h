#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t mac[6];
    const char *name;
} known_device_t;

// Agregar entradas antes del terminador. Nombres ASCII de hasta 17 caracteres.
// Ejemplo: {{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}, "MI-PC"},
static const known_device_t KNOWN_DEVICES[] = {
    {{0}, NULL},
};
