# Consola B: menú, historial y alertas animadas

Firmware ESP-IDF para ESP32-S3 y OLED SSD1306 SPI de **128×64**. Recibe
alertas reales de la Placa A por ESP-NOW. Se inicia en el menú principal.

## Compilar y cargar

```bash
. /home/valenrc/esp-idf/export.sh
cd /home/valenrc/esp-idf/wids-calleer_saved-comcom/placa_b_consola
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Seleccionar el puerto de la **Placa B**. USB nativo Serial/JTAG; salir del
monitor con `Ctrl+]`. PSRAM deshabilitada. Los binarios conservan el nombre
`wids_demo_fase1_joystick` por compatibilidad con el proyecto existente.

## Navegación

No hace falta un botón central: **arriba/abajo seleccionan, derecha entra e
izquierda vuelve**. Una acción por movimiento; soltar antes de repetir.
Las ayudas de cada pantalla recuerdan los controles.

| Pantalla | Función y controles |
|---|---|
| Menú | Monitor, Historial, Joystick y Estado radio. Arriba/abajo recorren las opciones; derecha abre. |
| Monitor | Última alerta, tipo, severidad, canal observado, RSSI e intensidad. Arriba/abajo alternan las dos páginas; derecha abre el historial. |
| Historial | Últimas **32 alertas**, de más reciente a más antigua, con tipo, severidad y antigüedad local. Arriba/abajo seleccionan; derecha abre el detalle. |
| Detalle desde historial | Conserva el evento seleccionado; izquierda vuelve a la misma posición de la lista. |
| Joystick | Diagnóstico de las cuatro entradas: GPIO, nivel crudo, nivel estable y pulsaciones. Izquierda vuelve al menú. |
| Estado radio | Canal, tag, eventos procesados, registros guardados y descartes por cola llena. Izquierda vuelve. |

El detalle de DEAUTH muestra BSSID y MAC de la víctima. EVIL TWIN muestra el
SSID completo, distribuido en dos líneas (hasta 32 bytes), BSSID y el tiempo
original de la Placa A. El OLED representa caracteres no ASCII como `?`.
`FPS` es la intensidad de DEAUTH; `SC`, el score de EVIL TWIN.

**El historial está en RAM: se borra al reiniciar.** Al superar 32 registros,
se reemplaza el más antiguo. La antigüedad se calcula desde la recepción en B,
en segundos, minutos, horas o días, sin sincronizar los relojes de las placas.

## Aviso de una alerta nueva

Una alerta interrumpe cualquier pantalla y abre un aviso de **2,2 segundos**:
cabecera invertida, tipo en letras grandes, esquinas que se abren con
desaceleración, severidad/canal y barra temporal. Después aparece el detalle.
Mover el joystick permite saltar inmediatamente al detalle; ese primer gesto
solo cierra la animación. No hay destellos de pantalla completa.

El aviso se dibuja a un objetivo de **25 FPS**, sin pausas de animación que
bloqueen el bucle. Mientras está activo se sigue leyendo el joystick y
procesando alertas. Las alertas de una ráfaga se guardan y actualizan el aviso
con la más reciente sin reiniciar su temporizador mientras está activo.
La recepción usa una cola FIFO de 32 elementos; si se llena, descarta el nuevo
evento y aumenta `COLA PERDIDOS` en Estado radio. Este contador no incluye
pérdidas del enlace inalámbrico.

## LED de alerta y debug SW2

El LED conectado a **IO1** se controla con nivel bajo: 0 encendido, 1
apagado. Arranca apagado. Cada alerta válida DEAUTH o EVIL TWIN procesada
lo enciende durante **2,2 segundos**; una alerta nueva renueva ese plazo.
Cerrar la animación con el joystick no apaga anticipadamente el LED.

**SW2 (IO21)** permite probarlo: mantener presionado enciende el LED y
soltarlo lo apaga, salvo que siga vigente el plazo de una alerta.
SW2 es activo en bajo, con pull-up interno y antirrebote de 40 ms.
Funciona en todas las pantallas, incluso si falló la fijación del canal de
radio. La prueba solo controla el LED; no agrega alertas al historial.
El monitor serie muestra los cambios de SW2 y los estados IO1 ON/OFF.

Para verificar en placa: mantener SW2 y soltarlo; luego recibir una alerta
de cada tipo y comprobar que el LED se apaga tras 2,2 segundos sin nuevas
alertas. Si se suelta SW2 durante ese intervalo, debe permanecer encendido
hasta que termine.

## Radio y contrato

La fuente del contrato es [`../shared/evento.h`](../shared/evento.h):
`[TEAM_TAG][evento_t]`, **58 bytes**, tag `NCH1`. El canal de transporte lo fija
`ESPNOW_CANAL` (**10** actualmente); ambas placas deben compilar el mismo valor.
El campo `evento_t.canal` es el canal observado del ataque y puede ser distinto.

Se descartan longitudes, tags, tipos, canales observados o severidades
inválidos. El firmware no genera eventos de prueba ni responde a la Placa A.
El tag separa equipos, no autentica al emisor. `LISTA` indica que el receptor
se inicializó, no que exista un enlace confirmado con A.

El monitor serie imprime la MAC receptora al iniciar y `ESP-NOW RX` por cada
alerta procesada. Si falla la fijación del canal, el menú indica `ERROR` y
Estado radio muestra el error; el joystick continúa disponible.

## Cableado y orientación

OLED: SCK **36**, MOSI **37**, CS **38**, DC **39**, RST **40**.
Joystick: contactos R **10**, D **11**, L **12**, U **9**, activos en bajo con
pull-up (`0` pulsado, `1` suelto). Muestreo cada 10 ms; antirrebote de 40 ms.

Se compensa el montaje girado **135° a la izquierda**, visto desde el OLED:

| Contactos activos | Dirección del menú |
|---|---|
| IO10 + IO11 | Arriba |
| IO11 + IO12 | Derecha |
| IO12 + IO9 | Abajo |
| IO9 + IO10 | Izquierda |
| IO10 solo | Izquierda |
| IO11 solo | Arriba |
| IO12 solo | Derecha |
| IO9 solo | Abajo |

Se esperan otros 40 ms para agrupar contactos vecinos. Mantener el joystick
o liberar parcialmente una combinación no dispara acciones adicionales.
El diagnóstico conserva los nombres físicos y muestra la dirección corregida
en la cabecera; el monitor serie conserva RAW, PRESS y RELEASE.

## Verificación

Desde la raíz del repositorio, pruebas en host sin hardware:

```bash
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Iplaca_b_consola/main -Ishared tests/test_placa_b_ui.c \
  placa_b_consola/main/console_ui.c -o /tmp/test_placa_b_ui
ASAN_OPTIONS=detect_leaks=0 /tmp/test_placa_b_ui
cc -std=c11 -Wall -Wextra -Werror -Iplaca_b_consola/main \
  tests/test_joystick_orientation.c -o /tmp/test_placa_b_joystick
/tmp/test_placa_b_joystick
```

Cubren navegación vacía, límites del historial, orden tras 70 eventos, vuelta
al registro seleccionado, interrupción de todas las pantallas, ráfagas,
fin/cancelación de animación, reloj más allá de 32 bits, SSID sin terminador
y límites del framebuffer. Se desactiva únicamente LeakSanitizer para poder
usar las pruebas en entornos con ptrace; se mantienen ASan y UBSan.

Para generar capturas y animación del **mismo renderizador del firmware**
(con eventos sintéticos exclusivamente en el programa de pruebas del host):

```bash
python3 tests/preview_placa_b.py /tmp/placa-b-preview
```

Prueba en placa: recorrer el menú, recibir DEAUTH y EVIL TWIN reales, abrir
un registro antiguo, comprobar que otra alerta interrumpe esa vista, saltar
el aviso con el joystick y verificar el detalle en el OLED. Confirmar también
que cada gesto navega una sola vez y que el historial se vacía al reiniciar.
