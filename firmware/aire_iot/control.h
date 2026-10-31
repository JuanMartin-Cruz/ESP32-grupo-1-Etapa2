// control.h — Cálculo de deltas y análisis de confort entre Nodo Aula y Pasillo.
// Puro C++ (sin Arduino), testeable en PC.
#pragma once
#include <stdint.h>
#include "config.h"
#include "sensor.h"

enum class Pulsacion : uint8_t { NINGUNA = 0, CORTA = 1, LARGA = 2 };

// Estructura de estado comparativo de confort entre ambos nodos
struct EstadoControl {
  float deltaIaq = 0.0f;
  float deltaTemp = 0.0f;
  float deltaHum = 0.0f;
  float deltaLdr = 0.0f;
  float deltaSonido = 0.0f;
  bool remotoConectado = false;
};

struct EstadoBoton {
  bool crudo = false;
  bool estable = false;
  bool largaEmitida = false;
  uint32_t cambioMs = 0;
  uint32_t inicioMs = 0;
};

extern Config config;

uint8_t bandaBosch(float iaq);
const char* validarConfig(const Config& c);

// Calcula diferencias de las 5 variables de confort entre Aula y Pasillo
void calcularDeltasConfort(EstadoControl& s, const Lectura& aula, const Lectura& pasillo);

// Debounce y clasificación de pulsaciones
Pulsacion botonPaso(EstadoBoton& b, bool presionado, uint32_t ahoraMs);
