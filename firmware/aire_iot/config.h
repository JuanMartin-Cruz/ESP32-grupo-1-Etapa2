// config.h — Configuración de hardware, roles y parámetros del sistema de confort.
// No incluye Arduino.h para permitir compilar tests nativos en PC.
#pragma once
#include <stdint.h>

// Descomentar para reemplazar los sensores físicos por el generador sintético
// #define SIMULATION

// ---------------- Roles de Nodo (Etapa 2) ----------------
#define ROL_AULA 1       // Nodo Interior (Aula): servidor central y concentrador
#define ROL_PASILLO 2    // Nodo Exterior (Pasillo): sensor remoto de referencia

#ifndef NODO_ROL
#define NODO_ROL ROL_AULA  // Cambiar a ROL_PASILLO para flashear el nodo exterior
#endif

// ---------------- Pines (Válidos en placas ESP32 DevKit de 30 y 38 pines) ----------------
// I2C para sensor Bosch BME680 (Calidad de aire, Temperatura, Humedad)
constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t BME680_ADDR = 0x76; // SDO a GND (0x77 si SDO a 3V3)

// Sensor analógico LDR (Iluminación / Confort lumínico)
// Conectado obligatoriamente a ADC1 (GPIO 32–39) porque ADC2 se inhabilita con Wi-Fi
constexpr uint8_t PIN_LDR = 34;       // ADC1_CH6 (solo entrada, sin strapping)

// Sensor de sonido digital KY-037 / KY-002 (Confort acústico)
// Muestreo a alta velocidad (kHz) mediante interrupción de hardware en pin digital DO
constexpr uint8_t PIN_SONIDO = 25;    // GPIO 25 con soporte de interrupción

// ---------------- Calibración y offsets ----------------
constexpr float TEMP_OFFSET_C = 0.0f;

// ---------------- Tiempos y red ----------------
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t CORTA_MAX_MS = 1000;
constexpr uint32_t LARGA_MIN_MS = 1500;
constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
constexpr uint32_t BSEC_GUARDAR_MS = 4UL * 3600UL * 1000UL;
constexpr uint32_t SENSOR_REINTENTO_MS = 10000;
constexpr uint32_t SIM_PERIODO_MS = 3000;

// Periodo de sincronización entre nodo pasillo y aula
constexpr uint32_t REMOTO_SYNC_MS = 3000;
constexpr uint32_t REMOTO_TIMEOUT_MS = 12000; // si pasan 12 s sin datos, marcar desconectado

// Configuración general
struct Config {
  uint16_t silencioMin = 15;
};
