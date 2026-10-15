# Complicaciones encontradas

Registro de problemas durante el desarrollo: qué pasó, por qué y cómo se resolvió. Si en la Etapa 2 aparecen nuevos, agregalos al final con el mismo formato.

## Encontradas durante el desarrollo

| Problema | Síntoma | Causa | Solución | Tiempo perdido |
|---|---|---|---|---|
| El ESP32 no entraba en la protoboard | Montado en la protoboard de 400 puntos, tapaba casi todos los agujeros y no quedaba lugar para los cables | El DevKit V1 mide unos 2,5 cm de ancho | ESP32 fuera de la protoboard, conectado con cables hembra–macho ([conexion.md](conexion.md#armado-en-la-protoboard-de-400-puntos)) | 10 min |
| El primer plano de armado no coincidía con la protoboard real | Agujeros de riel dibujados donde no existen | Se asumió una protoboard de 30 columnas con rieles de 30 agujeros; la real tiene rieles de 25, en grupos de 5, y la polaridad invertida en el lado derecho | Se rehízo el plano a partir de una foto de la protoboard (`protoboard.svg`) | 15 min |
| Un pulsador de 4 patas puede leerse "apretado" siempre | — (detectado al planificar el armado) | Dos pares de patas están unidos internamente; según cómo se gire, ambos cables pueden quedar en el mismo par | Conexión en diagonal (GPIO en J28, GND en C30): funciona en cualquier orientación | 5 min |
| No se podía instalar `arduino-cli` con Homebrew | La instalación seguía después de más de 30 min | En macOS 12, Homebrew compila `arduino-cli` desde el código fuente | Se usó el binario oficial (`install.sh` de Arduino) | ~30 min |
| El ejemplo de BSEC2 no compilaba | `'LED_BUILTIN' was not declared` | La placa genérica "ESP32 Dev Module" no define `LED_BUILTIN`; el problema era el ejemplo, no BSEC | Se compiló el ejemplo con `-DLED_BUILTIN=2` solo para verificar que la librería precompilada linkea con el core 3.3.12 | 5 min |
| Presión mal escalada (riesgo) | El header de BSEC2 documenta la presión en Pa | `Bsec2::run()` ya convierte Pa → hPa antes de procesar (`data.pressure *= 0.01f`), así que la salida está en hPa | Se usa el valor tal cual, sin dividir por 100. Se descubrió leyendo `bsec2.cpp` | 10 min |
| Falsos "sensor desconectado" (riesgo) | `run()` devuelve `false` | También devuelve `false` ante **advertencias** de BSEC (por ejemplo, cuando el loop se demora por el Wi-Fi) | Solo se considera caído si el código de BSEC o del BME68x es **negativo** (error real) | 10 min |
| `GET /api/config` podía responder el handler equivocado | — (detectado al revisar la librería) | `AsyncCallbackJsonWebHandler` acepta GET, POST, PUT y PATCH por defecto | `setMethod(HTTP_POST)` en los handlers JSON | 5 min |
| El historial de 24 h en JSON no entraba en memoria | — (detectado en el diseño) | ~14 000 valores en un `JsonDocument` necesitan más de 200 KB de heap | `/api/history` devuelve **CSV generado en streaming**, fila por fila, con el mismo código que el export | 15 min |
| El aviso "Sin conexión" no aparecía al apagar el mock | La página seguía "conectada" | Al recibir SIGTERM, aiohttp mantiene abiertos los WebSockets hasta 60 s y sigue enviando datos (era un artefacto de la prueba). Además, un equipo que pierde el Wi-Fi no cierra el socket | El vigía del WebSocket declara la desconexión si pasan 10 s sin datos, sin esperar el evento `close`. Se prueba con `kill -9` | 20 min |
| El trazo en vivo desaparecía al perder la conexión | Gráfico vacío | El hueco de "pluma levantada" usaba la hora del navegador; si no coincide con la del equipo, el eje x queda desordenado y uPlot no dibuja | El hueco se pone 1 s después del último punto | 10 min |
| La subida al ESP32 fallaba | `Unable to verify flash chip connection (No more data to read…)` justo al pasar a 921600 baudios | El adaptador USB-serie de la placa no aguanta 921600 en macOS | *Upload Speed* = 115200 (`esp32:esp32:esp32:UploadSpeed=115200` en arduino-cli) | 5 min |
| El escáner I2C no encontraba el BME680 | "Nada. Revisá cableado…"; el firmware reportaba `BME68x=-2` (falla de comunicación) | Falso contacto en la protoboard | Reacomodar los cables; el firmware se recuperó solo, sin reiniciar | 10 min |
| Sin sensor, la web no decía "Sensor no detectado" (revisión final) | Quedaba en "Esperando la primera lectura…" o alternaba "Sin conexión" / reconexión cada 10 s | Sin lecturas, el equipo no mandaba nada por WebSocket y la página creía que se había cortado la conexión | `/api/current` siempre informa `sensor_ok` y el equipo manda un latido cada 3 s mientras no hay sensor. El mock tiene `--sin-sensor` para probarlo | 20 min |
| `uptime_s` volvía a 0 a los 49,7 días (revisión final) | Historial y CSV desordenados | `millis()` es de 32 bits | `uptimeS()` con el contador de 64 bits `esp_timer_get_time()` | 10 min |
| Etiquetas con tilde podían romper el CSV (revisión final) | `pandas.read_csv` → `UnicodeDecodeError` | Al cortar a 23 bytes se partía un carácter UTF-8 (la "é" ocupa 2 bytes) | `limpiarEtiqueta()` corta en el límite del carácter; con tests | 15 min |
| `build_web.py --check` decía "desactualizado" en otra computadora | Falla aunque nadie tocó `web/` | El zlib de macOS y el de Linux comprimen el mismo HTML con bytes distintos | El chequeo compara la página **descomprimida** | 10 min |
| Test de empaquetado fallando por redondeo | `123.45f × 10` daba 1234 y no 1235 | 123,45 no es representable exacto en `float` (queda 123,4499…) | Se usó un valor de prueba sin ese borde (123,46) | 5 min |

## Pendientes de verificar con el hardware

- [x] El firmware se comunica con el BME680 (21,3 °C, 58 %, 1015,7 hPa). Se recuperó solo después de un falso contacto.
- [x] Sin `secrets.h` válido pasa a modo AP a los 15 s. El usuario abrió la web desde el celular.
- [ ] Modo estación con tu Wi-Fi en `secrets.h` (NTP + `aire.local`).
- [ ] Desconectar SDA con el equipo andando: los LEDs parpadean, la web dice "Sensor no detectado" y se recupera al reconectar.
- [ ] La precisión de BSEC llega a 3 y se restaura después de reiniciar.
- [ ] El offset de temperatura (`TEMP_OFFSET_C`), medido contra un termómetro.
