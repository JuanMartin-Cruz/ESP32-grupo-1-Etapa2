// muestras.h — almacenamiento compacto de lecturas y formato CSV (puro, sin Arduino).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sensor.h"

// Una muestra ocupa 24 bytes: se guardan enteros escalados (ej. temp100 = °C × 100)
// para que 24 h de historial entren en la RAM del ESP32.
struct Muestra {
  uint32_t t;        // segundos desde el arranque
  uint32_t gas;      // ohm
  uint16_t iaq10, siaq10, eco2, bvoc100;
  int16_t temp100;
  uint16_t hum100, pres10;
  uint8_t prec, led;
};
static_assert(sizeof(Muestra) == 24, "Muestra debe ocupar 24 bytes");

// Buffer circular. 'total' cuenta todo lo agregado desde el arranque (número de secuencia),
// así un lector puede recorrerlo aunque entren muestras nuevas mientras lee.
template <typename T, size_t N>
struct Anillo {
  T d[N];
  uint32_t total = 0;
  void agregar(const T& x) { d[total % N] = x; total++; }
  uint32_t primero() const { return total > N ? total - N : 0; }  // secuencia más vieja disponible
  const T& en(uint32_t seq) const { return d[seq % N]; }
};

// Acumula las lecturas de un minuto para guardar el promedio.
struct Promedio {
  double suma[8] = {};
  uint16_t n = 0;
  uint8_t precMin = 3;  // se guarda la peor precisión del minuto
  void sumar(const Lectura& l);
  bool vacio() const { return n == 0; }
  Lectura resultado() const;
  void reiniciar() { *this = Promedio(); }
};

struct Marcador {
  uint32_t t;          // segundos desde el arranque
  uint16_t id;
  char etiqueta[24];
};

Muestra empaquetar(const Lectura& l, uint32_t t, uint8_t led);

extern const char CSV_CABECERA[];
// Escribe una fila CSV terminada en '\n'. epochArranque = 0 si no hubo NTP (columna vacía).
size_t formatearFila(char* buf, size_t tam, const Muestra& m, uint32_t epochArranque, const char* evento);
// Copia una etiqueta reemplazando , " \r \n por espacios y cortando a tam−1 bytes
// sin partir un carácter UTF-8.
void limpiarEtiqueta(char* dst, size_t tam, const char* src);

// Etiqueta del primer marcador con t en [desde, hasta), o "" si no hay.
template <size_t N>
const char* buscarEvento(const Anillo<Marcador, N>& mk, uint32_t desde, uint32_t hasta) {
  for (uint32_t s = mk.primero(); s < mk.total; s++) {
    const Marcador& m = mk.en(s);
    if (m.t >= desde && m.t < hasta) return m.etiqueta;
  }
  return "";
}
