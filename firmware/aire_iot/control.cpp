// control.cpp — ver control.h. Este archivo es el "controlador" del sistema:
// entrada = IAQ medido; salidas = estado de los LEDs y pedido de beep.
#include "control.h"

// Comparación de tiempos que sigue funcionando cuando millis() desborda (cada ~49,7 días).
static bool yaPaso(uint32_t ahora, uint32_t instante) {
  return (int32_t)(ahora - instante) >= 0;
}

uint8_t bandaBosch(float iaq) {
  const float limites[] = {50, 100, 150, 200, 250, 350};
  uint8_t b = 0;
  while (b < 6 && iaq > limites[b]) b++;
  return b;
}

Led siguienteLed(Led actual, float iaq, const Config& c) {
  // Subida: inmediata al superar el umbral.
  if (iaq > c.umbralRojo) return Led::ROJO;
  // Bajada desde rojo: solo si cae por debajo de (umbral − histéresis).
  if (actual == Led::ROJO && iaq >= c.umbralRojo - c.histeresis) return Led::ROJO;
  if (iaq > c.umbralAmarillo) return Led::AMARILLO;
  if (actual != Led::VERDE && iaq >= c.umbralAmarillo - c.histeresis) return Led::AMARILLO;
  return Led::VERDE;
}

const char* validarConfig(const Config& c) {
  // Escrito con !(…) para que un NaN también sea rechazado.
  if (!(c.umbralAmarillo > 0 && c.umbralAmarillo < c.umbralRojo && c.umbralRojo <= 500))
    return "Los umbrales deben cumplir 0 < amarillo < rojo <= 500";
  if (!(c.histeresis >= 0 && c.histeresis < c.umbralAmarillo))
    return "La histéresis debe cumplir 0 <= h < umbral amarillo";
  if (c.repetirSeg < 5 || c.repetirSeg > 600)
    return "La repetición de la alarma debe estar entre 5 y 600 segundos";
  if (c.silencioMin < 1 || c.silencioMin > 120)
    return "El silencio debe estar entre 1 y 120 minutos";
  return nullptr;
}

bool controlPaso(EstadoControl& s, float iaq, uint8_t precision, uint32_t ahora, const Config& c) {
  s.led = siguienteLed(s.led, iaq, c);
  if (s.led != Led::ROJO) {
    s.alarma = Alarma::NORMAL;
    return false;
  }
  if (s.alarma == Alarma::NORMAL) {  // acaba de entrar a rojo
    s.alarma = Alarma::ALARMA;
    s.proxBeepMs = ahora;
  }
  if (s.alarma == Alarma::SILENCIADA) {
    if (!yaPaso(ahora, s.finSilencioMs)) return false;
    s.alarma = Alarma::ALARMA;  // venció el silencio y sigue en rojo
    s.proxBeepMs = ahora;
  }
  // Sin calibrar no suena, pero proxBeepMs queda vencido: suena apenas la precisión alcance.
  if (!yaPaso(ahora, s.proxBeepMs) || precision < PRECISION_MIN_ALARMA) return false;
  s.proxBeepMs = ahora + c.repetirSeg * 1000UL;
  return true;
}

bool controlSilenciar(EstadoControl& s, uint32_t ahora, const Config& c) {
  if (s.alarma == Alarma::NORMAL) return false;
  s.alarma = Alarma::SILENCIADA;
  s.finSilencioMs = ahora + c.silencioMin * 60000UL;
  return true;
}

Pulsacion botonPaso(EstadoBoton& b, bool presionado, uint32_t ahora) {
  if (presionado != b.crudo) {  // la lectura cruda cambió: reiniciar la espera de debounce
    b.crudo = presionado;
    b.cambioMs = ahora;
  }
  if (b.crudo != b.estable && ahora - b.cambioMs >= DEBOUNCE_MS) {
    b.estable = b.crudo;
    if (b.estable) {
      b.inicioMs = ahora;
      b.largaEmitida = false;
    } else if (!b.largaEmitida && ahora - b.inicioMs < CORTA_MAX_MS) {
      return Pulsacion::CORTA;
    }
  }
  if (b.estable && !b.largaEmitida && ahora - b.inicioMs >= LARGA_MIN_MS) {
    b.largaEmitida = true;
    return Pulsacion::LARGA;
  }
  return Pulsacion::NINGUNA;
}
