// config.h — ÚNICO lugar con pines, dirección I2C, tiempos y umbrales por defecto.
// No incluye Arduino.h para poder compilar los tests en la PC.
#pragma once
#include <stdint.h>

// Descomentá esta línea para reemplazar el BME680 por datos sintéticos
// (desarrollo de la UI, demo grabada o si el sensor falla en la presentación).
// #define SIMULATION

// ---------------- Pines (existen en placas de 30 y 38 pines) ----------------
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t PIN_LED_VERDE = 25;
constexpr uint8_t PIN_LED_AMARILLO = 26;
constexpr uint8_t PIN_LED_ROJO = 27;
constexpr uint8_t PIN_BUZZER = 32;   // KY-006 pasivo: necesita una onda (LEDC), no un HIGH
constexpr uint8_t PIN_BOTON = 33;    // a GND, con INPUT_PULLUP
constexpr uint8_t BME680_ADDR = 0x76; // SDO a GND. Si el escáner muestra 0x77, cambiá acá.

// ---------------- Calibración ----------------
// Diferencia entre la temperatura del BME680 y un termómetro de referencia (°C).
// El sensor se calienta a sí mismo y a la placa: medí y ajustá.
constexpr float TEMP_OFFSET_C = 0.0f;

// ---------------- Tiempos ----------------
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t CORTA_MAX_MS = 1000;            // pulsación corta: < 1 s
constexpr uint32_t LARGA_MIN_MS = 1500;            // pulsación larga: ≥ 1,5 s
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;        // después pasa a modo AP
constexpr uint32_t BSEC_GUARDAR_MS = 4UL * 3600UL * 1000UL;
constexpr uint32_t SENSOR_REINTENTO_MS = 10000;
constexpr uint32_t SIM_PERIODO_MS = 3000;          // igual que BSEC en modo LP
constexpr uint8_t PRECISION_MIN_ALARMA = 2;        // con 0–1 el IAQ no es confiable: no suena

// ---------------- Umbrales (editables desde la UI, se guardan en NVS) ----------------
struct Config {
  float umbralAmarillo = 100;  // IAQ > 100 → amarillo
  float umbralRojo = 200;      // IAQ > 200 → rojo
  float histeresis = 15;       // para bajar de nivel hay que caer por debajo de umbral − h
  uint16_t repetirSeg = 30;    // repetir la alarma cada N segundos mientras esté en rojo
  uint16_t silencioMin = 15;   // duración del silencio pedido con el botón
};
