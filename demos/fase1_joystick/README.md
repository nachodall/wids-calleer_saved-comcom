# Primera demo de la consola B: B1 + joystick

Proyecto ESP-IDF independiente del firmware principal, para ESP32-S3 y OLED
SSD1306 SPI. Implementa únicamente la pantalla de alerta B1 y diagnóstico
del joystick. No incluye historial, ACK, tabla de dispositivos ni sonido.
Esta demo corresponde a B1 de `PLACA_B_CONSOLA.md`; la fase 1 general de
`IMPLEMENTACION.md` (captura WiFi de la Placa A) es una fase diferente.

## Ejecutar

```bash
cd /home/valenrc/esp-idf
. ./export.sh
cd wids-calleer_saved-comcom/demos/fase1_joystick
idf.py flash monitor
```

Para elegir puerto: `idf.py -p /dev/ttyACM0 flash monitor`.
Usar USB nativo Serial/JTAG. Salir con `Ctrl+]`.

## Pantalla de alerta B1

Al arrancar muestra **ESPERA** y `ESP-NOW CH:13`; permanece esperando hasta
recibir una alerta real. No genera eventos sintéticos, ni hay una opción de
configuración que pueda activarlos. El diagnóstico del joystick sigue visible.

Recibe eventos ESP-NOW en `ESPNOW_CANAL` con el contrato
`[TEAM_TAG][evento_t]` de `../../../evento.h`: 58 bytes, canal **13** y tag NCH1.
La Placa A debe transmitir por broadcast en ese mismo canal y compilar el
mismo contrato. El campo `evento_t.canal` identifica el canal del ataque y
puede ser distinto del canal 13 usado para transportar la alerta.

Al recibir un paquete válido, el encabezado cambia a `RX`. Muestra tipo,
severidad, canal observado, RSSI, intensidad y BSSID (DEAUTH) o SSID (EVIL TWIN,
hasta 21 caracteres en pantalla; completo por serie). Solo conserva la alerta
más reciente y no transmite respuestas. No detecta ataques por sí misma.

Los paquetes de longitud, tag, tipo, canal observado o severidad inválidos
se descartan. El tráfico WiFi ajeno no se convierte en eventos. El tag separa
equipos; no autentica al emisor.

`../../../radio_config.h` configura Argentina, canales 1–13, y verifica que
la radio haya quedado en el canal 13. El monitor imprime la MAC receptora y
`ESPERANDO ALERTA REAL` al arrancar; cada recepción válida imprime `ESP-NOW RX`.

La radio usa HT20 y verifica el canal principal con reintentos acotados. El log
`Canal solicitado=13 principal=13 secundario=... HT20` confirma la configuración.
Si no consigue el canal correcto, muestra `ERROR RADIO` sin reiniciar y permite
seguir probando el joystick; revisar los valores reales en el monitor serie.

## Diagnóstico del joystick

Las cuatro filas inferiores permanecen visibles junto a la alerta:

| Dirección | Señal | GPIO |
|---|---|---|
| R / derecha | KEY_RIGHT | IO10 |
| D / abajo | KEY_DOWN | IO11 |
| L / izquierda | KEY_LEFT | IO12 |
| U / arriba | KEY_UP | IO9 |

Cada fila muestra `R` (nivel crudo), `S` (nivel estable con antirrebote de
40 ms) y `N` (pulsaciones confirmadas). Son entradas con pull-up: **0 pulsado,
1 suelto**. Se muestrean cada 10 ms; movimientos simultáneos se muestran
independientemente. El joystick solo diagnostica entradas, no cambia de modo.

Se compensa el montaje girado **135° a la izquierda**, visto desde el OLED,
mediante el mismo `../../../joystick_orientation.h` que usa la consola principal.
El encabezado indica la dirección corregida; `UP-L`, `UP-R`, `DOWN-L` y `DOWN-R`
son diagonales. Las filas RAW/ESTABLE conservan los GPIO y etiquetas originales.

| Contactos activos (nivel 0) | Dirección corregida |
|---|---|
| IO10 + IO11 | Arriba |
| IO11 + IO12 | Derecha |
| IO12 + IO9 | Abajo |
| IO9 + IO10 | Izquierda |

Un solo contacto produce una diagonal: IO10 arriba-izquierda, IO11
arriba-derecha, IO12 abajo-derecha e IO9 abajo-izquierda. El monitor imprime
`CORREGIDO 135 IZQ` y la acción `MENU` que usaría la consola. Para resolver
empates diagonales en un menú de cuatro direcciones: IO10 → izquierda,
IO11 → arriba, IO12 → derecha, IO9 → abajo. Se esperan 40 ms para agrupar
contactos vecinos; mantener pulsado o liberar parcialmente no repite la acción.

El monitor imprime cada cambio crudo (`RAW`), pulsación (`PRESS`) y liberación
(`RELEASE`), con dirección, GPIO y contadores. Cada segundo imprime los niveles
de las cuatro entradas para detectar una dirección trabada o cableado incorrecto.
Los flancos crudos permiten observar rebotes de duración mayor al muestreo;
no sustituye una medición con osciloscopio.

Prueba manual:

1. En reposo, todas las entradas deben mostrar `R1 S1`.
2. Pulsar cada dirección: solo su fila pasa a `R0 S0` y `N` sube una vez.
3. Mantener pulsado: `N` no debe seguir subiendo.
4. Soltar: vuelve a `R1 S1` y aparece `RELEASE` en serie.
5. Comparar el GPIO indicado con el movimiento físico para detectar cruces.

Pines OLED: SCK 36, MOSI 37, CS 38, DC 39, RST 40. PSRAM deshabilitada.
Compilar con `idf.py build`; validar pantalla, radio y GPIO en la placa física.
