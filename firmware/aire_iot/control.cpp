// control.cpp — Procesamiento comparativo de confort para las 5 variables.
#include "control.h"
#include <math.h>

uint8_t bandaBosch(float iaq) {
  if (iaq <= 50) return 0;
  if (iaq <= 100) return 1;
  if (iaq <= 150) return 2;
  if (iaq <= 200) return 3;
  if (iaq <= 250) return 4;
  if (iaq <= 350) return 5;
  return 6;
}

const char* validarConfig(const Config& c) {
  if (c.silencioMin < 1 || c.silencioMin > 120)
    return "Silencio debe estar entre 1 y 120 minutos";
  return nullptr;
}

void calcularDeltasConfort(EstadoControl& s, const Lectura& aula, const Lectura& pasillo) {
  s.remotoConectado = pasillo.conectado;
  s.deltaIaq = aula.iaq - pasillo.iaq;
  s.deltaTemp = aula.temp - pasillo.temp;
  s.deltaHum = aula.hum - pasillo.hum;
  s.deltaLdr = aula.ldr - pasillo.ldr;
  s.deltaSonido = aula.sonido - pasillo.sonido;
}

Pulsacion botonPaso(EstadoBoton& b, bool presionado, uint32_t ahoraMs) {
  if (presionado != b.crudo) {
    b.crudo = presionado;
    b.cambioMs = ahoraMs;
  }
  if ((ahoraMs - b.cambioMs) >= DEBOUNCE_MS && b.crudo != b.estable) {
    b.estable = b.crudo;
    if (b.estable) {
      b.inicioMs = ahoraMs;
      b.largaEmitida = false;
    } else {
      if (!b.largaEmitida && (ahoraMs - b.inicioMs) < CORTA_MAX_MS) {
        return Pulsacion::CORTA;
      }
    }
  }
  if (b.estable && !b.largaEmitida && (ahoraMs - b.inicioMs) >= LARGA_MIN_MS) {
    b.largaEmitida = true;
    return Pulsacion::LARGA;
  }
  return Pulsacion::NINGUNA;
}
