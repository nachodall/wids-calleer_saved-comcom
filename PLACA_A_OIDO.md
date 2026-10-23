# Placa A — El Oído (`wipds_oido`)

Rol: escuchar el aire en modo promiscuo, detectar ataques, mandar alertas por
ESP-NOW. **Sin display** (ahorra RAM y no pierde frames por dibujar).

Cada fase deja algo que se verifica por el **monitor serie** antes de pasar a la
siguiente. Si una fase no se ve funcionar, no se sigue.

Base de reuso: `espnow_chat` (arranque de ESP-NOW, filtro por tag, `send_text`).
Acá se le saca todo lo del OLED, los botones y la CLI.

---

## Contrato compartido con la Placa B

Las dos placas tienen que compilar el **mismo** struct del evento. Es el único
punto de acople. Se cierra antes de arrancar y no se toca después.

El archivo real está en [`wipds/evento.h`](evento.h) — fuente única. Se **copia
tal cual** en `main/` de cada proyecto (no se retipea), así A y B compilan bytes
idénticos. Está marcado `__attribute__((packed))` para que el layout no dependa
del compilador. Resumen de los campos:

```c
typedef struct __attribute__((packed)) {
    uint8_t  tipo;             // ALERTA_DEAUTH=1 / ALERTA_EVILTWIN=2
    uint8_t  canal;            // canal donde se vio (1..13)
    int8_t   rssi;             // intensidad de señal
    uint8_t  severidad;        // 0..3 (para destacar/tono en la consola)
    uint8_t  bssid[6];         // red afectada / sospechosa
    uint8_t  victima[6];       // MAC destino del deauth (0 si no aplica)
    uint16_t intensidad;       // deauth: frames/s. evil twin: score
    char     ssid[SSID_MAX];   // nombre de red (evil twin; vacío en deauth)
    uint32_t ts;               // millis del evento en la Placa A
} evento_t;
```

En el aire va `[TEAM_TAG][evento_t]`, igual que el chat manda `[TEAM_TAG][texto]`.
La Placa B filtra por tag y castea el resto a `evento_t`.

**Flujo de una sola vía:** A manda, B recibe. B nunca le contesta a A. El canal
donde B escucha es `ESPNOW_CANAL` (en `evento.h`, fijo). A salta de canal para
capturar, pero antes de mandar una alerta se fija en `ESPNOW_CANAL`, manda, y
vuelve a saltar (ver Fase A3).

---

## Fase A0 — Esqueleto sin display ✅ punto de partida

- **Objetivo:** tener `wipds_oido` compilando: copia de `espnow_chat` con todo lo
  del OLED / botones / CLI borrado, dejando solo el arranque de ESP-NOW.
- **Qué queda:** `espnow_start()`, `send_text()` (lo adaptamos a mandar bytes, no
  texto), `app_main` con un loop vacío que imprime "vivo" cada segundo.
- **Verificación serie:** aparece "vivo" cada segundo y la MAC al bootear. No crashea.
- **Por qué primero:** confirma que la base compila sin el display antes de sumar
  nada nuevo. RAM libre impresa por serie = línea base para comparar después.

---

## Fase A1 — Captura cruda + channel hopping

- **Objetivo:** contar cuántos frames pasan por segundo, saltando de canal.
- **Qué se hace:**
  - `esp_wifi_set_promiscuous(true)` + `esp_wifi_set_promiscuous_rx_cb(cb)`.
  - En el callback: **solo** `contador++`. Nada lento adentro (corre seguido).
  - Tarea de hopping: `esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)`, ch de 1 a
    13, cada ~250 ms.
  - Cada segundo: imprimir `canal=X frames=Y`.
- **Verificación serie:** el número sube y cambia según el canal. Nunca cero en
  todos los canales (siempre hay WiFi alrededor).
- **Feedback inmediato:** se ve el aire "respirar" — canales con router cerca dan
  más frames.

---

## Fase A2 — Parser de frames 802.11

- **Objetivo:** de cada frame sacar tipo/subtipo y las MAC. El contenido no importa.
- **Qué se saca:**
  - `frame control` (primeros 2 bytes del payload) → tipo y subtipo.
  - MAC destino, origen, BSSID (bytes 4..9, 10..15, 16..21 del header).
  - RSSI y canal vienen en `pkt->rx_ctrl` (no en el payload).
- **Subtipos que importan:** `0x08` beacon, `0x0A` disassoc, `0x0C` deauth.
- **Qué se hace:** en el callback, además de contar, clasificar por subtipo e
  incrementar contadores separados (beacons, deauth, disassoc).
- **Verificación serie:** cada segundo imprime `beacons=.. deauth=.. disassoc=..`.
  Beacons debería haber siempre; deauth/disassoc casi cero en reposo.
- **Feedback inmediato:** ya se distinguen los tres tipos que el proyecto necesita.

---

## Fase A3 — Detector DEAUTH + envío de alerta ← demo mínima

- **Objetivo:** disparar una alerta cuando hay muchos deauth/disassoc por segundo,
  y mandarla a la Placa B.
- **Cómo se detecta:** contar deauth (0x0C) + disassoc (0x0A) en ventana de 1 s.
  Reposo: esporádicos. Ataque: decenas/cientos por segundo. Umbral ej. >15/s.
- **Qué se hace:**
  - Al pasar el umbral, armar un `evento_t` con `tipo=ALERTA_DEAUTH`, el BSSID y la
    víctima vistos, `intensidad=frames/s`, canal, rssi, severidad por rango.
  - Mandarlo por ESP-NOW (reusa el envío del chat, ahora mandando el struct).
  - **Resolver el conflicto de canal (sección 4 del doc):** antes de mandar, fijar
    el canal de B, mandar, y volver a saltar. Es la opción robusta.
- **Verificación serie:** con el ataque de prueba contra el router propio
  (protocolo sección 7 del doc), la alerta se arma e imprime. Sin ataque, nada.
- **Feedback inmediato:** si la Placa B ya está, la alerta aparece en el OLED. Si no,
  se ve el "enviado: ESP_OK" por serie.
- **Dato de pitch:** WPA3 con PMF (802.11w) autentica los frames de gestión, así que
  el deauth clásico no funciona contra redes modernas bien configuradas.

---

## Fase A4 — Detector EVIL TWIN

- **Objetivo:** detectar un router falso que copia el SSID de una red conocida.
- **Cómo se detecta:** escuchar beacons (0x08), mantener tabla
  `SSID → BSSID(s) + canal + seguridad`. Si un SSID conocido aparece desde otro
  BSSID, es candidato a gemelo.
- **Señales que suman score (no regla dura):** otro canal, RSSI muy distinto,
  seguridad distinta (original con clave, gemelo abierto).
- **Ojo (parte difícil = buen pitch):** mesh y repetidores usan el mismo SSID con
  varias MAC a propósito. Por eso es score, no "MAC nueva = ataque".
- **Tabla "mis redes":** lista aparte, cargada antes del evento, de las redes
  propias:
  ```c
  struct red_conocida { char ssid[32]; uint8_t bssid[6]; bool es_mia; };
  ```
  La consola marca "ALERTA: MI RED" si el BSSID atacado tiene `es_mia=true`. Es
  también el gate para cualquier Protection futura (nunca actúa sin este flag).
- **Qué manda a B:** `evento_t` con `tipo=ALERTA_EVILTWIN`, `ssid`, el BSSID
  sospechoso, canal, rssi, `intensidad=score`.
- **Verificación:** levantar el hotspot de prueba con el SSID de una red propia
  (setup sección 7 del doc) y ver el candidato por serie / en la consola.

---

## Estado

```
A0  esqueleto sin display      punto de partida (copiar espnow_chat, podar)
A1  captura + hopping          siguiente
A2  parser 802.11              pendiente
A3  detector deauth + envío    pendiente  ← demo mínima presentable
A4  evil twin                  pendiente
```

Después de A3 ya hay algo presentable. A4 es el salto de calidad.
