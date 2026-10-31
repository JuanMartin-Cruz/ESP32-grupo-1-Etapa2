// storage.h — memoria no volátil (NVS) y buffers de historial en RAM.
#pragma once
#include "config.h"
#include "muestras.h"

constexpr size_t N_HIST = 1440;  // 24 h × 1 muestra por minuto
constexpr size_t N_VIVO = 300;   // 15 min × 1 muestra cada 3 s
constexpr size_t N_MARC = 64;    // marcadores de eventos

extern Anillo<Muestra, N_HIST> historial;
extern Anillo<Muestra, N_VIVO> vivo;
extern Anillo<Marcador, N_MARC> marcadores;
extern uint32_t epochArranque;

void storageIniciar();
uint32_t uptimeS();
bool storageCargarConfig(Config& c);
void storageGuardarConfig(const Config& c);
bool storageCargarBsec(uint8_t* buf, size_t tam);
void storageGuardarBsec(const uint8_t* buf, size_t tam);
void storageAgregar(const Lectura& l, uint32_t ahoraS);
uint16_t storageMarcador(uint32_t t, const char* etiqueta);
bool storageRenombrar(uint16_t id, const char* etiqueta);
void storageBloquear();
void storageDesbloquear();
