# Guía del código fuente

Esta guía es para quien abre el proyecto por primera vez y tiene que entenderlo o modificarlo. Indica en qué orden leer los archivos, qué hace cada parte y dónde tocar para los cambios más comunes. Todo el código está comentado en español; acá se explica el *porqué* de la organización.

> Para el diagrama de bloques, el flujo de datos y las máquinas de estado, ver [arquitectura.md](arquitectura.md). Esta guía se enfoca en el código.

## Orden de lectura recomendado

| # | Archivo | Por qué en este orden |
|---|---|---|
| 1 | `firmware/aire_iot/config.h` | Todos los pines, tiempos y umbrales por defecto. Es lo primero que cambia si se adapta el hardware. |
| 2 | `firmware/aire_iot/aire_iot.ino` | `setup()` y `loop()`: el programa completo en 70 líneas. Muestra cómo se conectan los módulos. |
| 3 | `firmware/aire_iot/control.h` y `control.cpp` | El **controlador**: histéresis, alarma y pulsador. Es la parte "de la materia". |
| 4 | `firmware/aire_iot/sensor.h` y `sensor.cpp` | Cómo se obtiene la medición (BSEC2 o simulación). |
| 5 | `firmware/aire_iot/actuators.h` y `actuators.cpp` | LEDs, buzzer y lectura del pulsador. |
| 6 | `firmware/aire_iot/muestras.h/.cpp` y `storage.h/.cpp` | Cómo se guarda el historial y la memoria no volátil. |
| 7 | `firmware/aire_iot/web.h` y `web.cpp` | Wi-Fi, API y WebSocket. |
| 8 | `web/app.js` | El dashboard que corre en el navegador. |

## 1. `config.h`: el único lugar de configuración

No incluye `Arduino.h` a propósito: así se puede compilar en la PC junto con los tests. Tiene tres grupos:

- **Pines** (`PIN_SDA`, `PIN_LED_VERDE`, …) y la dirección I2C `BME680_ADDR`.
- **Tiempos fijos**: antirrebote (`DEBOUNCE_MS = 30`), pulsación corta (< 1 s) y larga (≥ 1,5 s), espera de Wi-Fi (15 s), guardado de la calibración (cada 4 h), etc.
- **`struct Config`**: los umbrales que el usuario puede cambiar desde la web (amarillo 100, rojo 200, histéresis 15, repetir alarma cada 30 s, silencio de 15 min). Estos valores son los **por defecto**; los que el usuario guarda quedan en la memoria no volátil (NVS) y se cargan al arrancar.

También está el interruptor `// #define SIMULATION`: descomentado, el firmware usa datos sintéticos en lugar del sensor.

## 2. `aire_iot.ino`: el lazo principal

```cpp
void loop() {
  uint32_t ahora = millis();
  storageBloquear();
  Config c = config;          // copia: la web puede cambiarla desde otra tarea
  storageDesbloquear();

  Lectura l;
  if (sensorActualizar(l)) {  // hay una medición nueva (cada 3 s)
    if (controlPaso(control, l.iaq, l.precision, ahora, c)) buzzerAlarma();
    storageAgregar(l, (uint8_t)control.led, uptimeS());
    ...                       // línea CSV por el monitor serie
    webPublicar(l, control);
  }
  Pulsacion p = botonLeer();  // corta → silenciar; larga → marcador
  ...
  actuadoresActualizar(control.led, !sensorOk());
  webActualizar();
}
```

Hay dos ideas importantes:

- **No hay `delay()` en ningún lado.** El loop es *cooperativo*: cada módulo hace un poquito y devuelve el control. Los tiempos se manejan comparando `millis()`. Por eso el pulsador responde siempre, aunque el buzzer esté sonando o se esté enviando el CSV.
- **El orden es medir → controlar → registrar → publicar.** Es el ciclo de un sistema de control, escrito literalmente.

## 3. `control.cpp`: el controlador

Es código **puro**: no lee pines ni usa `millis()`, recibe todo por parámetro. Eso permite probarlo en la PC (`tools/test_nativo.cpp`) y razonar sobre él sin hardware.

| Función | Qué hace |
|---|---|
| `bandaBosch(iaq)` | Devuelve la banda de Bosch 0–6 (excelente … extremadamente contaminado). Se usa para el texto y los colores de la web. |
| `siguienteLed(actual, iaq, c)` | **Histéresis**. Sube de nivel apenas el IAQ supera un umbral; baja recién cuando cae por debajo de `umbral − histéresis`. Recibe el estado actual porque la histéresis necesita memoria. |
| `controlPaso(s, iaq, precision, ahora, c)` | Un paso del lazo: actualiza el LED y la **máquina de estados de la alarma** (Normal → Alarma → Silenciada). Devuelve `true` cuando hay que hacer sonar el buzzer. No suena si la precisión de BSEC es menor a 2. |
| `controlSilenciar(s, ahora, c)` | Pasa la alarma a "Silenciada" durante `silencioMin` minutos. |
| `botonPaso(b, presionado, ahora)` | Antirrebote de 30 ms y clasificación en pulsación corta o larga. |
| `validarConfig(c)` | Rechaza umbrales incoherentes (por ejemplo, amarillo ≥ rojo). La web muestra el mensaje de error. |

Un detalle: las comparaciones de tiempo usan `yaPaso(ahora, instante)`, que resta en lugar de comparar con `>`. Así siguen funcionando cuando `millis()` vuelve a 0 (cada ~49,7 días).

## 4. `sensor.cpp`: la medición

Con el sensor real, el flujo es:

1. `sensorIniciar()` arranca **BSEC2** (la librería de Bosch) con la configuración `bme680_iaq_33v_3s_4d`: BME680 a 3,3 V, una medición cada 3 s y una ventana de calibración de 4 días. Si hay una calibración guardada en NVS, la restaura.
2. `sensorActualizar()` llama a `bsec.run()` en cada vuelta del loop. Cuando BSEC tiene datos nuevos, llama a `alRecibir()`, que copia cada salida (IAQ, eCO₂, bVOC, temperatura, …) a la `struct Lectura`.
3. Si el sensor falla (cable suelto), `ok` pasa a `false`, los tres LEDs parpadean y se reintenta cada 10 s **sin reiniciar el equipo**.
4. Cuando la precisión llega a 3, se guarda el estado de calibración en NVS (y después cada 4 h).

Con `SIMULATION`, todo esto se reemplaza por `simular(t)` (en `sensor.h`), que genera un ciclo de 20 min con un pico de contaminación y su recuperación. **La misma fórmula** está en `tools/mock_server.py`; si se cambia una, hay que cambiar la otra.

## 5. `actuators.cpp`: LEDs, buzzer y pulsador

- **LEDs:** se enciende el del nivel actual. Si el sensor no responde, parpadean los tres (indicación de falla).
- **Buzzer pasivo:** necesita una onda, no un nivel fijo. Se usa el periférico **LEDC** del ESP32 (`ledcWriteTone()`). Las melodías son listas de notas `{frecuencia, duración}` que un **secuenciador** avanza en cada vuelta del loop, sin bloquear.
- **Pulsador:** `botonLeer()` lee el pin (LOW = apretado, por el *pull-up* interno) y se lo pasa a `botonPaso()`.

## 6. `muestras.cpp` y `storage.cpp`: datos y memoria

- Cada muestra se guarda **compacta** en 24 bytes (`struct Muestra`), con enteros escalados: `temp100` = °C × 100. Así entran 24 h de historial (1440 muestras ≈ 34 KB) en la RAM.
- `Anillo<T, N>` es un **buffer circular**: cuando se llena, la muestra nueva pisa a la más vieja.
- Hay dos historiales: `vivo` (últimos 15 min, una muestra cada 3 s) e `historial` (24 h, promedio por minuto). El promedio lo arma `Promedio`.
- `formatearFila()` arma una línea CSV. Se usa igual para el monitor serie, la web y la exportación.
- `storage.cpp` usa la librería `Preferences` para guardar en NVS (flash): la configuración (con un número de versión, por si cambia el `struct`) y la calibración de BSEC.
- **Concurrencia:** el servidor web corre en otra tarea de FreeRTOS. Todo acceso compartido va entre `storageBloquear()` y `storageDesbloquear()`.

## 7. `web.cpp`: red y API

- **Wi-Fi:** intenta conectarse a la red de `secrets.h` durante 15 s. Si no puede, crea su propia red `AireIoT` (modo punto de acceso). En los dos casos publica `aire.local` por mDNS.
- **Rutas HTTP** (todas en `rutas()`): `/` sirve la página comprimida de `web_assets.h`; `/api/*` devuelve datos o recibe cambios. La tabla completa está en [arquitectura.md](arquitectura.md#red).
- **WebSocket `/ws`:** `webPublicar()` envía cada lectura como JSON a todos los navegadores conectados.
- **CSV en streaming:** el historial no se arma entero en memoria. Se genera fila por fila mientras el navegador lo descarga.
- **Pedidos al lazo:** "Silenciar" desde la web no toca el control directamente. Levanta una bandera que `loop()` atiende en la vuelta siguiente (`webPedidoSilencio()`).

## 8. La página web (`web/`)

- `index.html`, `style.css` y `app.js` son HTML/CSS/JavaScript sin frameworks. Los gráficos usan **uPlot** (incluido en `web/`, no se descarga de internet).
- `app.js` está dividido en secciones comentadas: utilidades, lectura principal, gráficos, datos, conexión en vivo, controles y arranque. Al cargar, pide el estado con `/api/*` y después se mantiene actualizado por WebSocket. Si pasan 10 s sin datos, muestra "Sin conexión" y reconecta solo.
- **El ESP32 no lee `web/` directamente.** `tools/build_web.py` junta los tres archivos, los comprime con gzip y los guarda como arreglo en `firmware/aire_iot/web_assets.h`. Después de modificar la web hay que correrlo y volver a subir el firmware.

## Herramientas (`tools/`)

| Archivo | Para qué |
|---|---|
| `i2c_scanner/i2c_scanner.ino` | Sketch aparte para verificar que el BME680 responde (0x76 o 0x77) antes de subir el firmware. |
| `build_web.py` | Regenera `web_assets.h`. Con `--check` verifica que esté al día. |
| `mock_server.py` | Simula el ESP32 en la PC (misma API y mismos datos sintéticos) para trabajar en la web sin hardware. |
| `test_nativo.cpp` | Tests de `control.cpp` y `muestras.cpp` que corren en la PC con `g++`. |

## Cambios frecuentes: dónde tocar

| Quiero… | Archivo(s) |
|---|---|
| Cambiar un pin | `config.h` (y el cableado). |
| Cambiar umbrales por defecto | `struct Config` en `config.h`. Ojo: si ya hay una config guardada en NVS, manda esa; se cambia desde la web. |
| Cambiar el sonido de la alarma | Arreglos `ALARMA` y `CONFIRMA` en `actuators.cpp`. |
| Agregar un actuador (por ejemplo, un relé) | Pin en `config.h` → lógica pura en `control.cpp` (con su test) → `pinMode`/`digitalWrite` en `actuators.cpp` → llamada en `loop()`. Ejemplo completo en [mejora_propuesta.md](mejora_propuesta.md). |
| Agregar un dato al JSON de la web | `llenarLectura()` en `web.cpp` y `mostrarLectura()` en `web/app.js`; después `build_web.py`. |
| Agregar una columna al CSV | `struct Muestra`, `empaquetar()`, `formatearFila()` y `CSV_CABECERA` en `muestras.*`, y el test de `test_nativo.cpp`. |
| Trabajar sin sensor | Descomentar `#define SIMULATION` en `config.h`. |

Antes de subir un cambio en la lógica, conviene correr los tests nativos:

```bash
mkdir -p build
g++ -std=c++17 -Wall -Wextra -I firmware/aire_iot tools/test_nativo.cpp \
    firmware/aire_iot/control.cpp firmware/aire_iot/muestras.cpp -o build/test_nativo && build/test_nativo
```
