// Contrato compartido entre la Placa A (oido) y la Placa B (consola).
// FUENTE UNICA: este archivo se COPIA tal cual en main/ de cada proyecto
// (wipds_oido/main/evento.h y wipds_consola/main/evento.h). No retipear ni
// editar una sola copia: las dos placas tienen que compilar bytes identicos,
// porque B castea el mensaje ESP-NOW crudo a evento_t.
#pragma once

#include <stdint.h>

// Mismo tag que espnow_chat: filtra los mensajes de otros equipos de la sala.
#define TEAM_TAG   "NCH1"
#define TAG_LEN    4
#define SSID_MAX   32

// Canal donde la Placa B (consola) escucha ESP-NOW, fijo. La Placa A salta de
// canal para capturar, pero antes de mandar una alerta se fija en este canal,
// manda, y vuelve a saltar. Las dos placas tienen que usar el mismo numero.
#define ESPNOW_CANAL   1

// Flujo de una sola via: A manda, B recibe. B nunca le contesta a A.
// En el aire va [TEAM_TAG][evento_t]. B descarta lo que no empieza con TEAM_TAG
// y castea el resto a evento_t.

typedef enum {
    ALERTA_DEAUTH   = 1,
    ALERTA_EVILTWIN = 2,
} tipo_alerta_t;

// packed: sin padding entre campos, asi el layout no depende del compilador y
// A y B lo interpretan igual byte a byte.
typedef struct __attribute__((packed)) {
    uint8_t  tipo;              // tipo_alerta_t
    uint8_t  canal;             // canal donde se vio (1..13)
    int8_t   rssi;              // intensidad de senal
    uint8_t  severidad;        // 0..3 (para destacar/tono en la consola)
    uint8_t  bssid[6];          // red afectada / sospechosa
    uint8_t  victima[6];        // MAC destino del deauth (todo 0 si no aplica)
    uint16_t intensidad;        // deauth: frames/s. evil twin: score
    char     ssid[SSID_MAX];    // nombre de red (evil twin; vacio en deauth)
    uint32_t ts;                // millis del evento en la Placa A
} evento_t;
