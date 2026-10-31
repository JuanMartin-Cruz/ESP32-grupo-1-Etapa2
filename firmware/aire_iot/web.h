// web.h — Wi-Fi, sincronización multi-nodo, HTTP REST y WebSocket.
#pragma once
#include "control.h"
#include "sensor.h"

void webIniciar();
void webActualizar();
void webPublicar(const Lectura& local, const Lectura& remoto, const EstadoControl& s);
bool webObtenerRemoto(Lectura& out);
void webEnviarLecturaRemota(const Lectura& local);
bool webPedidoSilencio();
void webLatidoSinSensor();
