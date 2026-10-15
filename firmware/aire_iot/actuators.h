// actuators.h — LEDs, buzzer pasivo (LEDC) y lectura del pulsador.
#pragma once
#include "control.h"

void actuadoresIniciar();
// Enciende el LED del nivel. Con falla = true parpadean los tres (sensor no responde).
void actuadoresActualizar(Led led, bool falla);
void buzzerAlarma();      // tres beeps
void buzzerConfirmar();   // un beep corto (marcador guardado)
Pulsacion botonLeer();    // leer una vez por vuelta del loop
