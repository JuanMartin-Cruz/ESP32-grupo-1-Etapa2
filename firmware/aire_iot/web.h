// web.h — Wi-Fi, servidor HTTP y WebSocket.
#pragma once
#include "control.h"
#include "sensor.h"

void webIniciar();
void webActualizar();
void webPublicar(const Lectura& l, const EstadoControl& s);  // envía por /ws y guarda para /api/current
bool webPedidoSilencio();
void webLatidoSinSensor();  // con el sensor caído: avisa a la UI que el equipo sigue vivo  // true una vez por cada "Silenciar alarma" de la UI
