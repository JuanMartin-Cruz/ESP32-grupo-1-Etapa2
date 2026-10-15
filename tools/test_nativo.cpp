// Tests nativos (en la PC, sin ESP32) de la lógica pura del firmware.
// Compilar y correr:
//   g++ -std=c++17 -Wall -Wextra -I firmware/aire_iot tools/test_nativo.cpp \
//       firmware/aire_iot/control.cpp firmware/aire_iot/muestras.cpp -o build/test_nativo && build/test_nativo
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "control.h"
#include "muestras.h"

Config config;  // el firmware la define en aire_iot.ino

static void testBandas() {
  assert(bandaBosch(0) == 0 && bandaBosch(50) == 0 && bandaBosch(51) == 1);
  assert(bandaBosch(100) == 1 && bandaBosch(150) == 2 && bandaBosch(200) == 3);
  assert(bandaBosch(250) == 4 && bandaBosch(350) == 5 && bandaBosch(351) == 6);
}

static void testHisteresis() {
  Config c;  // 100 / 200 / h = 15
  assert(siguienteLed(Led::VERDE, 100, c) == Led::VERDE);      // el umbral exacto no sube
  assert(siguienteLed(Led::VERDE, 101, c) == Led::AMARILLO);
  assert(siguienteLed(Led::AMARILLO, 90, c) == Led::AMARILLO);  // dentro de la banda: se queda
  assert(siguienteLed(Led::AMARILLO, 85, c) == Led::AMARILLO);  // 85 = 100 − 15: todavía no baja
  assert(siguienteLed(Led::AMARILLO, 84.9f, c) == Led::VERDE);
  assert(siguienteLed(Led::AMARILLO, 201, c) == Led::ROJO);
  assert(siguienteLed(Led::ROJO, 190, c) == Led::ROJO);
  assert(siguienteLed(Led::ROJO, 184, c) == Led::AMARILLO);
  assert(siguienteLed(Led::ROJO, 90, c) == Led::AMARILLO);      // cae dentro de la banda amarilla
  assert(siguienteLed(Led::ROJO, 50, c) == Led::VERDE);         // salto de dos niveles
  assert(siguienteLed(Led::VERDE, 300, c) == Led::ROJO);
  // Oscilación en el borde: 99 ↔ 102 no hace parpadear el LED una vez en amarillo
  Led l = Led::VERDE;
  float serie[] = {99, 102, 99, 102, 99, 95, 101};
  for (float v : serie) l = siguienteLed(l, v, c);
  assert(l == Led::AMARILLO);
}

static void testValidacion() {
  Config c;
  assert(validarConfig(c) == nullptr);
  c.umbralAmarillo = 250; assert(validarConfig(c) != nullptr);  // amarillo ≥ rojo
  c = Config(); c.umbralRojo = 501; assert(validarConfig(c) != nullptr);
  c = Config(); c.histeresis = -1; assert(validarConfig(c) != nullptr);
  c = Config(); c.histeresis = 100; assert(validarConfig(c) != nullptr);
  c = Config(); c.umbralAmarillo = NAN; assert(validarConfig(c) != nullptr);
  c = Config(); c.histeresis = NAN; assert(validarConfig(c) != nullptr);
  c = Config(); c.repetirSeg = 4; assert(validarConfig(c) != nullptr);    // menos de 5 s: sería un pitido continuo
  c = Config(); c.repetirSeg = 601; assert(validarConfig(c) != nullptr);
  c = Config(); c.silencioMin = 121; assert(validarConfig(c) != nullptr);
}

static void testAlarma(uint32_t t0) {  // t0 permite probar el desborde de millis()
  Config c;  // repetir cada 30 s, silencio 15 min
  EstadoControl s;
  const uint32_t MIN = 60000;
  assert(!controlPaso(s, 150, 3, t0, c) && s.alarma == Alarma::NORMAL);
  assert(controlPaso(s, 250, 3, t0 + 3000, c) && s.alarma == Alarma::ALARMA);  // entra a rojo: suena
  assert(!controlPaso(s, 250, 3, t0 + 6000, c));                                // no repite enseguida
  assert(!controlPaso(s, 250, 3, t0 + 3000 + 29000, c));                        // todavía no pasaron 30 s
  assert(controlPaso(s, 250, 3, t0 + 3000 + 30000, c));                         // repite a los 30 s
  assert(controlPaso(s, 250, 3, t0 + 3000 + 60000, c));                         // y otra vez a los 60 s
  assert(controlSilenciar(s, t0 + 6 * MIN, c) && s.alarma == Alarma::SILENCIADA);
  assert(!controlPaso(s, 250, 3, t0 + 10 * MIN, c));                             // silenciada
  assert(controlPaso(s, 250, 3, t0 + 21 * MIN, c) && s.alarma == Alarma::ALARMA); // venció el silencio
  assert(!controlPaso(s, 100, 3, t0 + 22 * MIN, c) && s.alarma == Alarma::NORMAL); // sale de rojo
  assert(!controlSilenciar(s, t0 + 22 * MIN, c));                                // nada que silenciar
}

static void testAlarmaSinCalibrar() {
  Config c;
  EstadoControl s;
  assert(!controlPaso(s, 300, 1, 1000, c) && s.alarma == Alarma::ALARMA);  // rojo pero precisión 1: muda
  assert(s.led == Led::ROJO);                                               // el LED sí se enciende
  assert(controlPaso(s, 300, 2, 4000, c));                                  // llega a precisión 2: suena ya
}

static void testBoton() {
  EstadoBoton b;
  // Rebote: 10 ms apretado no cuenta
  assert(botonPaso(b, true, 0) == Pulsacion::NINGUNA);
  assert(botonPaso(b, false, 10) == Pulsacion::NINGUNA);
  assert(botonPaso(b, false, 100) == Pulsacion::NINGUNA);
  // Corta: 300 ms
  botonPaso(b, true, 1000);
  assert(botonPaso(b, true, 1040) == Pulsacion::NINGUNA);  // estable apretado
  botonPaso(b, false, 1300);
  assert(botonPaso(b, false, 1340) == Pulsacion::CORTA);
  // Larga: se avisa mientras sigue apretado, una sola vez, y al soltar no hay corta
  botonPaso(b, true, 5000);
  botonPaso(b, true, 5040);
  assert(botonPaso(b, true, 6539) == Pulsacion::NINGUNA);
  assert(botonPaso(b, true, 6540) == Pulsacion::LARGA);
  assert(botonPaso(b, true, 7000) == Pulsacion::NINGUNA);
  botonPaso(b, false, 7100);
  assert(botonPaso(b, false, 7140) == Pulsacion::NINGUNA);
  // Entre 1 s y 1,5 s: ni corta ni larga
  botonPaso(b, true, 9000);
  botonPaso(b, true, 9040);
  botonPaso(b, false, 10240);
  assert(botonPaso(b, false, 10280) == Pulsacion::NINGUNA);
}

static bool cerca(double a, double b, double tol) { return fabs(a - b) <= tol; }

static void testSimular() {
  // Valores de referencia: los mismos en tools/mock_server.py --selftest
  Lectura a = simular(0);
  assert(cerca(a.iaq, 60.0, 0.01) && cerca(a.eco2, 575.0, 0.05) && a.precision == 0);
  Lectura b = simular(360);
  assert(cerca(b.iaq, 157.101, 0.01) && cerca(b.hum, 49.734, 0.01) && b.precision == 2);
  Lectura c = simular(450);
  assert(cerca(c.iaq, 207.926, 0.01) && cerca(c.gas, 37504.485, 1.0));
  Lectura d = simular(4000);
  assert(cerca(d.iaq, 222.044, 0.01) && cerca(d.temp, 22.487, 0.01) && d.precision == 3);
}

static void testAnillo() {
  Anillo<int, 3> a;
  assert(a.primero() == 0 && a.total == 0);           // vacío: no hay nada que recorrer
  a.agregar(10); a.agregar(11);
  assert(a.primero() == 0 && a.total == 2 && a.en(0) == 10 && a.en(1) == 11);
  a.agregar(12); a.agregar(13);                        // pisa al 10
  assert(a.primero() == 1 && a.en(1) == 11 && a.en(3) == 13);
}

static void testEmpaquetarYCsv() {
  Lectura l;
  l.iaq = 123.46f; l.siaq = 110.0f; l.eco2 = 900; l.bvoc = 1.234f;
  l.temp = -5.5f; l.hum = 45.67f; l.pres = 1013.2f; l.gas = 123456; l.precision = 3;
  Muestra m = empaquetar(l, 60, 1);
  assert(m.iaq10 == 1235 && m.temp100 == -550 && m.pres10 == 10132 && m.gas == 123456);

  Lectura extremo; extremo.iaq = 9999; extremo.eco2 = -3;
  Muestra e = empaquetar(extremo, 0, 0);
  assert(e.iaq10 == 5000 && e.eco2 == 0);              // se recorta, no desborda

  char fila[200];
  formatearFila(fila, sizeof fila, m, 0, "");
  assert(strcmp(fila, "60,,123.5,110.0,3,900,1.23,-5.50,45.67,1013.2,123456,1,\n") == 0);
  formatearFila(fila, sizeof fila, m, 1700000000, "ventana");
  assert(strcmp(fila, "60,1700000060,123.5,110.0,3,900,1.23,-5.50,45.67,1013.2,123456,1,ventana\n") == 0);

  int comas = 0;
  for (const char* p = CSV_CABECERA; *p; p++) comas += *p == ',';
  assert(comas == 12);                                  // 13 columnas
}

static void testEtiquetas() {
  char e[24];
  limpiarEtiqueta(e, sizeof e, "cocina, \"horno\"\nabierto");
  assert(strchr(e, ',') == nullptr && strchr(e, '"') == nullptr && strchr(e, '\n') == nullptr);
  limpiarEtiqueta(e, sizeof e, "una etiqueta larguísima que no entra en veinticuatro");
  assert(strlen(e) == 23);

  // Una tilde en el borde de 23 bytes no puede quedar cortada a la mitad (UTF-8 inválido rompe pandas).
  limpiarEtiqueta(e, sizeof e, "ventana abierta cocinaé");  // "é" = 2 bytes: ocupa los bytes 22 y 23
  size_t n = strlen(e);
  assert(n == 22 && (unsigned char)e[n - 1] < 0x80);          // se descarta la "é" entera
  limpiarEtiqueta(e, sizeof e, "ventana abierta cocinéXY");  // "é" completa termina justo en el byte 23
  assert(strcmp(e, "ventana abierta cociné") == 0);
  limpiarEtiqueta(e, sizeof e, "cocinaé");
  assert(strcmp(e, "cocinaé") == 0);                            // si entra completa, se conserva

  Anillo<Marcador, 4> mk;
  Marcador m1 = {125, 1, "cocinando"};
  mk.agregar(m1);
  assert(strcmp(buscarEvento(mk, 120, 180), "cocinando") == 0);
  assert(strcmp(buscarEvento(mk, 180, 240), "") == 0);
}

static void testPromedio() {
  Promedio p;
  assert(p.vacio());
  Lectura a; a.iaq = 100; a.temp = 20; a.precision = 3;
  Lectura b; b.iaq = 200; b.temp = 22; b.precision = 1;
  p.sumar(a); p.sumar(b);
  Lectura r = p.resultado();
  assert(cerca(r.iaq, 150, 1e-3) && cerca(r.temp, 21, 1e-3) && r.precision == 1);
  p.reiniciar();
  assert(p.vacio());
}

int main() {
  testBandas();
  testHisteresis();
  testValidacion();
  testAlarma(1000);
  testAlarma(0xFFFFFFFFu - 60000);  // cruza el desborde de millis() (49,7 días)
  testAlarmaSinCalibrar();
  testBoton();
  testSimular();
  testAnillo();
  testEmpaquetarYCsv();
  testEtiquetas();
  testPromedio();
  puts("OK: todos los tests nativos pasaron");
  return 0;
}
