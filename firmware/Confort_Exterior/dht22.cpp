// dht22.cpp — Lector de pulsos digitales a microsegundos para sensor DHT22 (AM2302)
#include "dht22.h"

void DHT22::begin() {
  pinMode(_pin, INPUT_PULLUP);
}

bool DHT22::leer(float& temperatura, float& humedad) {
  uint8_t datos[5] = {0, 0, 0, 0, 0};

  // 1. Pulso de inicio: host baja la línea 18 ms
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
  delay(18);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(30);
  pinMode(_pin, INPUT_PULLUP);

  // 2. Esperar respuesta del sensor (80us LOW, 80us HIGH)
  uint32_t inicio = micros();
  while (digitalRead(_pin) == HIGH) {
    if (micros() - inicio > 100) return false;
  }
  inicio = micros();
  while (digitalRead(_pin) == LOW) {
    if (micros() - inicio > 100) return false;
  }
  inicio = micros();
  while (digitalRead(_pin) == HIGH) {
    if (micros() - inicio > 100) return false;
  }

  // 3. Leer los 40 bits
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);

  for (int i = 0; i < 40; i++) {
    uint32_t t = micros();
    while (digitalRead(_pin) == LOW) {
      if (micros() - t > 90) { portEXIT_CRITICAL(&mux); return false; }
    }
    uint32_t tInicioAlto = micros();
    while (digitalRead(_pin) == HIGH) {
      if (micros() - tInicioAlto > 110) { portEXIT_CRITICAL(&mux); return false; }
    }
    uint32_t duracionAlto = micros() - tInicioAlto;
    if (duracionAlto > 40) {
      datos[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  portEXIT_CRITICAL(&mux);

  // 4. Verificación de checksum
  if (((datos[0] + datos[1] + datos[2] + datos[3]) & 0xFF) != datos[4]) {
    temperatura = _ultimaTemp;
    humedad = _ultimaHum;
    return false;
  }

  // 5. Conversión de magnitudes
  float h = ((datos[0] << 8) | datos[1]) * 0.1f;
  int16_t rawT = ((datos[2] & 0x7F) << 8) | datos[3];
  if (datos[2] & 0x80) rawT = -rawT;
  float t = rawT * 0.1f;

  if (h >= 0.0f && h <= 100.0f && t >= -40.0f && t <= 80.0f) {
    _ultimaTemp = t;
    _ultimaHum = h;
    _ultimoExito = millis();
    temperatura = t;
    humedad = h;
    return true;
  }

  temperatura = _ultimaTemp;
  humedad = _ultimaHum;
  return false;
}

