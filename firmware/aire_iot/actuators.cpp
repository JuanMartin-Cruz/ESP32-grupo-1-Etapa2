// actuators.cpp — ver actuators.h. Todo sin delay(): el buzzer es un secuenciador de notas.
#include <Arduino.h>
#include "actuators.h"

struct Nota { uint16_t hz, ms; };  // hz = 0 es silencio; ms = 0 marca el fin
static const Nota ALARMA[] = {{2000, 150}, {0, 100}, {2000, 150}, {0, 100}, {2000, 150}, {0, 0}};
static const Nota CONFIRMA[] = {{3000, 60}, {0, 0}};
static const Nota* nota = nullptr;
static uint32_t finNota = 0;
static EstadoBoton boton;

static void tocar(const Nota* n) {
  nota = n;
  ledcWriteTone(PIN_BUZZER, n->hz);
  finNota = millis() + n->ms;
}

void actuadoresIniciar() {
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_AMARILLO, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BOTON, INPUT_PULLUP);
  ledcAttach(PIN_BUZZER, 2000, 10);
  ledcWriteTone(PIN_BUZZER, 0);
}

void buzzerAlarma() { tocar(ALARMA); }
void buzzerConfirmar() { tocar(CONFIRMA); }

void actuadoresActualizar(Led led, bool falla) {
  uint32_t ahora = millis();
  if (falla) {
    bool on = (ahora / 500) % 2;
    digitalWrite(PIN_LED_VERDE, on);
    digitalWrite(PIN_LED_AMARILLO, on);
    digitalWrite(PIN_LED_ROJO, on);
  } else {
    digitalWrite(PIN_LED_VERDE, led == Led::VERDE);
    digitalWrite(PIN_LED_AMARILLO, led == Led::AMARILLO);
    digitalWrite(PIN_LED_ROJO, led == Led::ROJO);
  }
  if (nota && (int32_t)(ahora - finNota) >= 0) {  // terminó la nota actual: pasar a la siguiente
    nota++;
    if (nota->ms == 0) { ledcWriteTone(PIN_BUZZER, 0); nota = nullptr; }
    else tocar(nota);
  }
}

Pulsacion botonLeer() {
  return botonPaso(boton, digitalRead(PIN_BOTON) == LOW, millis());  // LOW = apretado (pull-up)
}
