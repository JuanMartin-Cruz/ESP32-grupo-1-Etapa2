// aire_iot.ino — Monitoreo comparativo de confort multi-nodo (Etapa 2).
// UTN FRCU · Tecnologías para la Automatización. Ver readme_etapa2.md.
#include <Wire.h>
#include "config.h"
#include "control.h"
#include "sensor.h"
#include "storage.h"
#include "actuators.h"
#include "web.h"

Config config;                  // Configuración general
static EstadoControl control;   // Estado comparativo de confort (deltas)

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  storageIniciar();
  if (!storageCargarConfig(config)) Serial.println(F("# Config por defecto cargada"));
  actuadoresIniciar();
  sensorIniciar();
  webIniciar();
  
  Serial.printf("# Nodo de confort iniciado con rol: %s\n",
                NODO_ROL == ROL_AULA ? "AULA (INTERIOR / SERVIDOR)" : "PASILLO (EXTERIOR / REMOTO)");
  Serial.printf("# Buffers en RAM: %u bytes. Heap libre: %u bytes\n",
                (unsigned)(sizeof historial + sizeof vivo + sizeof marcadores), ESP.getFreeHeap());
  Serial.print(CSV_CABECERA);
}

void loop() {
  uint32_t ahora = millis();

  Lectura local;
  if (sensorActualizar(local)) {
    Lectura remoto;
    bool tieneRemoto = webObtenerRemoto(remoto);

#ifdef SIMULATION
    // En modo simulación, generamos automáticamente la contraparte
    if (!tieneRemoto) {
      remoto = simular(esp_timer_get_time() / 1e6, NODO_ROL != ROL_PASILLO);
      tieneRemoto = true;
    }
#endif

    if (NODO_ROL == ROL_PASILLO) {
      // Si somos el pasillo, transmitimos nuestras lecturas al aula
      webEnviarLecturaRemota(local);
    }

    // 1) Calcular deltas de las 5 variables de confort entre Aula y Pasillo
    if (NODO_ROL == ROL_AULA) {
      calcularDeltasConfort(control, local, remoto);
    } else {
      calcularDeltasConfort(control, remoto, local);
    }

    // 2) Historial y registro CSV en monitor serial
    storageAgregar(local, uptimeS());
    char fila[160];
    formatearFila(fila, sizeof fila, empaquetar(local, uptimeS()), epochArranque, "");
    Serial.print(fila);

    // 3) Publicación a través de WebSocket a clientes web
    webPublicar(local, remoto, control);
  }

  // Lectura del pulsador para inserción de marcadores de evento
  Pulsacion p = botonLeer();
  if (p == Pulsacion::LARGA) {
    uint16_t id = storageMarcador(uptimeS(), "evento");
    Serial.printf("# Marcador %u guardado\n", id);
  }

  // Latido en caso de falla de sensor
  static uint32_t ultimoLatido = 0;
  if (!sensorOk() && ahora - ultimoLatido >= 3000) {
    ultimoLatido = ahora;
    webLatidoSinSensor();
  }

  webActualizar();
}
