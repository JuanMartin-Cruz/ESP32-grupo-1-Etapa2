// aire_iot.ino — Monitor de calidad de aire interior (ESP32 + BME680 + BSEC2).
// UTN FRCU · Tecnologías para la Automatización. Ver README.md.
//
// El loop es cooperativo: cada módulo hace un poquito y devuelve el control.
// No hay delay(): los tiempos se manejan con millis().
#include <Wire.h>
#include "config.h"
#include "control.h"
#include "sensor.h"
#include "storage.h"
#include "actuators.h"
#include "web.h"

Config config;                  // umbrales vigentes (se editan desde la web)
static EstadoControl control;   // estado del lazo: LED y alarma

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  storageIniciar();
  if (!storageCargarConfig(config)) Serial.println(F("# Config por defecto"));
  actuadoresIniciar();
  sensorIniciar();
  webIniciar();
  Serial.printf("# Buffers en RAM: %u bytes. Heap libre: %u bytes\n",
                (unsigned)(sizeof historial + sizeof vivo + sizeof marcadores), ESP.getFreeHeap());
  Serial.print(CSV_CABECERA);
}

void loop() {
  uint32_t ahora = millis();
  storageBloquear();
  Config c = config;  // copia: la web puede cambiarla desde otra tarea
  storageDesbloquear();

  Lectura l;
  if (sensorActualizar(l)) {
    // 1) Control: IAQ → LED con histéresis → alarma
    if (controlPaso(control, l.iaq, l.precision, ahora, c)) buzzerAlarma();
    // 2) Historial y salida por Serial (una línea CSV por muestra)
    storageAgregar(l, (uint8_t)control.led, uptimeS());
    char fila[160];
    formatearFila(fila, sizeof fila, empaquetar(l, uptimeS(), (uint8_t)control.led), epochArranque, "");
    Serial.print(fila);
    // 3) Web
    webPublicar(l, control);
  }

  Pulsacion p = botonLeer();
  if (p == Pulsacion::CORTA || webPedidoSilencio()) {
    if (controlSilenciar(control, ahora, c)) Serial.println(F("# Alarma silenciada"));
  }
  if (p == Pulsacion::LARGA) {
    uint16_t id = storageMarcador(uptimeS(), "evento");
    buzzerConfirmar();
    Serial.printf("# Marcador %u guardado\n", id);
  }

  // Sin sensor no hay lecturas que publicar: un latido cada 3 s mantiene viva la conexión
  // de la página y le permite mostrar "Sensor no detectado" en lugar de "Sin conexión".
  static uint32_t ultimoLatido = 0;
  if (!sensorOk() && ahora - ultimoLatido >= 3000) {
    ultimoLatido = ahora;
    webLatidoSinSensor();
  }

  actuadoresActualizar(control.led, !sensorOk());
  webActualizar();
}
