// Orientacion 135 grados y agrupacion de contactos para el menu de la consola.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Bits de contactos fisicos, antes de corregir el montaje.
enum { JOY_HW_RIGHT = 1, JOY_HW_DOWN = 2, JOY_HW_LEFT = 4, JOY_HW_UP = 8 };
typedef struct { int x; int y; } joystick_vector_t;

// Montaje girado 135 grados antihorario respecto de la pantalla.
// Coordenadas de pantalla: +x derecha, +y abajo. Una tecla fisica queda
// diagonal; dos contactos vecinos forman una direccion cardinal corregida.
static inline joystick_vector_t joystick_correct(uint8_t contacts)
{
    int x = !!(contacts & JOY_HW_RIGHT) - !!(contacts & JOY_HW_LEFT);
    int y = !!(contacts & JOY_HW_DOWN) - !!(contacts & JOY_HW_UP);
    return (joystick_vector_t){ .x = -x + y, .y = -x - y };
}

static inline const char *joystick_direction(uint8_t contacts)
{
    joystick_vector_t v = joystick_correct(contacts);
    if (v.y < 0) return v.x < 0 ? "UP-LEFT" : v.x > 0 ? "UP-RIGHT" : "UP";
    if (v.y > 0) return v.x < 0 ? "DOWN-LEFT" : v.x > 0 ? "DOWN-RIGHT" : "DOWN";
    return v.x < 0 ? "LEFT" : v.x > 0 ? "RIGHT" : "CENTER";
}

static inline const char *joystick_screen_direction(uint8_t contacts)
{
    joystick_vector_t v = joystick_correct(contacts);
    if (v.y < 0) return v.x < 0 ? "UP-L" : v.x > 0 ? "UP-R" : "UP";
    if (v.y > 0) return v.x < 0 ? "DOWN-L" : v.x > 0 ? "DOWN-R" : "DOWN";
    return v.x < 0 ? "LEFT" : v.x > 0 ? "RIGHT" : "CENTER";
}

enum { JOY_MENU_NONE, JOY_MENU_UP, JOY_MENU_DOWN, JOY_MENU_LEFT, JOY_MENU_RIGHT };

static inline int joystick_menu_direction(uint8_t contacts)
{
    joystick_vector_t v = joystick_correct(contacts);
    // A 135 grados hay empates exactos entre dos cardinales. Resolverlos
    // siempre hacia el siguiente cardinal antihorario conserva las cuatro
    // acciones incluso si el joystick solo permite cerrar un contacto.
    if (v.x < 0 && v.y < 0) return JOY_MENU_LEFT;
    if (v.x > 0 && v.y < 0) return JOY_MENU_UP;
    if (v.x > 0 && v.y > 0) return JOY_MENU_RIGHT;
    if (v.x < 0 && v.y > 0) return JOY_MENU_DOWN;
    if (v.y < 0) return JOY_MENU_UP;
    if (v.y > 0) return JOY_MENU_DOWN;
    if (v.x < 0) return JOY_MENU_LEFT;
    if (v.x > 0) return JOY_MENU_RIGHT;
    return JOY_MENU_NONE;
}

typedef struct {
    uint8_t contacts;
    uint32_t changed_ms;
    bool consumed;
} joystick_gesture_t;

// Espera 40 ms adicionales para agrupar dos contactos vecinos. Una accion
// por gesto; la liberacion parcial no dispara una direccion diferente.
static inline int joystick_menu_poll(joystick_gesture_t *gesture, uint8_t contacts, uint32_t now)
{
    if (contacts != gesture->contacts) {
        gesture->contacts = contacts;
        gesture->changed_ms = now;
    }
    if (now - gesture->changed_ms < 40) return JOY_MENU_NONE;
    if (contacts == 0) {
        gesture->consumed = false;
        return JOY_MENU_NONE;
    }
    if (gesture->consumed) return JOY_MENU_NONE;
    gesture->consumed = true;
    return joystick_menu_direction(contacts);
}
