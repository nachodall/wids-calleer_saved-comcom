# Placa B — La Consola (`wipds_consola`)

Rol: recibir alertas de la Placa A por ESP-NOW y mostrarlas en el OLED con
estética terminal, navegable con joystick y botones. Canal ESP-NOW fijo.

Cada fase deja algo verificable. Clave: la Placa B **puede arrancar en paralelo
con la A** usando un emisor de prueba, sin esperar a que el oído esté listo.

Base de reuso: `espnow_chat` ya tiene OLED (framebuffer, fuente 5x7, `draw_text`,
`redraw` con semáforo), cola RX y filtro por tag. Casi todo B sale de ahí.

---

## Contrato compartido con la Placa A

Mismo `evento.h` que la Placa A — el archivo real está en [`wipds/evento.h`](evento.h)
y se **copia tal cual** en `main/` de este proyecto (no se retipea). La B filtra por
`TEAM_TAG` y castea el resto del mensaje a `evento_t`. **No arrancar sin cerrar ese
struct.**

**Flujo de una sola vía:** B solo recibe; nunca le contesta a A. B escucha ESP-NOW
en `ESPNOW_CANAL` (en `evento.h`, fijo) y se queda ahí — no salta de canal.

---

## Fase B0 — Receptor que imprime por serie ✅ punto de partida

- **Objetivo:** recibir un `evento_t` por ESP-NOW y volcarlo por serie. Todavía sin
  dibujar nada en el OLED.
- **Qué se hace:** copia de `espnow_chat`, pero `on_recv` castea a `evento_t` y lo
  encola; `app_main` lo imprime formateado (tipo, ssid/bssid, canal, intensidad).
- **Emisor de prueba (para no depender de la Placa A):** una segunda placa, o la
  misma Placa A en Fase A0, mandando un `evento_t` falso cada 2 s (deauth y evil
  twin alternados). Así B se prueba sola.
- **Verificación serie:** llega el evento falso y se imprime bien parseado.
- **Por qué primero:** separa "recibir bien el struct" de "dibujarlo". Si el casteo
  está mal, se ve acá antes de pelear con la pantalla.

---

## Fase B1 — Pantalla de alerta (modo ATAQUE)

- **Objetivo:** cuando llega una alerta, el OLED la muestra grande y clara.
- **Qué se dibuja:**
  - Línea 1: tipo en mayúsculas (`DEAUTH` / `EVIL TWIN`).
  - Líneas siguientes: SSID o BSSID afectado, canal, intensidad.
  - Un marco o inversión de video para que "grite" (es monocromo: jugar con relleno).
- **Reusa:** `draw_text`, `redraw`, el semáforo `flush_done` tal cual del chat.
- **Verificación:** el emisor de prueba manda un deauth falso → aparece la pantalla
  de alerta en el OLED. Foto para la demo.
- **Feedback inmediato:** ya es "demostrable" aunque la lógica real de la A no esté.

---

## Fase B2 — Modo LOG (historial scrollable)

- **Objetivo:** guardar los últimos N eventos y poder recorrerlos.
- **Qué se hace:**
  - Buffer circular de eventos en SRAM (ej. últimos 16).
  - Modo LOG: lista tipo terminal, una línea por evento con hora relativa.
  - Joystick arriba/abajo para scrollear la lista.
- **Verificación:** mandar varios eventos de prueba → quedan en la lista y se
  scrollean. El más viejo se descarta al pasar N.

---

## Fase B3 — Navegación de modos + acciones

- **Objetivo:** cambiar entre ATAQUE y LOG, y accionar sobre las alertas.
- **Qué se hace:**
  - Joystick izquierda/derecha: cambiar de modo (pines UP 9, DOWN 11, LEFT 12,
    RIGHT 10).
  - Botones SW1–SW4 (14, 21, 47, 48): ack de alerta / limpiar historial.
  - Una alerta nueva fuerza el modo ATAQUE aunque estés en LOG.
- **Verificación:** con eventos de prueba entrando, se navega entre modos, se
  limpia, y una alerta nueva interrumpe y muestra el ATAQUE.

---

## Fase B4 — "Mis dispositivos" (nombres en vez de MAC)

- **Objetivo:** mostrar `VÍCTIMA: MI-PC` en vez de la MAC cruda.
- **Qué se hace:** tabla `MAC → nombre` cargada antes del evento. Al mostrar una
  alerta, si la MAC está en la tabla, usar el nombre.
- **Verificación:** mandar un evento con la MAC de un dispositivo propio → la
  consola muestra el nombre. Es un detalle que pega fuerte en la demo.

---

## Estado

```
B0  receptor por serie         punto de partida (copiar espnow_chat)
B1  pantalla de alerta         siguiente  ← ya demostrable
B2  modo log scrollable        pendiente
B3  navegación + acciones      pendiente
B4  mis dispositivos           pendiente (detalle de demo)
```

B puede ir en paralelo con A desde el día uno, gracias al emisor de prueba de B0.
El único punto de sincronización es el `evento.h` compartido.
