// sensor.cpp — BME680 + BSEC2, LDR y sensor de sonido digital KY-002.
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "sensor.h"
#include "storage.h"

// ---------------- Adquisición rápida de Sonido (KY-037 / KY-002) ----------------
static volatile uint32_t pulsosSubVentana = 0;
static uint32_t ultimoSubMuestreoMs = 0;
static uint32_t ultimoReporteSonidoMs = 0;
static uint32_t slotsConSonido = 0;
static uint32_t totalSubVentanas = 0;
static uint32_t pulsosAcumulados = 0;
static float nivelRuidoFiltrado = 10.0f;

static void IRAM_ATTR isrSonido() {
  pulsosSubVentana++;
}

float sensorLeerLdr() {
  uint32_t suma = 0;
  for (int i = 0; i < 8; i++) {
    suma += analogRead(PIN_LDR);
  }
  float crudo = (float)suma / 8.0f;
  // Convertir 0–4095 a porcentaje (0–100%)
  float pct = (crudo / 4095.0f) * 100.0f;
  return pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct;
}

float sensorLeerSonido(uint32_t& pulsosOut) {
  uint32_t ahora = millis();
  uint32_t dt = ahora - ultimoReporteSonidoMs;
  if (dt == 0) dt = 1;

  float factorOcupacion = (totalSubVentanas > 0) ? ((float)slotsConSonido / totalSubVentanas) * 100.0f : 0.0f;
  float frecuenciaMediaHz = (pulsosAcumulados * 1000.0f) / dt;
  pulsosOut = (uint32_t)frecuenciaMediaHz;

  // Normalizar porcentaje de ruido combinando ocupación continua + densidad de pulsos
  float nivelCrudo = (factorOcupacion * 0.60f) + ((frecuenciaMediaHz / 200.0f) * 40.0f);
  if (nivelCrudo > 100.0f) nivelCrudo = 100.0f;

  // Filtro exponencial tipo sonómetro IEC 61672
  const float ALFA = 0.35f;
  nivelRuidoFiltrado = (ALFA * nivelCrudo) + ((1.0f - ALFA) * nivelRuidoFiltrado);

  slotsConSonido = 0;
  totalSubVentanas = 0;
  pulsosAcumulados = 0;
  ultimoReporteSonidoMs = ahora;

  return nivelRuidoFiltrado;
}

#ifndef SIMULATION
#include <bsec2.h>

// Configuración de Bosch para BME680 a 3,3 V, muestreo cada 3 s (LP), calibración en 4 días.
static const uint8_t bsecConfig[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};

static Bsec2 bsec;
static Lectura ultima;
static bool hayNueva = false, ok = false, guardadoInicial = false;
static uint32_t ultimoIntento = 0, ultimoGuardado = 0;
static char version[20] = "?";

static void alRecibir(const bme68xData, const bsecOutputs salidas, const Bsec2) {
  for (uint8_t i = 0; i < salidas.nOutputs; i++) {
    const bsecData& o = salidas.output[i];
    switch (o.sensor_id) {
      case BSEC_OUTPUT_IAQ: ultima.iaq = o.signal; ultima.precision = o.accuracy; break;
      case BSEC_OUTPUT_STATIC_IAQ: ultima.siaq = o.signal; break;
      case BSEC_OUTPUT_CO2_EQUIVALENT: ultima.eco2 = o.signal; break;
      case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT: ultima.bvoc = o.signal; break;
      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE: ultima.temp = o.signal; break;
      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY: ultima.hum = o.signal; break;
      case BSEC_OUTPUT_RAW_GAS: ultima.gas = o.signal; break;
      default: break;
    }
  }
  hayNueva = salidas.nOutputs > 0;
}

static void informarError(const char* donde) {
  Serial.printf("# Sensor no disponible (%s): BSEC=%d BME68x=%d. Reintento en %lu s\n", donde,
                (int)bsec.status, (int)bsec.sensor.status, SENSOR_REINTENTO_MS / 1000);
}

static bool iniciarBsec() {
  bsecSensor lista[] = {
      BSEC_OUTPUT_IAQ, BSEC_OUTPUT_STATIC_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY, BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_STABILIZATION_STATUS, BSEC_OUTPUT_RUN_IN_STATUS};

  uint8_t dirUsada = BME680_ADDR;
  bool beginOk = bsec.begin(dirUsada, Wire);
  if (!beginOk) {
    uint8_t alt = (BME680_ADDR == 0x76) ? 0x77 : 0x76;
    beginOk = bsec.begin(alt, Wire);
    if (beginOk) {
      dirUsada = alt;
      Serial.printf("# BME680 detectado en dirección alternativa I2C: 0x%02X\n", alt);
    }
  }
  if (!beginOk) {
    informarError("begin (0x76 y 0x77)");
    Serial.println(F("# BME680 no detectado: midiendo LDR y Micrófono mientras se reintenta..."));
    return false;
  }
  Serial.printf("# BME680 iniciado correctamente en I2C 0x%02X\n", dirUsada);

  bsec.setTemperatureOffset(TEMP_OFFSET_C);
  if (!bsec.setConfig(bsecConfig)) return informarError("setConfig"), false;
  uint8_t estado[BSEC_MAX_STATE_BLOB_SIZE];
  if (storageCargarBsec(estado, sizeof estado)) {
    if (bsec.setState(estado)) Serial.println(F("# Calibración BSEC restaurada desde NVS"));
    else Serial.println(F("# Estado BSEC inválido: se descarta y calibra de cero"));
  }
  if (!bsec.updateSubscription(lista, sizeof lista / sizeof lista[0], BSEC_SAMPLE_RATE_LP))
    return informarError("updateSubscription"), false;
  bsec.attachCallback(alRecibir);
  snprintf(version, sizeof version, "%d.%d.%d.%d", bsec.version.major, bsec.version.minor,
           bsec.version.major_bugfix, bsec.version.minor_bugfix);
  return true;
}

bool sensorIniciar() {
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_SONIDO, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_SONIDO), isrSonido, CHANGE);
  ultimoSubMuestreoMs = millis();
  ultimoReporteSonidoMs = millis();

  // Inicializar lecturas base para disponibilidad inmediata
  ultima.ldr = sensorLeerLdr();
  ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
  ultima.iaq = 25.0f;
  ultima.temp = 22.0f;
  ultima.hum = 50.0f;
  ultima.gas = 50000.0f;
  ultima.precision = 0;
  ultima.conectado = true;

  ultimoIntento = millis();
  ok = iniciarBsec();
  return ok;
}

bool sensorActualizar(Lectura& out) {
  uint32_t ahora = millis();

  // Sub-muestreo acústico continuo cada 50 ms
  if (ahora - ultimoSubMuestreoMs >= 50) {
    ultimoSubMuestreoMs = ahora;
    noInterrupts();
    uint32_t p = pulsosSubVentana;
    pulsosSubVentana = 0;
    interrupts();
    totalSubVentanas++;
    pulsosAcumulados += p;
    if (p > 0) slotsConSonido++;
  }

  // Si BME680 no está disponible o falló, generamos muestra periódica con LDR y Sonido
  if (!ok) {
    static uint32_t ultimoEnvioSinBsec = 0;
    if (ahora - ultimoIntento >= SENSOR_REINTENTO_MS) {
      ultimoIntento = ahora;
      ok = iniciarBsec();
    }
    if (ahora - ultimoEnvioSinBsec >= 1000) {
      ultimoEnvioSinBsec = ahora;
      ultima.ldr = sensorLeerLdr();
      ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
      ultima.conectado = true;
      out = ultima;
      return true;
    }
    return false;
  }

  if (!bsec.run() && (bsec.status < BSEC_OK || bsec.sensor.status < BME68X_OK)) {
    ok = false;
    ultimoIntento = ahora;
    informarError("run");
    return false;
  }

  if (!hayNueva) {
    // Si BSEC tarda en su ciclo de 3s pero ya tenemos lecturas, refrescamos sonido y luz cada 1s
    static uint32_t ultimoRefrescoRapido = 0;
    if (ultima.conectado && (ahora - ultimoRefrescoRapido >= 1000)) {
      ultimoRefrescoRapido = ahora;
      ultima.ldr = sensorLeerLdr();
      ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
      out = ultima;
      return true;
    }
    return false;
  }
  hayNueva = false;

  // Incorporar lecturas de LDR y Sonido digital muestreado a alta velocidad
  ultima.ldr = sensorLeerLdr();
  ultima.sonido = sensorLeerSonido(ultima.pulsosSonido);
  ultima.conectado = true;
  out = ultima;

  if (ultima.precision == 3 && (!guardadoInicial || ahora - ultimoGuardado >= BSEC_GUARDAR_MS)) {
    uint8_t estado[BSEC_MAX_STATE_BLOB_SIZE];
    if (bsec.getState(estado)) {
      storageGuardarBsec(estado, sizeof estado);
      guardadoInicial = true;
      ultimoGuardado = ahora;
    }
  }
  return true;
}

bool sensorOk() { return ok; }
const char* sensorVersion() { return version; }

#else  // ---------------- SIMULATION ----------------

static uint32_t proxima = 0;

bool sensorIniciar() {
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_SONIDO, INPUT);
  Serial.printf("# MODO SIMULACIÓN: Nodo %s con datos sintéticos\n",
                NODO_ROL == ROL_AULA ? "AULA (INTERIOR)" : "PASILLO (EXTERIOR)");
  return true;
}

bool sensorActualizar(Lectura& out) {
  uint32_t ahora = millis();
  if ((int32_t)(ahora - proxima) < 0) return false;
  proxima = ahora + SIM_PERIODO_MS;
  out = simular(esp_timer_get_time() / 1e6, NODO_ROL == ROL_PASILLO);
  return true;
}

bool sensorOk() { return true; }
const char* sensorVersion() { return "simulación"; }
#endif
