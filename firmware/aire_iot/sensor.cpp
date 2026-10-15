// sensor.cpp — BME680 + BSEC2 o, con SIMULATION, el generador sintético de sensor.h.
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "sensor.h"
#include "storage.h"

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

// BSEC llama a esta función cuando tiene salidas nuevas.
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
      case BSEC_OUTPUT_RAW_PRESSURE: ultima.pres = o.signal; break;  // Bsec2::run() ya la pasó a hPa
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
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY, BSEC_OUTPUT_RAW_PRESSURE, BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_STABILIZATION_STATUS, BSEC_OUTPUT_RUN_IN_STATUS};
  if (!bsec.begin(BME680_ADDR, Wire)) return informarError("begin"), false;
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
  ultimoIntento = millis();
  ok = iniciarBsec();
  return ok;
}

bool sensorActualizar(Lectura& out) {
  uint32_t ahora = millis();
  if (!ok) {
    if (ahora - ultimoIntento < SENSOR_REINTENTO_MS) return false;
    sensorIniciar();  // reintentar; si funcionó, la lectura llega en una vuelta siguiente
    return false;
  }
  // run() devuelve false también ante advertencias (ej. el loop se demoró): solo los errores
  // (códigos negativos de BSEC o del BME68x, como una falla de I2C) cuentan como sensor caído.
  if (!bsec.run() && (bsec.status < BSEC_OK || bsec.sensor.status < BME68X_OK)) {
    ok = false;
    ultimoIntento = ahora;
    informarError("run");
    return false;
  }
  if (!hayNueva) return false;
  hayNueva = false;
  out = ultima;
  // Guardar la calibración: la primera vez que llega a 3 y después cada 4 h.
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
  Serial.println(F("# MODO SIMULACIÓN: datos sintéticos, no hay sensor"));
  return true;
}

bool sensorActualizar(Lectura& out) {
  uint32_t ahora = millis();
  if ((int32_t)(ahora - proxima) < 0) return false;
  proxima = ahora + SIM_PERIODO_MS;
  out = simular(esp_timer_get_time() / 1e6);
  return true;
}

bool sensorOk() { return true; }
const char* sensorVersion() { return "simulación"; }
#endif
