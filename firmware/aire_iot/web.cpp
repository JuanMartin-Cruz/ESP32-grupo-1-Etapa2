// web.cpp — Wi-Fi (estación → AP), mDNS, hora por NTP, servidor HTTP y WebSocket.
// Los handlers corren en la tarea de AsyncTCP, NO en loop(): por eso todo acceso a
// buffers/config va con storageBloquear() y los pedidos al lazo usan banderas.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <memory>
#include "web.h"
#include "storage.h"
#include "web_assets.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Falta secrets.h: copiá secrets.example.h como secrets.h y completá tu Wi-Fi"
#endif

enum class ModoWifi { CONECTANDO, STA, AP };
static ModoWifi modo = ModoWifi::CONECTANDO;
static uint32_t inicioWifi = 0, ultimaLimpieza = 0;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static volatile bool pedidoSilencio = false;
static Lectura ultima;          // última lectura publicada (protegida por el lock)
static EstadoControl ultimoEstado;
static bool hayLectura = false;

static const char* nombreModo() {
  return modo == ModoWifi::STA ? "STA" : modo == ModoWifi::AP ? "AP" : "conectando";
}

// Arma el JSON de una lectura + estado. Lo usan /ws y /api/current.
static void llenarLectura(JsonDocument& d, const Lectura& l, const EstadoControl& s) {
  d["t"] = uptimeS();
  d["iaq"] = l.iaq;  d["siaq"] = l.siaq;  d["acc"] = l.precision;
  d["eco2"] = l.eco2; d["bvoc"] = l.bvoc;
  d["temp"] = l.temp; d["hum"] = l.hum;  d["pres"] = l.pres;  d["gas"] = l.gas;
  d["nivel"] = bandaBosch(l.iaq);
  d["led"] = (uint8_t)s.led;
  d["alarma"] = (uint8_t)s.alarma;
  d["sensor_ok"] = sensorOk();
}

// ---------- CSV en streaming (history y export) ----------
// Se genera fila por fila a medida que el cliente pide bytes: nunca se arma el CSV entero en RAM.
struct Flujo {
  bool enVivo;          // true: buffer de 3 s; false: historial de 1 min
  uint32_t seq, fin;    // rango de números de secuencia a enviar
  bool cabecera = false;
  char linea[160];
  size_t len = 0, off = 0;
};

static size_t llenar(Flujo& f, uint8_t* buf, size_t max) {
  size_t escrito = 0;
  while (escrito < max) {
    if (f.off == f.len) {  // se terminó la línea anterior: preparar la siguiente
      f.off = 0;
      if (!f.cabecera) {
        f.len = strlcpy(f.linea, CSV_CABECERA, sizeof f.linea);
        f.cabecera = true;
      } else if (f.seq < f.fin) {
        Muestra m;
        char evento[sizeof(Marcador::etiqueta)];
        uint32_t paso = f.enVivo ? 3 : 60;
        storageBloquear();
        uint32_t primero = f.enVivo ? vivo.primero() : historial.primero();
        if (f.seq < primero) f.seq = primero;  // se pisó mientras enviábamos: saltear
        bool hay = f.seq < f.fin;
        if (hay) {
          m = f.enVivo ? vivo.en(f.seq) : historial.en(f.seq);
          strlcpy(evento, buscarEvento(marcadores, m.t, m.t + paso), sizeof evento);
        }
        uint32_t epoch = epochArranque;
        storageDesbloquear();
        if (!hay) break;
        f.len = formatearFila(f.linea, sizeof f.linea, m, epoch, evento);
        f.seq++;
      } else {
        break;  // fin del CSV
      }
    }
    size_t n = min(max - escrito, f.len - f.off);
    memcpy(buf + escrito, f.linea + f.off, n);
    f.off += n;
    escrito += n;
  }
  return escrito;
}

static void enviarCsv(AsyncWebServerRequest* r, const String& rango, bool descarga) {
  auto f = std::make_shared<Flujo>();
  f->enVivo = rango == "live";
  uint32_t minutos = rango == "1h" ? 60 : rango == "6h" ? 360 : N_HIST;
  storageBloquear();
  f->fin = f->enVivo ? vivo.total : historial.total;
  uint32_t primero = f->enVivo ? vivo.primero() : historial.primero();
  f->seq = f->enVivo || f->fin - primero <= minutos ? primero : f->fin - minutos;
  storageDesbloquear();
  AsyncWebServerResponse* res = r->beginChunkedResponse(
      "text/csv", [f](uint8_t* buf, size_t max, size_t) -> size_t { return llenar(*f, buf, max); });
  if (descarga) res->addHeader("Content-Disposition", "attachment; filename=\"aire_iot.csv\"");
  r->send(res);
}

// ---------- Rutas ----------
static void enviarJson(AsyncWebServerRequest* r, int codigo, JsonDocument& d) {
  String out;
  serializeJson(d, out);
  r->send(codigo, "application/json", out);
}

static void enviarError(AsyncWebServerRequest* r, const char* mensaje) {
  JsonDocument d;
  d["error"] = mensaje;
  enviarJson(r, 400, d);
}

static void rutas() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    r->send(res);
  });

  server.on("/api/current", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    storageBloquear();
    Lectura l = ultima;
    EstadoControl s = ultimoEstado;
    bool hay = hayLectura;
    storageDesbloquear();
    if (hay) llenarLectura(d, l, s);
    d["sensor_ok"] = sensorOk();  // siempre: sin él la UI no distingue "sin sensor" de "sin datos todavía"
    JsonObject eq = d["equipo"].to<JsonObject>();
    eq["modo"] = nombreModo();
    eq["ip"] = (modo == ModoWifi::AP ? WiFi.softAPIP() : WiFi.localIP()).toString();
    eq["rssi"] = modo == ModoWifi::STA ? WiFi.RSSI() : 0;
    eq["uptime"] = uptimeS();
    eq["heap"] = ESP.getFreeHeap();
    eq["bsec"] = sensorVersion();
    eq["epoch_arranque"] = epochArranque;
    enviarJson(r, 200, d);
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* r) {
    String rango = r->hasParam("range") ? r->getParam("range")->value() : "1h";
    if (rango != "live" && rango != "1h" && rango != "6h" && rango != "24h")
      return enviarError(r, "range debe ser live, 1h, 6h o 24h");
    enviarCsv(r, rango, false);
  });

  server.on("/api/export.csv", HTTP_GET, [](AsyncWebServerRequest* r) { enviarCsv(r, "24h", true); });

  server.on("/api/markers", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    storageBloquear();
    for (uint32_t s = marcadores.primero(); s < marcadores.total; s++) {
      const Marcador& m = marcadores.en(s);
      JsonObject o = a.add<JsonObject>();
      o["id"] = m.id;
      o["t"] = m.t;
      o["label"] = String(m.etiqueta);  // String: copia (el buffer puede cambiar después)
    }
    storageDesbloquear();
    enviarJson(r, 200, d);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    storageBloquear();
    Config c = config;
    storageDesbloquear();
    JsonDocument d;
    d["umbral_amarillo"] = c.umbralAmarillo;
    d["umbral_rojo"] = c.umbralRojo;
    d["histeresis"] = c.histeresis;
    d["repetir_seg"] = c.repetirSeg;
    d["silencio_min"] = c.silencioMin;
    enviarJson(r, 200, d);
  });

  auto* postConfig = new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest* r, JsonVariant& j) {
    storageBloquear();
    Config c = config;
    storageDesbloquear();
    // Campo ausente o de otro tipo → se mantiene el valor actual.
    c.umbralAmarillo = j["umbral_amarillo"] | c.umbralAmarillo;
    c.umbralRojo = j["umbral_rojo"] | c.umbralRojo;
    c.histeresis = j["histeresis"] | c.histeresis;
    c.repetirSeg = j["repetir_seg"] | c.repetirSeg;
    c.silencioMin = j["silencio_min"] | c.silencioMin;
    if (const char* error = validarConfig(c)) return enviarError(r, error);
    storageBloquear();
    config = c;
    storageDesbloquear();
    storageGuardarConfig(c);
    r->send(200, "application/json", "{\"ok\":true}");
  });
  postConfig->setMethod(HTTP_POST);  // por defecto también acepta GET y pisaría otras rutas
  server.addHandler(postConfig);

  auto* postMarker = new AsyncCallbackJsonWebHandler("/api/marker", [](AsyncWebServerRequest* r, JsonVariant& j) {
    const char* label = j["label"] | "";
    if (!*label) return enviarError(r, "Falta la etiqueta (label)");
    JsonDocument d;
    if (j["id"].is<uint16_t>()) {
      if (!storageRenombrar(j["id"].as<uint16_t>(), label)) return enviarError(r, "No existe ese marcador");
      d["id"] = j["id"];
    } else {
      d["id"] = storageMarcador(uptimeS(), label);
    }
    enviarJson(r, 200, d);
  });
  postMarker->setMethod(HTTP_POST);  // por defecto también acepta GET y pisaría otras rutas
  server.addHandler(postMarker);

  server.on("/api/silence", HTTP_POST, [](AsyncWebServerRequest* r) {
    pedidoSilencio = true;  // lo atiende loop() en la próxima vuelta
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "No encontrado"); });
  server.addHandler(&ws);
}

static void iniciarServicios() {
  if (MDNS.begin("aire")) MDNS.addService("http", "tcp", 80);
  IPAddress ip = modo == ModoWifi::AP ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("# Wi-Fi %s. Abrí http://%s o http://aire.local\n", nombreModo(), ip.toString().c_str());
}

void webIniciar() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  inicioWifi = millis();
  rutas();
  server.begin();
}

void webActualizar() {
  uint32_t ahora = millis();
  if (modo == ModoWifi::CONECTANDO) {
    if (WiFi.status() == WL_CONNECTED) {
      modo = ModoWifi::STA;
      configTime(-3 * 3600, 0, "pool.ntp.org");  // Argentina, UTC−3
      iniciarServicios();
    } else if (ahora - inicioWifi > WIFI_TIMEOUT_MS) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, AP_PASS);
      modo = ModoWifi::AP;
      iniciarServicios();
    }
  }
  if (modo == ModoWifi::STA && epochArranque == 0) {
    time_t t = time(nullptr);
    if (t > 1700000000) epochArranque = t - uptimeS();  // hay hora válida por NTP
  }
  if (ahora - ultimaLimpieza > 1000) {
    ultimaLimpieza = ahora;
    ws.cleanupClients();
  }
}

void webPublicar(const Lectura& l, const EstadoControl& s) {
  storageBloquear();
  ultima = l;
  ultimoEstado = s;
  hayLectura = true;
  storageDesbloquear();
  if (ws.count() == 0) return;
  JsonDocument d;
  llenarLectura(d, l, s);
  char buf[384];
  serializeJson(d, buf, sizeof buf);
  ws.textAll(buf);
}

void webLatidoSinSensor() {
  if (ws.count() == 0) return;
  char buf[64];
  snprintf(buf, sizeof buf, "{\"t\":%lu,\"sensor_ok\":false}", (unsigned long)uptimeS());
  ws.textAll(buf);
}

bool webPedidoSilencio() {
  if (!pedidoSilencio) return false;
  pedidoSilencio = false;
  return true;
}
