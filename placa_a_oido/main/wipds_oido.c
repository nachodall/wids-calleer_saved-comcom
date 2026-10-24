// Placa A (el oido) - Fase A4: detectores de deauth y evil twin, scopeados a MI red.
// Arranca WiFi + ESP-NOW (base de espnow_chat), prende el modo centinela (la radio
// le entrega todos los frames del aire que pasan, no solo los suyos) y salta de
// canal 1..13. Parsea cada frame y lista las redes vistas. Dos detectores, los dos
// SOLO sobre MIS redes (tabla MIS_REDES, con nombre + MAC de cada una):
//  - DEAUTH: cuenta deauth/disassoc contra mi BSSID en una ventana corta; si pasan
//    un umbral bajo, alerta.
//  - EVIL TWIN: si aparece un beacon con el NOMBRE de mi red desde una MAC que no es
//    la mia, alerta (alguien esta copiando mi red).
// El resto del aire es solo contexto, nunca alarma. Todo pasivo: solo escucha, nunca
// transmite. KEY1 dispara un auto-test (simula un deauth contra mi red SIN transmitir
// nada) para ver la alerta sin un ataque real; KEY2 es "modo aprender": fija como mi
// red el BSSID que en ese momento usa mi SSID (util si el hotspot cambia de MAC, sin
// recompilar). Sin display. Todavia no manda las alertas por ESP-NOW (eso con Placa B).
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
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

// ---------- captura + parser 802.11 (Fase A4) ----------

#define CANAL_MIN  1
#define CANAL_MAX  13
#define HOP_MS     250    // cuanto se queda escuchando cada canal
#define MAX_REDES  32     // cuantas redes distintas recordamos para listar
#define PIN_KEY1   14     // boton KEY1: auto-test de deauth (ver boton_task)
#define PIN_KEY2   21     // boton KEY2: modo aprender mi red (ver boton_task)

// Subtipos de frame de management que nos importan (los 4 bits de subtipo).
#define SUB_BEACON    0x08
#define SUB_DISASSOC  0x0A
#define SUB_DEAUTH    0x0C

// Umbral BAJO y ventana de varios segundos. Como la deteccion es solo sobre MI red
// (donde el piso normal de deauth es ~0), con pocos frames en la ventana alcanza
// para marcar ataque, incluso uno quirurgico de rafaga lenta. Sobre todo el aire
// haria falta un umbral mucho mas alto para no tener falsos positivos.
#define UMBRAL_DEAUTH  5
#define VENTANA_SEG    3

// Evil twin: cuantos segundos sin ver al impostor para "olvidarlo" y, si vuelve,
// volver a alertar. Mas que el ciclo de canales (~3s) para no spamear por el hopping.
#define ET_GRACIA_SEG  6

// Una red vista por su beacon. Se guarda una sola vez por BSSID (dedupe).
typedef struct {
    uint8_t bssid[6];
    char    ssid[SSID_MAX + 1];
    int8_t  rssi;
    uint8_t canal;
} red_vista_t;

// Contadores del ultimo segundo: los suma centinela_cb, app_main los lee y resetea.
// c_deauth/c_disassoc cuentan TODO el aire (contexto); c_mi, deauth contra MI red;
// c_evil, beacons de evil twin (nombre de mi red desde otra MAC).
static volatile uint32_t c_beacon, c_deauth, c_disassoc, c_otros;
static volatile uint32_t c_mi, c_evil;

// Tabla de redes vistas. La escribe centinela_cb (tarea WiFi) y la lee app_main; el
// spinlock evita que se pisen. Las entradas solo se agregan, nunca se borran.
static red_vista_t redes[MAX_REDES];
static volatile int n_redes;
static portMUX_TYPE redes_mux = portMUX_INITIALIZER_UNLOCKED;

// Contexto del ultimo deauth/disassoc visto, para describir la alerta. Lo escribe
// centinela_cb y lo lee app_main, siempre bajo redes_mux.
static uint8_t atk_bssid[6];     // addr3 del frame = red atacada
static uint8_t atk_victima[6];   // addr1 del frame = destino (victima, o broadcast)
static int8_t  atk_rssi;
static uint8_t atk_canal;

// Contexto del ultimo evil twin visto, para la alerta. Mismo esquema (bajo redes_mux).
static uint8_t et_bssid[6];           // MAC del impostor
static char    et_ssid[SSID_MAX + 1]; // el nombre copiado (= mi red)
static int8_t  et_rssi;
static uint8_t et_canal;

// MIS_REDES: tus propias redes, con NOMBRE (SSID) y MAC del router (BSSID). El nombre
// lo usa el detector de evil twin; la MAC, el de deauth. El BSSID podes re-fijarlo en
// caliente con KEY2 (modo aprender), util si el hotspot cambia de MAC. Por eso NO es
// const. Para una demo estable igual conviene un router de verdad (MAC fija).
typedef struct {
    char    ssid[SSID_MAX + 1];
    uint8_t bssid[6];
} mi_red_t;

static mi_red_t MIS_REDES[] = {
    {"POCO X8 Pro", {0x62, 0xb6, 0x65, 0xba, 0x72, 0xf1}},   //  <-- tu red (nombre + MAC)
};
#define N_MIS_REDES  (sizeof(MIS_REDES) / sizeof(MIS_REDES[0]))

// Devuelve true si bssid es el de una red propia (lo usa el detector de deauth).
static bool es_mi_red(const uint8_t *bssid)
{
    for (size_t i = 0; i < N_MIS_REDES; i++) {
        if (memcmp(MIS_REDES[i].bssid, bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

// Devuelve true si (ssid, bssid) es un evil twin: tiene el NOMBRE de una red mia pero
// desde una MAC que NO es la mia. O sea, alguien copia el nombre de mi red desde otro
// aparato. Si el nombre Y la MAC son los mios, es mi red real, no un gemelo (false).
static bool es_evil_twin(const char *ssid, const uint8_t *bssid)
{
    for (size_t i = 0; i < N_MIS_REDES; i++) {
        if (strcmp(MIS_REDES[i].ssid, ssid) == 0 &&
            memcmp(MIS_REDES[i].bssid, bssid, 6) != 0) {
            return true;
        }
    }
    return false;
}

// Candidato para el modo aprender (KEY2): BSSID del ultimo beacon visto cuyo nombre
// coincide con una red mia. KEY2 lo copia a MIS_REDES para re-fijar "mi red" sin
// recompilar. cand_idx dice a cual de MIS_REDES corresponde (-1 = todavia nada).
static int     cand_idx = -1;
static uint8_t cand_bssid[6];

// Callback del modo centinela: la radio lo llama por CADA frame 802.11. Lee el
// frame control para sacar tipo y subtipo, y clasifica beacon/deauth/disassoc. De
// los beacons saca SSID (nombre) y BSSID (MAC del AP) y los guarda una vez por
// red. Hace lo minimo y NO imprime: imprimir aca haria perder frames.
static void centinela_cb(void *buf, wifi_promiscuous_pkt_type_t type_pkt)
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
    if (subtype == SUB_DEAUTH || subtype == SUB_DISASSOC) {
        if (subtype == SUB_DEAUTH) {
            c_deauth++;
        } else {
            c_disassoc++;
        }
        // addr3 (bytes 16..21) = BSSID de la red atacada. Solo nos alarma si es MIA.
        // Si lo es, suma al contador scopeado y guarda la victima (addr1) para la
        // alerta. Si no, ya quedo contado en el "ambiente" de arriba y listo.
        if (es_mi_red(p + 16)) {
            c_mi++;
            taskENTER_CRITICAL(&redes_mux);
            memcpy(atk_victima, p + 4, 6);
            memcpy(atk_bssid, p + 16, 6);
            atk_rssi = pkt->rx_ctrl.rssi;
            atk_canal = pkt->rx_ctrl.channel;
            taskEXIT_CRITICAL(&redes_mux);
        }
        return;
    }
    if (subtype != SUB_BEACON) { c_otros++; return; }

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
    char ssid[SSID_MAX + 1] = {0};
    memcpy(ssid, p + 38, ssid_len);

    // Modo aprender: si el beacon trae el nombre de una red mia, recordar su BSSID
    // como candidato (KEY2 lo fija como mi red).
    for (size_t i = 0; i < N_MIS_REDES; i++) {
        if (strcmp(MIS_REDES[i].ssid, ssid) == 0) {
            taskENTER_CRITICAL(&redes_mux);
            cand_idx = (int)i;
            memcpy(cand_bssid, bssid, 6);
            taskEXIT_CRITICAL(&redes_mux);
            break;
        }
    }

    // Evil twin: este beacon trae el NOMBRE de mi red pero desde otra MAC.
    if (es_evil_twin(ssid, bssid)) {
        c_evil++;
        taskENTER_CRITICAL(&redes_mux);
        memcpy(et_bssid, bssid, 6);
        memcpy(et_ssid, ssid, sizeof(et_ssid));
        et_rssi = pkt->rx_ctrl.rssi;
        et_canal = pkt->rx_ctrl.channel;
        taskEXIT_CRITICAL(&redes_mux);
    }

    // Lo guardamos en la lista de redes vistas (una vez por BSSID).
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
        memcpy(r->ssid, ssid, sizeof(r->ssid));
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

// Configura KEY1 como entrada con pull-up. El boton conecta a GND: suelto = 1,
// apretado = 0.
static void boton_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_KEY1) | (1ULL << PIN_KEY2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

// Dos botones, los dos legales (no transmiten nada):
//  - KEY1 (auto-test): inyecta una rafaga de deauth FALSOS en el contador de "mi red"
//    para que la alerta se dispare, y asi verla o mostrarla sin un ataque real.
//  - KEY2 (modo aprender): fija como "mi red" el BSSID que en ese momento usa mi SSID
//    (el candidato que vio el callback). Util si el hotspot cambia de MAC, sin
//    recompilar. Apretalo cuando SOLO este tu red real (sin gemelo), asi no aprende
//    la MAC de un impostor. Los dos detectan el flanco de apretado (1 -> 0).
static void boton_task(void *arg)
{
    static const uint8_t victima_sim[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    int prev1 = 1, prev2 = 1;
    while (1) {
        int k1 = gpio_get_level(PIN_KEY1);
        int k2 = gpio_get_level(PIN_KEY2);

        if (prev1 == 1 && k1 == 0) {          // KEY1: auto-test de deauth
            printf("\n[KEY1] auto-test: simulando deauth contra mi red (sin transmitir)\n");
            taskENTER_CRITICAL(&redes_mux);
            memcpy(atk_bssid, MIS_REDES[0].bssid, 6);
            memcpy(atk_victima, victima_sim, 6);
            atk_rssi = -40;
            atk_canal = 0;
            taskEXIT_CRITICAL(&redes_mux);
            c_mi += 40;                       // rafaga simulada, bien por encima del umbral
        }

        if (prev2 == 1 && k2 == 0) {          // KEY2: aprender mi red
            uint8_t b[6] = {0};
            taskENTER_CRITICAL(&redes_mux);
            int idx = cand_idx;
            if (idx >= 0) {
                memcpy(b, cand_bssid, 6);
                memcpy(MIS_REDES[idx].bssid, b, 6);
            }
            taskEXIT_CRITICAL(&redes_mux);
            if (idx >= 0) {
                printf("\n[KEY2] aprendido: \"%s\" ahora es mi red en %02x:%02x:%02x:%02x:%02x:%02x\n\n",
                       MIS_REDES[idx].ssid, b[0], b[1], b[2], b[3], b[4], b[5]);
            } else {
                printf("\n[KEY2] todavia no vi un beacon con el nombre de mi red; espera unos segundos\n\n");
            }
        }

        prev1 = k1;
        prev2 = k2;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    espnow_start();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    printf("wipds_oido A4 | MAC %02x:%02x:%02x:%02x:%02x:%02x | tag \"%s\"\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], TEAM_TAG);

    // Sin ahorro de energia: dormir la radio haria perder frames al capturar.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(centinela_cb));
    printf("modo centinela ON | RAM libre: %u bytes\n", (unsigned)esp_get_free_heap_size());

    xTaskCreate(hop_task, "hop", 2048, NULL, 4, NULL);

    boton_init();
    xTaskCreate(boton_task, "boton", 2048, NULL, 4, NULL);

    int impresas = 0;
    uint32_t ventana[VENTANA_SEG] = {0};   // deauth contra mi red, un slot por segundo
    int vent_i = 0;
    bool et_activo = false;                // ya avisamos del evil twin y sigue presente
    int et_gracia = 0;                     // segundos de margen antes de "olvidarlo"
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Imprime las redes nuevas descubiertas desde la vuelta anterior. Las que
        // esten en MIS_REDES salen marcadas, para confirmar que cargaste bien el BSSID.
        int n = n_redes;
        for (int i = impresas; i < n; i++) {
            printf("RED  %-20s  %02x:%02x:%02x:%02x:%02x:%02x  ch%-2d  %d dBm%s\n",
                   redes[i].ssid,
                   redes[i].bssid[0], redes[i].bssid[1], redes[i].bssid[2],
                   redes[i].bssid[3], redes[i].bssid[4], redes[i].bssid[5],
                   redes[i].canal, redes[i].rssi,
                   es_mi_red(redes[i].bssid) ? "  <- MIA"
                   : es_evil_twin(redes[i].ssid, redes[i].bssid) ? "  <- EVIL TWIN?!"
                   : "");
        }
        impresas = n;

        // Lee y resetea los contadores del segundo.
        uint32_t b = c_beacon, d = c_deauth, da = c_disassoc, o = c_otros, mi = c_mi, ev = c_evil;
        c_beacon = c_deauth = c_disassoc = c_otros = c_mi = c_evil = 0;

        // Ventana deslizante: deauth contra MI red en los ultimos VENTANA_SEG segundos.
        ventana[vent_i] = mi;
        vent_i = (vent_i + 1) % VENTANA_SEG;
        uint32_t suma = 0;
        for (int i = 0; i < VENTANA_SEG; i++) {
            suma += ventana[i];
        }

        // Deteccion quirurgica: solo sobre MI red, umbral bajo.
        if (suma > UMBRAL_DEAUTH) {
            uint8_t bssid[6], vict[6];
            int8_t rssi;
            uint8_t canal;
            taskENTER_CRITICAL(&redes_mux);
            memcpy(bssid, atk_bssid, 6);
            memcpy(vict, atk_victima, 6);
            rssi = atk_rssi;
            canal = atk_canal;
            taskEXIT_CRITICAL(&redes_mux);
            int sev = (suma > 60) ? 3 : (suma > 20) ? 2 : 1;
            printf("\n*** ALERTA: DEAUTH CONTRA MI RED  sev%d  %lu en %ds ***\n",
                   sev, (unsigned long)suma, VENTANA_SEG);
            printf("    mi red  : %02x:%02x:%02x:%02x:%02x:%02x  ch%d  %d dBm\n",
                   bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], canal, rssi);
            printf("    victima : %02x:%02x:%02x:%02x:%02x:%02x\n\n",
                   vict[0], vict[1], vict[2], vict[3], vict[4], vict[5]);
        }

        // Deteccion evil twin: alcanza con UN beacon (mi nombre desde otra MAC).
        // Avisa al aparecer, calla mientras sigue (la gracia cubre los huecos del
        // hopping), y vuelve a avisar solo si estuvo ausente ET_GRACIA_SEG segundos.
        if (ev > 0) {
            if (!et_activo) {
                uint8_t bssid[6];
                char    ssid[SSID_MAX + 1];
                int8_t  rssi;
                uint8_t canal;
                taskENTER_CRITICAL(&redes_mux);
                memcpy(bssid, et_bssid, 6);
                memcpy(ssid, et_ssid, sizeof(ssid));
                rssi = et_rssi;
                canal = et_canal;
                taskEXIT_CRITICAL(&redes_mux);
                printf("\n*** ALERTA: EVIL TWIN (red gemela) ***\n");
                printf("    tu red \"%s\" aparece desde OTRA MAC:\n", ssid);
                printf("    impostor: %02x:%02x:%02x:%02x:%02x:%02x  ch%d  %d dBm\n\n",
                       bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], canal, rssi);
                et_activo = true;
            }
            et_gracia = ET_GRACIA_SEG;
        } else if (et_activo && --et_gracia <= 0) {
            et_activo = false;
        }

        printf("== aire: beacon/s:%lu deauth/s:%lu otros/s:%lu | MI RED deauth(%ds):%lu evil:%lu | redes:%d ==\n",
               (unsigned long)b, (unsigned long)(d + da), (unsigned long)o,
               VENTANA_SEG, (unsigned long)suma, (unsigned long)ev, n);
    }
}
