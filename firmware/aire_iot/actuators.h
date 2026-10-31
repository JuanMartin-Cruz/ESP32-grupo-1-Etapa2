// actuators.h — Manejo de entradas y eventos (sin LEDs ni buzzer en Etapa 2).
#pragma once
#include <stdint.h>
#include "control.h"

void actuadoresIniciar();
// En Etapa 2 se descartaron LEDs y Buzzer: se mantiene botonLeer para marcadores
Pulsacion botonLeer();
