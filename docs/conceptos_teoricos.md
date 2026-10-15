# Conceptos teóricos aplicados

Tres conceptos de la materia, identificados en este sistema y ligados al código concreto.

---

## 1. Realimentación: lazo abierto y lazo cerrado

**Definición.** En un sistema de **lazo abierto**, la acción de control no depende de la salida: el controlador actúa sin medir el resultado. En un sistema de **lazo cerrado** (realimentado), se mide la salida, se compara con una referencia y la diferencia (el error) determina la acción, que vuelve a modificar la salida.

**En este sistema.** La variable que nos interesa es la **calidad del aire del ambiente**, medida como IAQ.

- El equipo **mide** (BME680 + BSEC2), **compara** contra las referencias (umbrales 100 y 200) y **actúa** (LED y buzzer). Pero ni el LED ni el buzzer cambian el aire. **Respecto del ambiente, el lazo está abierto.**
- El lazo se cierra a través de una **persona**: ve el LED rojo o escucha la alarma, abre la ventana, el aire se renueva y el IAQ baja. Es un **lazo cerrado con humano en el lazo** (*human-in-the-loop*): la persona es el actuador que falta.
- Sin la persona (nadie en el aula, o alguien que ignora la alarma), el sistema sigue midiendo y avisando, pero la perturbación no se corrige. Por eso la alarma **se repite cada 30 segundos** mientras siga en rojo: insistir sobre el "actuador humano" es la única forma de influir en la salida.
- El lazo queda **cerrado de verdad** cuando un actuador físico actúa sobre el aire, como un extractor manejado por relé. Esa es la mejora propuesta para la etapa 2 ([mejora_propuesta.md](mejora_propuesta.md)).

**En el código.** El ciclo medir → comparar → actuar está en `loop()` (`firmware/aire_iot/aire_iot.ino`):

```cpp
Lectura l;
if (sensorActualizar(l)) {                                   // 1) medir
  if (controlPaso(control, l.iaq, l.precision, ahora, c))    // 2) comparar con la referencia
    buzzerAlarma();                                          // 3) actuar (sobre la persona)
  ...
}
...
actuadoresActualizar(control.led, !sensorOk());             // 3) actuar (LED)
```

El **controlador** es `controlPaso()` en `control.cpp`. Es una función pura: recibe la medición y la referencia (`Config`) y devuelve la acción. Como no depende del hardware, se prueba en la PC con `tools/test_nativo.cpp`.

---

## 2. Histéresis y estabilidad

**Definición.** Un controlador **on/off con un único umbral** cambia de estado cada vez que la señal cruza ese valor. Si la señal tiene ruido y está cerca del umbral, la salida **oscila** muy rápido (*chattering*): el LED parpadea, un relé se gastaría y un motor arrancaría y pararía sin sentido. La **histéresis** usa **dos umbrales**: uno para subir de estado y otro, más bajo, para bajar. Entre ambos queda una **banda muerta** en la que el estado no cambia, y así la salida se vuelve estable frente al ruido.

**En este sistema.** El IAQ tiene ruido de unos ±3 puntos entre muestras (el simulador lo modela con `3·sen(1,7t)·sen(0,31t)`). Con umbral 100 y **h = 15**:

- Se **pasa a amarillo** cuando IAQ > 100.
- Se **vuelve a verde** recién cuando IAQ < 85 (100 − h).

Ejemplo con la serie del test `testHisteresis()` (`tools/test_nativo.cpp`), un IAQ que oscila en el borde:

| IAQ | 99 | 102 | 99 | 102 | 99 | 95 | 101 |
|---|---|---|---|---|---|---|---|
| **Sin histéresis** (umbral 100) | Verde | Amarillo | Verde | Amarillo | Verde | Verde | Amarillo |
| **Con histéresis** (100 / 85) | Verde | Amarillo | Amarillo | Amarillo | Amarillo | Amarillo | Amarillo |

Sin histéresis hay **5 cambios** en 7 muestras (21 s). Con histéresis hay **1**. La banda debe ser **mayor que la amplitud del ruido** (15 > 2 × 3): si h fuera menor que el ruido, la oscilación volvería.

El costo es un **retardo en la bajada**: el LED sigue amarillo aunque el aire ya esté algo por debajo de 100. Por eso h es configurable desde la web: más h da más estabilidad y menos rapidez para "bajar la guardia".

**En el código.** `siguienteLed()` en `firmware/aire_iot/control.cpp`:

```cpp
Led siguienteLed(Led actual, float iaq, const Config& c) {
  // Subida: inmediata al superar el umbral.
  if (iaq > c.umbralRojo) return Led::ROJO;
  // Bajada desde rojo: solo si cae por debajo de (umbral − histéresis).
  if (actual == Led::ROJO && iaq >= c.umbralRojo - c.histeresis) return Led::ROJO;
  if (iaq > c.umbralAmarillo) return Led::AMARILLO;
  if (actual != Led::VERDE && iaq >= c.umbralAmarillo - c.histeresis) return Led::AMARILLO;
  return Led::VERDE;
}
```

Fijate que la función recibe el **estado actual**: la histéresis es **memoria**. Con el mismo IAQ = 90, la salida es verde si venía de verde y amarilla si venía de amarillo. La misma idea aparece en el pulsador: el **antirrebote** (`botonPaso()`) exige 30 ms de lectura estable antes de aceptar un cambio, que es una histéresis en el tiempo en lugar de en la amplitud.

---

## 3. Perturbaciones y tiempo de establecimiento

**Definición.** Una **perturbación** es una entrada no controlada que desvía la salida de su valor normal (por ejemplo, una perturbación escalón). El **tiempo de establecimiento** es cuánto tarda la salida en volver y quedarse dentro de una banda alrededor del valor final (típicamente 2 % o 5 %). En un sistema de primer orden con constante de tiempo **τ**, la respuesta es exponencial: a los 3τ recorrió el 95 % y a los 4–5τ, el 98–99 %.

**En este sistema.**

- **Perturbaciones:** cocinar, limpiar con lavandina o alcohol, varias personas respirando en un aula cerrada, abrir la puerta a un pasillo con humo. Cada una es, en la práctica, un **escalón** o un **pulso** de VOCs.
- **Recuperación al ventilar:** el aire se diluye con una dinámica aproximadamente de **primer orden**. El simulador (`simular()` en `firmware/aire_iot/sensor.h`) la modela con **τ = 120 s**:

  ```cpp
  double pico = p < 300 ? 0 : p < 420 ? 180 * (p - 300) / 120 : 180 * exp(-(p - 420) / 120);
  ```

  - El pico sube a +180 IAQ en 2 min (la perturbación).
  - Después decae con τ = 120 s: al 5 % en 3τ = 6 min y al 1 % en 5τ = 10 min.
  - Con la histéresis, el LED **vuelve a verde** cuando el pico cae por debajo de ~25 IAQ: 180·e^(−x/120) < 25, o sea **x ≈ 237 s ≈ 4 min** después de empezar a ventilar.

  En el gráfico de historial se ven la subida y la curva de descarga. Los **marcadores de evento** (pulsación larga) sirven para anotar cuándo empezó cada perturbación y cuándo se abrió la ventana, y así medir el tiempo de establecimiento real del ambiente con el CSV exportado.

- **Transitorio del propio sensor (calibración).** BSEC no confía en su IAQ hasta aprender cómo es el aire "limpio" y el "sucio" de ese lugar. Su **precisión** pasa de 0 a 3 en minutos u horas (la configuración usada tiene una ventana de **4 días**). Es un transitorio largo, del instrumento y no del ambiente, y el sistema lo trata explícitamente:
  - mientras la precisión es 0–1, **la alarma no suena** (`PRECISION_MIN_ALARMA = 2` en `config.h`, usado en `controlPaso()`), para no actuar con una medición que todavía no se estableció;
  - la web muestra "Calibrando" y avisa que el IAQ no es confiable;
  - la calibración se guarda en memoria no volátil (`storageGuardarBsec()`) para no repetir el transitorio en cada reinicio.

  ```cpp
  // Sin calibrar no suena, pero proxBeepMs queda vencido: suena apenas la precisión alcance.
  if (!yaPaso(ahora, s.proxBeepMs) || precision < PRECISION_MIN_ALARMA) return false;
  ```

- **Muestreo:** BSEC entrega una medición cada **3 s** (modo LP). Para una dinámica con τ = 120 s eso son unas 40 muestras por constante de tiempo, más que suficiente. El historial se guarda **promediado por minuto**, un filtro pasabajos simple que también reduce el ruido del dataset.
