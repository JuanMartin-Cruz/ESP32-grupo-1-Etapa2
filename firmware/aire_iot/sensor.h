// sensor.h — lectura del BME680 procesada por BSEC2 (o sintética con SIMULATION).
#pragma once
#include <math.h>
#include <stdint.h>

struct Lectura {
  float iaq = 0;    // índice de calidad de aire 0–500 (escalado)
  float siaq = 0;   // IAQ estático (sin escalar, para equipos fijos)
  float eco2 = 0;   // CO2 equivalente estimado [ppm] — NO es un sensor de CO2 real
  float bvoc = 0;   // VOC de aliento equivalente estimado [ppm]
  float temp = 0;   // temperatura compensada [°C]
  float hum = 0;    // humedad compensada [%]
  float pres = 0;   // presión [hPa]
  float gas = 0;    // resistencia del sensor de gas [ohm]
  uint8_t precision = 0;  // precisión del IAQ según BSEC: 0 = sin calibrar … 3 = calibrado
};

// Generador sintético. MISMAS fórmulas en tools/mock_server.py (función simular):
// si cambiás una, cambiá la otra y los valores de referencia de ambos tests.
// Ciclo de 20 min: deriva lenta, pico de contaminación (cocinar) en 300–420 s
// y recuperación exponencial al ventilar (τ = 120 s).
inline Lectura simular(double t) {
  const double DOS_PI = 6.283185307179586;
  double p = fmod(t, 1200.0);
  double pico = p < 300 ? 0 : p < 420 ? 180 * (p - 300) / 120 : 180 * exp(-(p - 420) / 120);
  double iaq = 60 + 15 * sin(DOS_PI * t / 3600) + pico + 3 * sin(1.7 * t) * sin(0.31 * t);
  iaq = iaq < 0 ? 0 : iaq > 500 ? 500 : iaq;
  Lectura l;
  l.iaq = iaq;
  l.siaq = iaq * 0.95;
  l.eco2 = 400 + 5 * (iaq > 25 ? iaq - 25 : 0);
  l.bvoc = 0.5 * exp(iaq / 100);
  l.temp = 23 + 1.5 * sin(DOS_PI * t / 7200);
  l.hum = 45 + 5 * sin(DOS_PI * t / 5400) + 0.03 * pico;
  l.pres = 1013 + 0.5 * sin(DOS_PI * t / 10800);
  l.gas = 150000 * exp(-iaq / 150);
  l.precision = t < 120 ? 0 : t < 300 ? 1 : t < 600 ? 2 : 3;
  return l;
}

bool sensorIniciar();                  // true si el sensor respondió
bool sensorActualizar(Lectura& out);   // true cuando hay una lectura nueva (cada 3 s)
bool sensorOk();                       // false si el sensor no responde
const char* sensorVersion();           // versión de BSEC o "simulación"
