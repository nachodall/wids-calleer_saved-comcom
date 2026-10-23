# WIPDS/WIDS: Plan de implementación por fases

**WIPDS** = Wireless Intrusion Protection & Detection System. El nombre cubre
las dos letras posibles — D de Detection, P de Protection — porque el diseño
deja la puerta abierta a crecer de una a la otra. El **scope cerrado para este
hackatón es solo D: WIDS.** Detecta y alerta. No transmite nada contra un
ataque. Ver sección 1 para el detalle completo.

Dos placas ESP32-S3. Todo pasivo y legal: solo escucha el aire, nunca emite ni
interfiere.

Stack: **C con ESP-IDF v6.1**. El modo promiscuo es API nativa de Espressif.

---

## 1. Casos de uso: Detection y Protection

El proyecto distingue dos tipos de caso. Están separados a propósito, porque
tienen implicancias legales distintas.

### Detection (scope actual — todo lo que se construye en el hackatón)

| Caso | Qué hace | RF que transmite |
|---|---|---|
| Detector deauth | Cuenta frames deauth/disassoc, alerta si supera umbral | Ninguna (solo escucha) |
| Detector evil twin | Compara SSID/BSSID contra una tabla conocida, calcula score | Ninguna (solo escucha) |

Las dos son 100% pasivas. El ESP32-S3 en modo promiscuo recibe lo que ya está
en el aire, no transmite nada. Por eso son legales sin pedir permiso a nadie:
es análogo a escuchar con un micrófono un sonido que ya existe.

### Protection (fuera de scope — documentado para el futuro, no para este hackatón)

Un WIPS de verdad, además de detectar, actúa. Hay dos formas de actuar, y son
muy distintas:

| Método | Cómo actúa | ¿Entra en scope? |
|---|---|---|
| **Contención por aire (RF)** | Manda deauth contra el atacante | **No, nunca.** Es el mismo ataque que se quiere detectar. No se puede apuntar con precisión sin riesgo de afectar a terceros (art. 197 CP, interrupción de comunicaciones) |
| **Respuesta por red (no-RF)** | Avisa por Telegram/dashboard, o le pide al router que bloquee un BSSID | Posible como extensión futura. No entra en el hackatón |

**Regla dura: el proyecto nunca transmite un frame 802.11 de gestión (deauth,
disassoc) como respuesta a algo.** Eso convertiría a WIPDS en el ataque
que dice combatir, y rompe el argumento central del pitch ("todo pasivo y
legal").

Si en algún momento se agrega una respuesta no-RF, tiene que estar **gateada**
por la tabla de redes de confianza (ver Fase 4): solo se dispara una acción si
el BSSID atacado está marcado `es_mia = true`. Así el sistema nunca "actúa"
sobre una red que no es del dueño del sensor.

```
   Detection (scope actual)              Protection (futuro, no-RF)
   ─────────────────────────             ───────────────────────────
   escuchar ──► detectar ──► alertar     alertar ──► ¿es_mia? ──► avisar
   (esto es todo lo que hace             por Telegram / pedirle al
    el proyecto hoy)                     router que bloquee
                                          NUNCA: transmitir deauth
```

---

## 2. Hardware real

La placa es custom (EasyEDA/JLCPCB). Ya está confirmado lo que el spec daba por
pendiente. Estos datos reemplazan los supuestos del spec original.

| Componente | Qué es | Pines (GPIO del S3) |
|---|---|---|
| Pantalla | **OLED SSD1306 128x64 monocromo** por SPI (no es TFT a color) | SCL 36, SDA 37, CS 38, DC 39, RST 40 |
| Navegación | **Joystick de 5 vías** (no un encoder rotativo) | UP 9, DOWN 11, LEFT 12, RIGHT 10, centro no conectado |
| Botones | SW1–SW4 | 14, 21, 47, 48 |
| Sonido | Parlante por amplificador **I2S MAX98357A** (no un buzzer) | DIN 4, BCLK 5, LRCLK 6 |
| Micrófono | I2S | CLK 7, DOUT 15 |
| Header I2C | Para conectar placas por cable | SDA 1, SCL 2 |

Implicancias:

- **La estética monocromo juega a favor.** Terminal, ASCII, scanlines: todo se ve
  bien en un OLED blanco. No hay color. La pantalla es chica: 21 caracteres por
  fila, 7 filas con la fuente 5x7.
- **El sonido es I2S, no un pin que se prende.** Un tono de alerta se puede
  generar, pero cuesta más que un buzzer simple. Dejar para la fase de pulido.
- **La navegación de modos usa el joystick** (izquierda/derecha), no un encoder.

### Memoria

La placa tiene 8 MB de PSRAM, pero **no se puede usar**: la PSRAM octal ocupa los
GPIO 35–37 y choca con el display. Quedan ~512 KB de SRAM interna. Para la tabla
de SSID→BSSID y los contadores, sobra. No activar PSRAM en `menuconfig`.

---

## 3. Arquitectura

Dos roles separados, comunicados por ESP-NOW (directo placa-a-placa, sin router).

```
   PLACA A = EL OÍDO                      PLACA B = LA CONSOLA
   ----------------                       --------------------
   WiFi en modo promiscuo                 recibe eventos por ESP-NOW
   salta de canal (1..13)                 los encola y los muestra
   parsea cada frame 802.11               OLED con estética terminal
   corre los detectores                   joystick para cambiar de modo
   arma eventos y los manda  ──ESP-NOW──► tono de alerta por severidad
   SIN pantalla (no pierde frames)        botones para ack/limpiar
```

**Por qué separadas:** cazar frames es sensible al tiempo. Dibujar en pantalla
consume tiempo. Si una sola placa hiciera las dos cosas, perdería paquetes.
Separar "el que escucha" de "el que muestra" evita esa pérdida. Además permite
escalar a N placas A reportando a una sola consola B.

---

## 4. El desafío técnico central: promiscuo + ESP-NOW

Las dos funciones comparten la única radio de 2.4 GHz. No se puede escuchar en un
canal y transmitir en otro a la vez.

```
Placa A salta:   1 → 2 → 3 → ... → 13 → 1 ...   (cada ~250 ms)
Placa B escucha: canal fijo = 1

   A detecta un deauth mientras está en el canal 7
   quiere avisar a B, pero B escucha en el canal 1
   si A manda desde el canal 7, el mensaje no llega
```

Opciones, de más simple a más robusta:

1. **A fija el canal de B un instante, manda, y vuelve a saltar.** Robusto. Es la
   forma recomendada. Costo: unos ms sin escuchar por cada evento.
2. **A acumula eventos y los manda solo cuando el salto la deja en el canal de B.**
   Más simple, pero el aviso se demora hasta que el ciclo vuelve a ese canal.

Se resuelve en la Fase 1, cuando ya haya captura real. La decisión afecta cómo se
escribe el bucle de la Placa A.

---

## 5. Fases

Regla de scope: **después de la Fase 3 ya hay proyecto presentable.** Mejor dos
detectores sólidos y bien demostrados que seis a medio andar.

Cada fase deja algo que funciona y se puede verificar por el monitor serie.

### Fase 0 — El cable (ESP-NOW A↔B) ✅ YA HECHO

- **Estado:** listo. Es el proyecto `espnow_chat`.
- **Qué prueba:** que A manda y B recibe por ESP-NOW. Base de todo lo demás.
- **Reusar de acá:** el patrón de arranque de ESP-NOW, el filtro por tag, la cola
  de recepción, y todo el manejo del OLED (framebuffer, fuente 5x7, `draw_text`).

### Fase 1 — Captura cruda (Placa A)

- **Objetivo:** la Placa A escucha todos los frames del aire y cuenta cuántos pasan
  por segundo. Sin parsear todavía.
- **Qué se hace:**
  - Iniciar WiFi, `esp_wifi_set_promiscuous(true)`.
  - Registrar el callback con `esp_wifi_set_promiscuous_rx_cb`.
  - En el callback, **solo** incrementar un contador. Nada lento ahí adentro.
  - Una tarea aparte salta de canal con `esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)`,
    recorriendo 1..13 cada ~250 ms.
  - Cada segundo, imprimir por serie los frames contados.
- **API clave (verificada en v6.1):** `esp_wifi_set_promiscuous`,
  `esp_wifi_set_promiscuous_rx_cb`, `esp_wifi_set_channel`.
- **Criterio de que funciona:** el contador sube y cambia según el canal. Hay
  tráfico WiFi real alrededor, así que nunca debería dar cero en todos los canales.
- **Acá se decide** cómo convive el channel hopping con el envío ESP-NOW (sección 4).
- **Proyecto nuevo:** `wipds_oido`.

### Fase 2 — Parser de frames 802.11 (Placa A)

- **Objetivo:** de cada frame, sacar la metadata. El contenido no importa.
- **Qué se saca del frame:**
  - Tipo y subtipo (de los primeros 2 bytes, el "frame control").
  - Direcciones MAC (origen, destino, BSSID).
  - Canal actual (el que está escuchando la placa).
  - RSSI (intensidad de señal). Viene en `rx_ctrl.rssi` del paquete.
- **Estructura del paquete:** el callback recibe un `wifi_promiscuous_pkt_t`. Trae
  `rx_ctrl` (con rssi y canal) y `payload` (el frame 802.11 crudo).
- **Subtipos que importan:**
  - `0x08` beacon (lo usa el detector evil twin)
  - `0x0A` disassoc, `0x0C` deauth (los usa el detector deauth)
- **Criterio de que funciona:** por serie se ven beacons con el nombre de redes
  WiFi reales de alrededor, con su MAC y su RSSI.

### Fase 3 — Detector DEAUTH (Placa A) ← demo mínima presentable

- **Qué es el ataque:** un frame deauth desconecta a un cliente de su red. En WPA2
  esos frames no están autenticados, así que cualquiera los falsifica. Mandados en
  loop = la víctima se queda sin WiFi.
- **Cómo se detecta:** contar frames deauth (0x0C) y disassoc (0x0A) por ventana de
  1 segundo. Uso normal: esporádicos. Ataque: decenas o cientos por segundo. Si
  supera un umbral (ej. >15/s), se arma una alerta.
- **Enfoque:** conteo. No necesita memoria de estado.
- **Evento que manda a B:** `{tipo: DEAUTH, víctima (MAC destino), BSSID, canal,
  intensidad (frames/s), severidad, timestamp}`.
- **Criterio de que funciona:** con el ataque de prueba corriendo (ver sección 7),
  la alerta se dispara. Sin ataque, no hay falsos positivos.
- **Dato para el pitch:** WPA3 con PMF (802.11w) autentica los frames de gestión,
  así que el deauth clásico no funciona contra redes modernas bien configuradas.

### Fase 4 — Detector EVIL TWIN (Placa A)

- **Qué es el ataque:** un router falso copia el nombre (SSID) de una red conocida,
  con señal más fuerte. El celu de la víctima se engancha al falso, y todo su
  tráfico pasa por el atacante.
- **Cómo se detecta:** escuchar beacons (0x08) y mantener una tabla
  `SSID → BSSID(s) conocidos + canal + seguridad`. Si un SSID conocido aparece
  desde otro BSSID, es candidato a gemelo.
- **Señales que suman sospecha (score, no regla dura):** aparece en otro canal,
  RSSI muy distinto, seguridad distinta (original con clave, gemelo abierto).
- **Enfoque:** comparar y recordar. Necesita memoria de estado (la tabla, en SRAM).
- **El desafío real (buen punto de pitch):** las redes mesh y los repetidores usan
  el mismo SSID con varias MAC a propósito. Por eso no es "MAC nueva = ataque",
  sino un score de varias señales. Distinguir un repetidor legítimo de un gemelo
  malicioso es la parte difícil.
- **Tabla de redes de confianza ("mis redes"):** además de la tabla de SSID→BSSID
  vistos, mantener una lista separada, cargada antes del evento, de las redes
  propias:
  ```c
  struct red_conocida {
      char ssid[32];
      uint8_t bssid[6];
      bool es_mia;   // true = red propia, false = solo vista antes
  };
  ```
  Sirve para dos cosas: (1) en la demo, la consola etiqueta el evento como
  "ALERTA: MI RED" cuando el BSSID atacado está marcado `es_mia`, lo que hace
  el pitch más claro; (2) es el gate obligatorio para cualquier feature de
  Protection futura (sección 1): sin este flag en `true`, nunca se dispara una
  acción.
- **Criterio de que funciona:** levantar un AP de prueba con el nombre de una red
  propia y ver que la consola lo marca como candidato.

### Fase 5 — La consola (Placa B)

- **Objetivo:** mostrar los eventos con estética terminal, navegable.
- **Qué se hace:**
  - Recibir eventos de A por ESP-NOW y encolarlos (ya sabemos cómo, de la Fase 0).
  - Dibujar en el OLED: modo ATAQUE (alerta + texto grande), modo LOG (scroll tipo
    terminal con hora).
  - Joystick izquierda/derecha para cambiar de modo.
  - Botones SW1–SW4 para ack / limpiar alertas.
- **Reusar:** todo el manejo del OLED de `espnow_chat` y `com_com`.
- **Feature que pega en la demo:** lista de "mis dispositivos" (MAC → nombre), para
  que la consola muestre "VÍCTIMA: MI-PC" en vez de la MAC cruda.
- **Criterio de que funciona:** un evento enviado por A aparece en la consola, con
  el modo correcto, y se puede navegar y limpiar.

### Fase 6 — Pulido

- Tono de alerta por I2S según severidad (es lo más caro; dejarlo último).
- ASCII art de booteo, scanlines, animaciones.
- Modos extra si sobra tiempo: beacon flood (3er detector, conteo barato),
  multitud (dispositivos cercanos, % de MAC random), radar (RSSI del más cercano),
  ruido (micrófono como VU meter).

---

## 6. Decisiones técnicas cerradas

- **Scope:** solo Detection (WIDS). Protection queda documentada pero fuera del
  hackatón — ver sección 1. Si se agrega, solo respuesta no-RF, gateada por la
  tabla "mis redes" de la Fase 4.
- **Lenguaje:** C con ESP-IDF v6.1. Ya está todo andando. El modo promiscuo es API
  nativa de Espressif, madura. No pasar a Arduino: sería tirar lo hecho.
- **Comunicación:** ESP-NOW por broadcast. No depende de ninguna red WiFi, que es
  clave cuando lo que detectás son ataques a las redes.
- **Display:** OLED SSD1306 monocromo. Estética terminal, no gráficos a color.
- **Memoria:** SRAM interna. PSRAM deshabilitada (choca con el display).
- **Sonido:** I2S, solo en la fase de pulido.

---

## 7. Demostración (protocolo por tipo de ataque)

Los dos ataques de prueba tienen riesgo legal distinto. Se tratan distinto.

### Deauth: grabado en video, nunca en vivo frente a la sala

- **Por qué:** un deauth se dirige por BSSID, pero no todos los dispositivos
  validan esa dirección correctamente, y las herramientas de ataque suelen tener
  modos "contra todo lo que vea" por defecto. En una sala con 80 personas y
  decenas de redes, el riesgo de afectar a un tercero sin su permiso es real →
  interrupción de comunicaciones (delito, art. 197 CP).
- **Cómo se graba:** en casa, contra el router propio y un dispositivo propio
  como única víctima. Herramientas públicas ya hechas, no se programan: Bettercap
  o aircrack-ng en una notebook Linux, o ESP32 Marauder en otra placa.
- **Protocolo de testing** (para validar el detector, no solo para el video):
  1. Anotar el BSSID del router propio.
  2. Prender la Placa A cerca, en modo promiscuo.
  3. Correr el ataque apuntado a ese BSSID puntual (`aireplay-ng --deauth 50 -a
     <BSSID>`, nunca en modo broadcast general).
  4. Verificar por el monitor serie de A que el contador de deauth/disassoc sube
     y dispara la alerta al pasar el umbral.
  5. Grabar esto en video para la demo.
- **Si de verdad se quiere hacer en vivo** (más riesgo logístico que legal si se
  cumplen estas 4 condiciones): AP propio con BSSID único en la sala, única
  víctima un dispositivo propio, herramienta scopeada a ese BSSID exacto (no
  "deauth todo"), y organizadores avisados de antemano. Sin las 4 condiciones,
  video.

### Evil twin: se puede hacer en vivo

- **Por qué es distinto al deauth:** un beacon de evil twin solo *anuncia* un
  nombre de red. No fuerza a ningún dispositivo a desconectarse de nada. No hay
  comunicación ajena interrumpida, así que no aplica el mismo riesgo legal. Es el
  mismo acto que prender cualquier router nuevo en la sala.
- **Único riesgo residual:** que el SSID elegido coincida, por casualidad, con
  una red que otro dispositivo en la sala tenga guardada sin clave y se conecte
  solo. Se evita usando el SSID real y propio (no uno genérico tipo "WiFi" o
  "Internet").
- **Setup recomendado — hotspot de un celu Android:**

  | Config | Recomendación | Por qué |
  |---|---|---|
  | Banda | **2.4 GHz, obligatorio** | El ESP32-S3 no tiene radio de 5 GHz: si el hotspot sale en 5 GHz, la Placa A no lo ve |
  | SSID | Idéntico al router real, letra por letra | Es el disparador central del detector |
  | Seguridad | Abierta si Android lo permite (o un tipo distinto al real) | Suma a la señal "seguridad distinta" del score |
  | BSSID | No requiere acción | El hotspot genera una MAC propia automáticamente |
  | Posición | Celu cerca de la Placa A, router real lejos | Suma a la señal "RSSI más fuerte = atacante cerca" |
  | Canal | Distinto al del router real, si el teléfono lo permite | Suma a la señal "aparece en otro canal" (opcional) |

  iPhone no deja sacarle la clave al hotspot: usar Android si se puede.

- **Cortesía, no requisito legal:** avisar a los organizadores antes de prender
  el hotspot falso, para que nadie del jurado se alarme al ver "ALERTA: EVIL
  TWIN" en la consola.

### Demo ideal

Deauth en video (split-screen con el OLED reaccionando en el momento en que se
grabó) + evil twin en vivo con el hotspot, mostrando la detección en tiempo real
frente al jurado.

---

## 8. Reparto sugerido

Las dos placas corren código distinto (oído y consola), así que el trabajo se puede
partir una vez que la Fase 1 defina cómo conviven canal y ESP-NOW.

- **Oído (Placa A):** Fases 1 a 4. Captura, parser, detectores.
- **Consola (Placa B):** Fase 5. Reusa el manejo del OLED que ya existe, así que
  puede arrancar en paralelo con un emisor de eventos de prueba.

El contrato entre las dos es la estructura del evento (sección Fase 3). Si las dos
personas acuerdan esos campos primero, pueden avanzar en paralelo sin pisarse.

---

## 9. Estado actual

```
Fase 0  ESP-NOW A↔B          ✅ HECHO (proyecto espnow_chat)
Fase 1  captura + hopping       siguiente
Fase 2  parser 802.11           pendiente
Fase 3  detector deauth         pendiente  ← demo mínima
Fase 4  evil twin               pendiente (incluye tabla "mis redes")
Fase 5  consola B               pendiente (puede ir en paralelo)
Fase 6  pulido                  pendiente
```

Próximo paso: proyecto `wipds_oido`, Placa A en modo promiscuo contando
frames por segundo, verificado por serie. Recién cuando la captura funcione, se
suman el parser y los detectores.
