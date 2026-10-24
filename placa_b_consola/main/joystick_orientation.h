// Correccion de orientacion del joystick para la Placa B (consola). RECONSTRUIDO a
// partir del uso en demo_main.c/joystick.c y de la tabla del README (el archivo
// original no estaba en el repo). El joystick esta montado girado 135 grados a la
// izquierda visto desde el OLED, asi que el contacto fisico no coincide con la
// direccion real en pantalla; este header hace esa correccion.
//
// OJO: las cadenas y valores exactos son una reconstruccion razonable a partir del
// comportamiento documentado, no el archivo original del autor de la demo. Antes de
// confiar en el texto exacto que imprime, reconciliar con su version si la recupera.
#pragma once

#include <stdint.h>
#include "joystick.h"

// Bits de contacto fisico (uno por pin), en el mismo orden que el enum JOY_* de
// joystick.h: RIGHT=IO10, DOWN=IO11, LEFT=IO12, UP=IO9 (nombres de pin, no de
// pantalla: estan girados 135 grados).
#define JOY_HW_RIGHT  (1u << JOY_RIGHT)
#define JOY_HW_DOWN   (1u << JOY_DOWN)
#define JOY_HW_LEFT   (1u << JOY_LEFT)
#define JOY_HW_UP     (1u << JOY_UP)

// Accion de menu que puede producir un gesto del joystick.
typedef enum {
    JOY_MENU_NONE = 0,
    JOY_MENU_UP,
    JOY_MENU_DOWN,
    JOY_MENU_LEFT,
    JOY_MENU_RIGHT,
} joystick_menu_t;

// Estado minimo para detectar flancos de contacto (evita repetir la accion
// mientras se mantiene apretado o al soltar parcialmente un combo).
typedef struct {
    uint8_t last_contacts;
} joystick_gesture_t;

// Texto de diagnostico (para el log "CORREGIDO 135 IZQ: %s"): nombra la direccion
// real en pantalla ya corregida. Dos contactos vecinos = cardinal; un solo
// contacto = diagonal (por el giro de 135 grados no hay un cardinal puro).
static inline const char *joystick_direction(uint8_t contacts)
{
    switch (contacts) {
        case JOY_HW_RIGHT | JOY_HW_DOWN:  return "UP";      // IO10+IO11 -> arriba
        case JOY_HW_DOWN  | JOY_HW_LEFT:  return "RIGHT";   // IO11+IO12 -> derecha
        case JOY_HW_LEFT  | JOY_HW_UP:    return "DOWN";    // IO12+IO9  -> abajo
        case JOY_HW_UP    | JOY_HW_RIGHT: return "LEFT";    // IO9+IO10  -> izquierda
        case JOY_HW_RIGHT: return "UP-L";                   // IO10 solo -> arriba-izq
        case JOY_HW_DOWN:  return "UP-R";                   // IO11 solo -> arriba-der
        case JOY_HW_LEFT:  return "DOWN-R";                 // IO12 solo -> abajo-der
        case JOY_HW_UP:    return "DOWN-L";                 // IO9  solo -> abajo-izq
        default: return "-";                                 // nada o combo invalido
    }
}

// Version corta para la cabecera de la pantalla OLED (espacio limitado).
static inline const char *joystick_screen_direction(uint8_t contacts)
{
    switch (contacts) {
        case JOY_HW_RIGHT | JOY_HW_DOWN:  return "U";
        case JOY_HW_DOWN  | JOY_HW_LEFT:  return "R";
        case JOY_HW_LEFT  | JOY_HW_UP:    return "D";
        case JOY_HW_UP    | JOY_HW_RIGHT: return "L";
        case JOY_HW_RIGHT: return "UL";
        case JOY_HW_DOWN:  return "UR";
        case JOY_HW_LEFT:  return "DR";
        case JOY_HW_UP:    return "DL";
        default: return "-";
    }
}

// Resuelve un solo contacto a una direccion cardinal de menu (sin diagonales),
// segun la tabla de desempate del README: IO10->izquierda, IO11->arriba,
// IO12->derecha, IO9->abajo. Dos contactos simultaneos no navegan el menu (se
// reservan para gestos futuros); solo un pin estable a la vez dispara una accion.
static inline joystick_menu_t joystick_menu_poll(joystick_gesture_t *g, uint8_t contacts, uint32_t now_ms)
{
    (void)now_ms;
    if (contacts == g->last_contacts) {
        return JOY_MENU_NONE;              // sin cambio: ni flanco de apretado ni de soltado
    }
    uint8_t before = g->last_contacts;
    g->last_contacts = contacts;
    if (before != 0 || contacts == 0) {
        return JOY_MENU_NONE;              // solo dispara en el flanco 0 -> un solo pin
    }
    switch (contacts) {
        case JOY_HW_RIGHT: return JOY_MENU_LEFT;
        case JOY_HW_DOWN:  return JOY_MENU_UP;
        case JOY_HW_LEFT:  return JOY_MENU_RIGHT;
        case JOY_HW_UP:    return JOY_MENU_DOWN;
        default:           return JOY_MENU_NONE;   // combo de 2+ pines: no navega el menu
    }
}
