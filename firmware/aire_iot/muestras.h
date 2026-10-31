// muestras.h — Almacenamiento compacto de las 5 variables de confort y formato CSV.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sensor.h"

// Una muestra ocupa exactamente 24 bytes para optimizar la RAM del ESP32.
struct Muestra {
  uint32_t t;        // segundos desde el arranque (4 B)
  uint32_t gas;      // ohm (4 B)
  uint16_t iaq10;    // IAQ x 10 (2 B)
  uint16_t eco2;     // ppm (2 B)
  uint16_t bvoc100;  // ppm x 100 (2 B)
  int16_t temp100;   // °C x 100 (2 B)
  uint16_t hum100;   // % x 100 (2 B)
  uint16_t pres10;   // hPa x 10 (2 B)
  uint8_t prec;      // precisión BSEC (1 B)
  uint8_t ldr;       // Iluminación % (1 B)
  uint8_t sonido;    // Actividad sonora % (1 B)
  uint8_t reservado; // padding alineado a 24 bytes (1 B)
};
static_assert(sizeof(Muestra) == 24, "Muestra debe ocupar 24 bytes");

template <typename T, size_t N>
struct Anillo {
  T d[N];
  uint32_t total = 0;
  void agregar(const T& x) { d[total % N] = x; total++; }
  uint32_t primero() const { return total > N ? total - N : 0; }
  const T& en(uint32_t seq) const { return d[seq % N]; }
};

struct Promedio {
  double suma[9] = {}; // iaq, eco2, bvoc, temp, hum, pres, gas, ldr, sonido
  uint16_t n = 0;
  uint8_t precMin = 3;
  void sumar(const Lectura& l);
  bool vacio() const { return n == 0; }
  Lectura resultado() const;
  void reiniciar() { *this = Promedio(); }
};

struct Marcador {
  uint32_t t;
  uint16_t id;
  char etiqueta[24];
};

Muestra empaquetar(const Lectura& l, uint32_t t);

extern const char CSV_CABECERA[];
size_t formatearFila(char* buf, size_t tam, const Muestra& m, uint32_t epochArranque, const char* evento);
void limpiarEtiqueta(char* dst, size_t tam, const char* src);

template <size_t N>
const char* buscarEvento(const Anillo<Marcador, N>& mk, uint32_t desde, uint32_t hasta) {
  for (uint32_t s = mk.primero(); s < mk.total; s++) {
    const Marcador& m = mk.en(s);
    if (m.t >= desde && m.t < hasta) return m.etiqueta;
  }
  return "";
}
