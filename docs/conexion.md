# Conexionado

![Esquema de conexión](conexion.svg)

## Tabla de pines

Los pines están definidos en **un solo lugar**: `firmware/aire_iot/config.h`.

| Señal | Pin del ESP32 | Por qué este pin |
|---|---|---|
| BME680 VCC | **3V3** | El BME680 trabaja de 1,7 a 3,6 V. **Nunca a 5 V**. |
| BME680 GND | GND | — |
| BME680 SDA | **GPIO 21** | Pin I2C por defecto del ESP32 (`Wire`). |
| BME680 SCL | **GPIO 22** | Pin I2C por defecto del ESP32. |
| BME680 SDO | **GND** | Fija la dirección I2C en **0x76** (a 3V3 sería 0x77). |
| BME680 CS | **3V3** | En alto, el BME680 queda en modo I2C (en bajo entra en SPI). |
| LED verde (con 330 Ω en serie) | **GPIO 25** | Salida digital libre, sin función especial al arrancar. |
| LED amarillo (con 330 Ω en serie) | **GPIO 26** | Ídem. |
| LED rojo (con 330 Ω en serie) | **GPIO 27** | Ídem. |
| KY-006 **S** (señal) | **GPIO 32** | Tono generado por el periférico LEDC (PWM). |
| KY-006 **−** | GND | — |
| Pulsador | **GPIO 33** ↔ GND | Entrada con *pull-up* interno: suelto = HIGH, apretado = LOW. |

### Criterios de elección

- **Todos estos pines existen en las placas de 30 y de 38 pines**, así que el mismo cableado sirve para las dos variantes del DevKit V1.
- **No se usan GPIO 6–11**: están conectados a la memoria flash interna. Usarlos cuelga la placa.
- **No se usan pines de "strapping" (0, 2, 5, 12, 15)**: el ESP32 los lee al arrancar para decidir el modo de inicio. Con un LED o un pulsador conectado ahí, a veces no bootea o no se puede programar.
- GPIO 32 y 33 también sirven como entradas analógicas, pero acá se usan como digitales.

### Detalles

- **Resistencias de los LEDs:** con 330 Ω circulan unos (3,3 − 2,0) V / 330 Ω ≈ 4 mA, suficiente para verlos y muy por debajo del máximo de un GPIO. Con 220 Ω brillan un poco más (~6 mA).
- **Polaridad de los LEDs:** la pata larga (ánodo) va hacia la resistencia/GPIO; la corta (cátodo, lado plano) a GND.
- **Buzzer pasivo:** si ponés un nivel fijo, no suena. El firmware genera una onda cuadrada de 2–3 kHz con `ledcWriteTone()`. Si en tu kit el KY-006 tiene tres pines, el del medio no se usa.
- **Pulsador:** no necesita resistencia externa; el antirrebote se hace por software (30 ms).
- **Cables del BME680:** mantenelos cortos (< 20 cm). La mayoría de los breakouts ya traen resistencias pull-up para I2C.
- **Ubicación del sensor:** lejos de la placa del ESP32 y de los LEDs (calientan) y fuera del flujo directo de aire o del sol.

## Armado en la protoboard de 400 puntos

![Armado en la protoboard](protoboard.svg)

La protoboard usada tiene **400 puntos**: 30 filas numeradas con columnas **A–E** y **F–J** (300 agujeros), y dos rieles de alimentación a cada lado, de 25 agujeros cada uno (100). En cada fila, A–E están unidos entre sí y F–J también, pero las dos mitades están separadas por el canal central. Cada riel está unido en toda su longitud.

En esta protoboard, del lado izquierdo el riel **+** va por fuera y el **−** por dentro; del lado derecho es al revés (+ por dentro, − por fuera). Guiate por las líneas roja (+) y azul (−) impresas.

**El ESP32 va afuera de la protoboard.** El DevKit V1 mide unos 2,5 cm de ancho: montado en una protoboard de 400 puntos tapa casi todos los agujeros libres. Se conecta con cables **hembra–macho**.

| Componente | Agujeros | Conexión |
|---|---|---|
| BME680: VCC, GND, SCL, SDA, SDO, CS | A3 a A8 (el módulo queda sobre los rieles izquierdos) | — |
| Puente VCC | B3 → riel + izquierdo | 3,3 V al sensor |
| Puente GND | B4 → riel − izquierdo | GND al sensor |
| Puente SDO | C7 → C4 | SDO a GND (dirección 0x76) |
| Puente CS | D8 → D3 | CS a 3,3 V (modo I2C) |
| ESP32 GPIO 22 / GPIO 21 | E5 / E6 | SCL / SDA |
| ESP32 3V3 y un GND | riel + y riel − izquierdos | Alimentan el lado A–E |
| KY-006: S, +, − | A20, A21, A22 | GPIO 32 → E20; puente B22 → riel − izquierdo; + sin conectar |
| LED verde | GPIO 25 → J8; 330 Ω de G8 a G11; pata larga H11, pata corta H12 | J12 → riel − derecho |
| LED amarillo | GPIO 26 → J14; 330 Ω de G14 a G17; pata larga H17, pata corta H18 | J18 → riel − derecho |
| LED rojo | GPIO 27 → J22; 330 Ω de G22 a G25; pata larga H25, pata corta H26 | J26 → riel − derecho |
| Pulsador | Patas en E28, F28, E30 y F30 (sobre el canal central) | GPIO 33 → J28; puente C30 → riel − izquierdo |
| ESP32 otro GND | riel − derecho | GND de los LEDs |

Notas del armado:

- **Pulsador en diagonal:** el GPIO entra por la fila 28 del lado F–J y el GND sale por la fila 30 del lado A–E. Así funciona sin importar cómo esté girado el botón (en un pulsador de 4 patas, dos pares de patas están siempre unidos; si los dos cables quedaran sobre un mismo par, se leería "apretado" todo el tiempo).
- **Todos los pines GND del ESP32 son el mismo.** Se usan dos solo por comodidad, uno para cada mitad de la protoboard.
- **En los rieles sirve cualquier agujero:** las filas del dibujo son solo una referencia. Usá las que eviten cruzar cables.
- **Revisá con el tester** (continuidad) que los rieles no estén cortados al medio; en algunas protoboards lo están.

## Verificación rápida

1. Subí `tools/i2c_scanner/i2c_scanner.ino` y abrí el monitor serie a 115200: tiene que listar **0x76**.
2. Subí el firmware: al arrancar se enciende el LED verde (o parpadean los tres si el sensor no responde).
3. Mantené apretado el pulsador 1,5 s: tiene que sonar un beep corto (marcador guardado).
