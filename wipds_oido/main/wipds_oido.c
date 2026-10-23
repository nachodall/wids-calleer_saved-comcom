// Placa A (el oido) - Fase A2: captura + parser de frames 802.11.
// Arranca WiFi + ESP-NOW (base de espnow_chat), prende el modo centinela (modo
// promiscuo en jerga 802.11: la radio entrega todos los frames del aire) y salta
// de canal 1..13. De cada frame lee el "frame control" para sacar tipo y subtipo,
// y clasifica: beacon, deauth, disassoc u otros. De cada beacon saca el nombre de
// red (SSID), la MAC del AP (BSSID) y el RSSI, y arma una lista de redes vistas
// (una vez por red). Imprime las redes nuevas y un resumen por segundo. Sin
// display. Todavia no detecta ataques (eso es A3) ni manda nada por ESP-NOW.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "evento.h"

// Deja WiFi + ESP-NOW listos, igual que espnow_chat. El orden importa: cada paso
// necesita el anterior. NVS (la radio guarda calibracion ahi), red base, WiFi en
// modo estacion SIN conectarse a ninguna red, canal fijo ESPNOW_CANAL, ESP-NOW.
// En A0 todavia no manda ni recibe nada: solo confirma que la base arranca.
static void espnow_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CANAL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
}

// ---------- captura + parser 802.11 (Fase A2) ----------

#define CANAL_MIN  1
#define CANAL_MAX  13
#define HOP_MS     250    // cuanto se queda escuchando cada canal
#define MAX_REDES  32     // cuantas redes distintas recordamos para listar

// Subtipos de frame de management que nos importan (los 4 bits de subtipo).
#define SUB_BEACON    0x08
#define SUB_DISASSOC  0x0A
#define SUB_DEAUTH    0x0C

// Una red vista por su beacon. Se guarda una sola vez por BSSID (dedupe).
typedef struct {
    uint8_t bssid[6];
    char    ssid[SSID_MAX + 1];
    int8_t  rssi;
    uint8_t canal;
} red_vista_t;

// Contadores del ultimo segundo: los suma promisc_cb, app_main los lee y resetea.
static volatile uint32_t c_beacon, c_deauth, c_disassoc, c_otros;

// Tabla de redes vistas. La escribe promisc_cb (tarea WiFi) y la lee app_main; el
// spinlock evita que se pisen. Las entradas solo se agregan, nunca se borran.
static red_vista_t redes[MAX_REDES];
static volatile int n_redes;
static portMUX_TYPE redes_mux = portMUX_INITIALIZER_UNLOCKED;

// La radio llama a esta funcion por CADA frame 802.11. Lee el frame control para
// sacar tipo y subtipo, y clasifica: cuenta beacon, deauth, disassoc u otros. De
// los beacons saca SSID (nombre) y BSSID (MAC del AP) y los guarda una vez por
// red. Hace lo minimo y NO imprime: imprimir aca haria perder frames.
static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type_pkt)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24) {            // el header de management son 24 bytes
        return;
    }

    uint8_t ftype   = (p[0] >> 2) & 0x3;   // 0 = management
    uint8_t subtype = (p[0] >> 4) & 0xF;

    if (ftype != 0) {          // data y control no nos interesan todavia
        c_otros++;
        return;
    }
    if (subtype == SUB_DEAUTH)   { c_deauth++;   return; }
    if (subtype == SUB_DISASSOC) { c_disassoc++; return; }
    if (subtype != SUB_BEACON)   { c_otros++;    return; }

    c_beacon++;

    // En un beacon: addr3 (bytes 16..21) = BSSID. El SSID va en los "tagged
    // params", que arrancan en el offset 36; el primer tag (numero 0) es el SSID,
    // con su largo en el byte siguiente.
    const uint8_t *bssid = p + 16;
    if (len < 38) {
        return;
    }
    uint8_t tag_num  = p[36];
    uint8_t ssid_len = p[37];
    if (tag_num != 0 || ssid_len > SSID_MAX || len < 38 + ssid_len) {
        return;
    }

    taskENTER_CRITICAL(&redes_mux);
    bool existe = false;
    for (int i = 0; i < n_redes; i++) {
        if (memcmp(redes[i].bssid, bssid, 6) == 0) {
            existe = true;
            break;
        }
    }
    if (!existe && n_redes < MAX_REDES) {
        red_vista_t *r = &redes[n_redes];
        memcpy(r->bssid, bssid, 6);
        memcpy(r->ssid, p + 38, ssid_len);
        r->ssid[ssid_len] = '\0';
        r->rssi = pkt->rx_ctrl.rssi;
        r->canal = pkt->rx_ctrl.channel;
        n_redes++;
    }
    taskEXIT_CRITICAL(&redes_mux);
}

// Salta de canal 1..13, uno cada HOP_MS, en loop, sin imprimir. La radio escucha
// un solo canal de 2.4 GHz a la vez, asi que para ver todo el aire hay que rotar.
static void hop_task(void *arg)
{
    uint8_t ch = CANAL_MIN;
    while (1) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
        vTaskDelay(pdMS_TO_TICKS(HOP_MS));
        ch = (ch >= CANAL_MAX) ? CANAL_MIN : ch + 1;
    }
}

void app_main(void)
{
    espnow_start();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    printf("wipds_oido A2 | MAC %02x:%02x:%02x:%02x:%02x:%02x | tag \"%s\"\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], TEAM_TAG);

    // Sin ahorro de energia: dormir la radio haria perder frames al capturar.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promisc_cb));
    printf("modo centinela ON | RAM libre: %u bytes\n", (unsigned)esp_get_free_heap_size());

    xTaskCreate(hop_task, "hop", 2048, NULL, 4, NULL);

    int impresas = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Imprime las redes nuevas descubiertas desde la vuelta anterior.
        int n = n_redes;
        for (int i = impresas; i < n; i++) {
            printf("RED  %-20s  %02x:%02x:%02x:%02x:%02x:%02x  ch%-2d  %d dBm\n",
                   redes[i].ssid,
                   redes[i].bssid[0], redes[i].bssid[1], redes[i].bssid[2],
                   redes[i].bssid[3], redes[i].bssid[4], redes[i].bssid[5],
                   redes[i].canal, redes[i].rssi);
        }
        impresas = n;

        // Resumen de clasificacion del ultimo segundo, y reset de contadores.
        uint32_t b = c_beacon, d = c_deauth, da = c_disassoc, o = c_otros;
        c_beacon = c_deauth = c_disassoc = c_otros = 0;
        printf("== beacon/s:%lu  deauth/s:%lu  disassoc/s:%lu  otros/s:%lu  | redes:%d ==\n",
               (unsigned long)b, (unsigned long)d, (unsigned long)da, (unsigned long)o, n);
    }
}
