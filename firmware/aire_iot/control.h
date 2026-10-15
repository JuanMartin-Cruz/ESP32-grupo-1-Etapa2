// control.h — lógica de control pura (sin Arduino): niveles con histéresis,
// máquina de estados de la alarma y clasificación de pulsaciones del botón.
#pragma once
#include <stdint.h>
#include "config.h"

enum class Led : uint8_t { VERDE = 0, AMARILLO = 1, ROJO = 2 };
enum class Alarma : uint8_t { NORMAL = 0, ALARMA = 1, SILENCIADA = 2 };
enum class Pulsacion : uint8_t { NINGUNA = 0, CORTA = 1, LARGA = 2 };

struct EstadoControl {
  Led led = Led::VERDE;
  Alarma alarma = Alarma::NORMAL;
  uint32_t proxBeepMs = 0;     // cuándo toca el próximo beep (en ALARMA)
  uint32_t finSilencioMs = 0;  // cuándo vence el silencio (en SILENCIADA)
};

struct EstadoBoton {
  bool crudo = false;          // última lectura sin filtrar (true = apretado)
  bool estable = false;        // lectura filtrada por debounce
  bool largaEmitida = false;   // ya avisamos la pulsación larga de esta presión
  uint32_t cambioMs = 0;       // último cambio de la lectura cruda
  uint32_t inicioMs = 0;       // cuándo empezó la presión estable
};

extern Config config;  // instancia global, definida en aire_iot.ino

// Banda de IAQ de Bosch: 0 excelente … 6 extremadamente contaminado.
uint8_t bandaBosch(float iaq);
// Estado de LED siguiente, con histéresis: sube al superar el umbral,
// baja recién por debajo de (umbral − histéresis).
Led siguienteLed(Led actual, float iaq, const Config& c);
// nullptr si la config es válida; si no, el mensaje de error en español.
const char* validarConfig(const Config& c);
// Un paso del lazo: actualiza LED y alarma. Devuelve true si hay que sonar el buzzer.
bool controlPaso(EstadoControl& s, float iaq, uint8_t precision, uint32_t ahoraMs, const Config& c);
// Silencia la alarma si está activa. Devuelve true si cambió algo.
bool controlSilenciar(EstadoControl& s, uint32_t ahoraMs, const Config& c);
// Debounce + clasificación. Llamar seguido (cada vuelta del loop).
Pulsacion botonPaso(EstadoBoton& b, bool presionado, uint32_t ahoraMs);
