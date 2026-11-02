// sensor.h — BME680 (BSEC2), Fotorresistor LDR y Sensor de Sonido digital KY-002.
#pragma once
#include <math.h>
#include <stdint.h>

struct Lectura {
  float iaq = 0;              // Índice de calidad de aire (0–500)
  float siaq = 0;             // IAQ estático
  float eco2 = 0;             // CO2 equivalente estimado [ppm]
  float bvoc = 0;             // VOC equivalente estimado [ppm]
  float temp = 0;             // Temperatura compensada [°C]
  float hum = 0;              // Humedad compensada [%]
  float gas = 0;              // Resistencia del sensor de gas [ohm]
  float ldr = 0;              // Nivel de iluminación [0–100 %]
  float sonido = 0;           // Nivel de actividad sonora / ruido [0–100 %]
  uint32_t pulsosSonido = 0;  // Conteo de pulsos de conmutación por segundo (Hz)
  uint8_t precision = 0;      // Precisión de calibración BSEC (0–3)
  bool conectado = true;      // Indicador de conexión / validez
};

// Generador sintético para desarrollo y pruebas sin hardware
inline Lectura simular(double t, bool exterior = false) {
  const double DOS_PI = 6.283185307179586;
  Lectura l;
  
  if (!exterior) {
    // ---- NODO INTERIOR (AULA) ----
    // Ciclo de 20 min: deriva lenta, pico de personas/respiración en 300–420 s y ventilación
    double p = fmod(t, 1200.0);
    double pico = p < 300 ? 0 : p < 420 ? 160 * (p - 300) / 120 : 160 * exp(-(p - 420) / 120);
    double iaq = 65 + 12 * sin(DOS_PI * t / 3600) + pico + 2 * sin(1.7 * t) * sin(0.31 * t);
    l.iaq = iaq < 0 ? 0 : iaq > 500 ? 500 : iaq;
    l.siaq = l.iaq * 0.95f;
    l.eco2 = 400 + 5 * (l.iaq > 25 ? l.iaq - 25 : 0);
    l.bvoc = 0.5f * exp(l.iaq / 100.0f);
    l.temp = 23.5f + 1.2f * sin(DOS_PI * t / 7200) + 0.01f * pico;
    l.hum = 48.0f + 4.0f * sin(DOS_PI * t / 5400) + 0.02f * pico;
    l.gas = 140000.0f * exp(-l.iaq / 150.0f);
    
    // Iluminación Aula: Luces encendidas (70–85%) con leve variación
    l.ldr = 78.0f + 5.0f * sin(DOS_PI * t / 600);
    
    // Ruido Aula: Bullicio de clase (~35%), con picos de debate/participación (hasta 75%)
    double ruidoBase = 32.0 + 8.0 * sin(0.7 * t) * sin(1.3 * t);
    double ruidoPico = (p > 250 && p < 450) ? 35.0 * sin(0.1 * p) : 0;
    double rTot = ruidoBase + (ruidoPico > 0 ? ruidoPico : 0);
    l.sonido = rTot < 5 ? 5 : rTot > 100 ? 100 : rTot;
    l.pulsosSonido = (uint32_t)(l.sonido * 14.5f);
    l.precision = t < 120 ? 0 : t < 300 ? 1 : t < 600 ? 2 : 3;
  } else {
    // ---- NODO EXTERIOR (PASILLO) ----
    // El pasillo suele estar más fresco y con aire más renovado, salvo picos de tránsito
    double p = fmod(t + 150.0, 1200.0);
    double iaqExt = 45 + 10 * sin(DOS_PI * t / 4000) + 4 * sin(0.9 * t);
    l.iaq = iaqExt < 10 ? 10 : iaqExt > 500 ? 500 : iaqExt;
    l.siaq = l.iaq * 0.92f;
    l.eco2 = 410 + 2 * l.iaq;
    l.bvoc = 0.4f * exp(l.iaq / 130.0f);
    l.temp = 20.8f + 1.8f * sin(DOS_PI * (t - 600) / 7200);
    l.hum = 52.0f + 6.0f * sin(DOS_PI * t / 5400);
    l.gas = 175000.0f * exp(-l.iaq / 160.0f);
    
    // Iluminación Pasillo: Luz difusa y natural (45–65%)
    l.ldr = 55.0f + 12.0f * sin(DOS_PI * t / 1800);
    
    // Ruido Pasillo: Generalmente silencioso (15–25%), con picos intensos en cambios de clase
    double transito = (p > 500 && p < 650) ? 55.0 * sin(0.08 * p) : 0;
    double rPasillo = 18.0 + 5.0 * sin(0.4 * t) + (transito > 0 ? transito : 0);
    l.sonido = rPasillo < 2 ? 2 : rPasillo > 100 ? 100 : rPasillo;
    l.pulsosSonido = (uint32_t)(l.sonido * 12.0f);
    l.precision = t < 90 ? 0 : t < 240 ? 1 : 3;
  }
  
  l.conectado = true;
  return l;
}

bool sensorIniciar();
bool sensorActualizar(Lectura& out);
bool sensorOk();
const char* sensorVersion();
float sensorLeerLdr();
float sensorLeerSonido(uint32_t& pulsosOut);
