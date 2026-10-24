#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "joystick_orientation.h"

int main(void)
{
    // Direcciones cardinales respecto del OLED con montaje 135 grados CCW.
    assert(strcmp(joystick_direction(JOY_HW_RIGHT | JOY_HW_DOWN), "UP") == 0);
    assert(strcmp(joystick_direction(JOY_HW_DOWN | JOY_HW_LEFT), "RIGHT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_LEFT | JOY_HW_UP), "DOWN") == 0);
    assert(strcmp(joystick_direction(JOY_HW_UP | JOY_HW_RIGHT), "LEFT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_RIGHT), "UP-LEFT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_DOWN), "UP-RIGHT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_LEFT), "DOWN-RIGHT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_UP), "DOWN-LEFT") == 0);
    assert(strcmp(joystick_direction(JOY_HW_RIGHT | JOY_HW_LEFT), "CENTER") == 0);
    assert(strcmp(joystick_direction(JOY_HW_UP | JOY_HW_DOWN), "CENTER") == 0);
    assert(strcmp(joystick_direction(15), "CENTER") == 0);
    // Cada contacto individual conserva una accion de menu distinta.
    assert(joystick_menu_direction(JOY_HW_RIGHT) == JOY_MENU_LEFT);
    assert(joystick_menu_direction(JOY_HW_DOWN) == JOY_MENU_UP);
    assert(joystick_menu_direction(JOY_HW_LEFT) == JOY_MENU_RIGHT);
    assert(joystick_menu_direction(JOY_HW_UP) == JOY_MENU_DOWN);

    joystick_gesture_t gesture = {0};
    assert(joystick_menu_poll(&gesture, JOY_HW_DOWN, 100) == JOY_MENU_NONE);
    // El segundo contacto llega ligeramente despues del primero.
    uint8_t right = JOY_HW_DOWN | JOY_HW_LEFT;
    assert(joystick_menu_poll(&gesture, right, 120) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, right, 159) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, right, 160) == JOY_MENU_RIGHT);
    assert(joystick_menu_poll(&gesture, right, 1000) == JOY_MENU_NONE);
    // Liberacion parcial y rebote de liberacion no generan nuevas acciones.
    assert(joystick_menu_poll(&gesture, JOY_HW_LEFT, 1010) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, JOY_HW_LEFT, 1060) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, 0, 1100) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, JOY_HW_LEFT, 1120) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, 0, 1140) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, 0, 1180) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, JOY_HW_UP, 1200) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, JOY_HW_UP, 1240) == JOY_MENU_DOWN);
    // El contador de milisegundos puede desbordar durante un gesto.
    gesture = (joystick_gesture_t){0};
    assert(joystick_menu_poll(&gesture, JOY_HW_RIGHT, UINT32_MAX - 10) == JOY_MENU_NONE);
    assert(joystick_menu_poll(&gesture, JOY_HW_RIGHT, 29) == JOY_MENU_LEFT);
    puts("OK: rotacion 135 grados, diagonales, contactos simultaneos y gestos");
}
