// actuators.cpp — Implementación sin LEDs ni buzzer (descartados en Etapa 2).
#include <Arduino.h>
#include "actuators.h"
#include "config.h"

static EstadoBoton boton;

void actuadoresIniciar() {
  pinMode(PIN_BOTON, INPUT_PULLUP);
}

Pulsacion botonLeer() {
  return botonPaso(boton, digitalRead(PIN_BOTON) == LOW, millis());
}
