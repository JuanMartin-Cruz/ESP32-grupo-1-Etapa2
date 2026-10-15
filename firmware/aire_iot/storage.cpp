// storage.cpp — ver storage.h.
#include <Arduino.h>
#include <Preferences.h>
#include "control.h"  // validarConfig()
#include "storage.h"

Anillo<Muestra, N_HIST> historial;
Anillo<Muestra, N_VIVO> vivo;
Anillo<Marcador, N_MARC> marcadores;
uint32_t epochArranque = 0;

static Preferences nvs;
static Promedio minuto;
static uint32_t inicioMinutoS = 0;
static uint16_t proximoId = 1;
// ponytail: un solo lock global para todos los buffers; alcanza porque cada sección crítica copia < 100 bytes.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static const uint32_t VERSION_CONFIG = 2;  // v2: repetirMin (min) → repetirSeg (s)  // subir si cambia el struct Config

void storageBloquear() { portENTER_CRITICAL(&lock); }
void storageDesbloquear() { portEXIT_CRITICAL(&lock); }

void storageIniciar() { nvs.begin("aire", false); }

uint32_t uptimeS() { return (uint32_t)(esp_timer_get_time() / 1000000LL); }

bool storageCargarConfig(Config& c) {
  Config leida;
  if (nvs.getUInt("cfg_ver", 0) != VERSION_CONFIG ||
      nvs.getBytes("cfg", &leida, sizeof leida) != sizeof leida || validarConfig(leida)) return false;
  c = leida;
  return true;
}

void storageGuardarConfig(const Config& c) {
  nvs.putBytes("cfg", &c, sizeof c);
  nvs.putUInt("cfg_ver", VERSION_CONFIG);
}

bool storageCargarBsec(uint8_t* buf, size_t tam) {
  return nvs.getBytesLength("bsec") == tam && nvs.getBytes("bsec", buf, tam) == tam;
}

void storageGuardarBsec(const uint8_t* buf, size_t tam) {
  nvs.putBytes("bsec", buf, tam);
  Serial.println(F("# Estado de calibración BSEC guardado en NVS"));
}

void storageAgregar(const Lectura& l, uint8_t led, uint32_t ahoraS) {
  Muestra m = empaquetar(l, ahoraS, led);
  bool cerrarMinuto = !minuto.vacio() && ahoraS - inicioMinutoS >= 60;
  Muestra prom;
  if (cerrarMinuto) prom = empaquetar(minuto.resultado(), inicioMinutoS, led);
  storageBloquear();
  vivo.agregar(m);
  if (cerrarMinuto) historial.agregar(prom);
  storageDesbloquear();
  if (cerrarMinuto || minuto.vacio()) {
    if (cerrarMinuto) minuto.reiniciar();
    inicioMinutoS = ahoraS;
  }
  minuto.sumar(l);
}

uint16_t storageMarcador(uint32_t t, const char* etiqueta) {
  Marcador m;
  m.t = t;
  limpiarEtiqueta(m.etiqueta, sizeof m.etiqueta, etiqueta);
  storageBloquear();
  m.id = proximoId++;
  marcadores.agregar(m);
  storageDesbloquear();
  return m.id;
}

bool storageRenombrar(uint16_t id, const char* etiqueta) {
  char limpia[sizeof(Marcador::etiqueta)];
  limpiarEtiqueta(limpia, sizeof limpia, etiqueta);
  bool ok = false;
  storageBloquear();
  for (uint32_t s = marcadores.primero(); s < marcadores.total; s++) {
    Marcador& m = marcadores.d[s % N_MARC];
    if (m.id == id) { memcpy(m.etiqueta, limpia, sizeof limpia); ok = true; }
  }
  storageDesbloquear();
  return ok;
}
