// web.cpp — Wi-Fi, servidor HTTP, WebSocket y sincronización entre Nodo Aula y Pasillo.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <memory>
#include "web.h"
#include "storage.h"
#include "web_assets.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

enum class ModoWifi { CONECTANDO, STA, AP };
static ModoWifi modo = ModoWifi::CONECTANDO;
static uint32_t inicioWifi = 0, ultimaLimpieza = 0, ultimoEnvioRemoto = 0;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static volatile bool pedidoSilencio = false;

static Lectura lecturaLocal;
static Lectura lecturaRemota;
static uint32_t ultimoRemotoMs = 0;
static EstadoControl ultimoEstado;
static bool hayLecturaLocal = false;

static const char* nombreModo() {
  return modo == ModoWifi::STA ? "STA" : modo == ModoWifi::AP ? "AP" : "conectando";
}

static void empaquetarNodoJson(JsonObject obj, const Lectura& l) {
  obj["iaq"] = l.iaq;
  obj["temp"] = l.temp;
  obj["hum"] = l.hum;
  obj["pres"] = l.pres;
  obj["gas"] = l.gas;
  obj["ldr"] = l.ldr;
  obj["sonido"] = l.sonido;
  obj["pulsos"] = l.pulsosSonido;
  obj["acc"] = l.precision;
  obj["conectado"] = l.conectado;
}

static void llenarLecturaCompleta(JsonDocument& d, const Lectura& loc, const Lectura& rem, const EstadoControl& s) {
  d["t"] = uptimeS();
  d["rol"] = (NODO_ROL == ROL_AULA) ? "aula" : "pasillo";
  
  JsonObject objAula = d["aula"].to<JsonObject>();
  JsonObject objPasillo = d["pasillo"].to<JsonObject>();

  if (NODO_ROL == ROL_AULA) {
    empaquetarNodoJson(objAula, loc);
    empaquetarNodoJson(objPasillo, rem);
  } else {
    empaquetarNodoJson(objAula, rem);
    empaquetarNodoJson(objPasillo, loc);
  }

  JsonObject delta = d["delta"].to<JsonObject>();
  delta["iaq"] = s.deltaIaq;
  delta["temp"] = s.deltaTemp;
  delta["hum"] = s.deltaHum;
  delta["ldr"] = s.deltaLdr;
  delta["sonido"] = s.deltaSonido;

  d["sensor_ok"] = sensorOk();
}

// ---------- CSV en streaming ----------
struct Flujo {
  bool enVivo;
  uint32_t seq, fin;
  bool cabecera = false;
  char linea[160];
  size_t len = 0, off = 0;
};

static size_t llenar(Flujo& f, uint8_t* buf, size_t max) {
  size_t escrito = 0;
  while (escrito < max) {
    if (f.off == f.len) {
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
        if (f.seq < primero) f.seq = primero;
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
        break;
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
  if (descarga) res->addHeader("Content-Disposition", "attachment; filename=\"confort_aula_pasillo.csv\"");
  r->send(res);
}

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

  auto* postRemoto = new AsyncCallbackJsonWebHandler("/api/nodo_remoto", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!j.is<JsonObject>()) return enviarError(r, "Payload JSON inválido");
    storageBloquear();
    lecturaRemota.iaq = j["iaq"] | 0.0f;
    lecturaRemota.temp = j["temp"] | 0.0f;
    lecturaRemota.hum = j["hum"] | 0.0f;
    lecturaRemota.pres = j["pres"] | 0.0f;
    lecturaRemota.gas = j["gas"] | 0.0f;
    lecturaRemota.ldr = j["ldr"] | 0.0f;
    lecturaRemota.sonido = j["sonido"] | 0.0f;
    lecturaRemota.pulsosSonido = j["pulsos"] | 0;
    lecturaRemota.precision = j["acc"] | 0;
    lecturaRemota.conectado = true;
    ultimoRemotoMs = millis();
    storageDesbloquear();
    r->send(200, "application/json", "{\"ok\":true}");
  });
  postRemoto->setMethod(HTTP_POST);
  server.addHandler(postRemoto);

  server.on("/api/current", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    storageBloquear();
    Lectura loc = lecturaLocal;
    Lectura rem = lecturaRemota;
    EstadoControl s = ultimoEstado;
    bool hay = hayLecturaLocal;
    storageDesbloquear();

#ifdef SIMULATION
    if (!rem.conectado) {
      rem = simular(esp_timer_get_time() / 1e6, NODO_ROL != ROL_PASILLO);
    }
#endif

    if (hay) llenarLecturaCompleta(d, loc, rem, s);
    d["sensor_ok"] = sensorOk();

    JsonObject eq = d["equipo"].to<JsonObject>();
    eq["rol"] = (NODO_ROL == ROL_AULA) ? "aula" : "pasillo";
    eq["modo"] = nombreModo();
    eq["ip"] = (modo == ModoWifi::AP ? WiFi.softAPIP() : WiFi.localIP()).toString();
    eq["rssi"] = modo == ModoWifi::STA ? WiFi.RSSI() : 0;
    eq["uptime"] = uptimeS();
    eq["heap"] = ESP.getFreeHeap();
    eq["bsec"] = sensorVersion();
    eq["remoto_ok"] = rem.conectado;
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
      o["label"] = String(m.etiqueta);
    }
    storageDesbloquear();
    enviarJson(r, 200, d);
  });

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
  postMarker->setMethod(HTTP_POST);
  server.addHandler(postMarker);

  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "No encontrado"); });
  server.addHandler(&ws);
}

static void iniciarServicios() {
  const char* host = (NODO_ROL == ROL_AULA) ? "aire" : "pasillo";
  if (MDNS.begin(host)) MDNS.addService("http", "tcp", 80);
  IPAddress ip = modo == ModoWifi::AP ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("# Wi-Fi %s. Abrí http://%s o http://%s.local\n", nombreModo(), ip.toString().c_str(), host);
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
      configTime(-3 * 3600, 0, "pool.ntp.org");
      iniciarServicios();
    } else if (ahora - inicioWifi > WIFI_TIMEOUT_MS) {
      WiFi.disconnect(true);
      if (NODO_ROL == ROL_AULA) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("AireIoT_Aula", "aire1234");
        modo = ModoWifi::AP;
        iniciarServicios();
      } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin("AireIoT_Aula", "aire1234");
        inicioWifi = ahora;
      }
    }
  }

  if (lecturaRemota.conectado && (ahora - ultimoRemotoMs > REMOTO_TIMEOUT_MS)) {
    storageBloquear();
    lecturaRemota.conectado = false;
    storageDesbloquear();
  }

  if (modo == ModoWifi::STA && epochArranque == 0) {
    time_t t = time(nullptr);
    if (t > 1700000000) epochArranque = t - uptimeS();
  }

  if (ahora - ultimaLimpieza > 1000) {
    ultimaLimpieza = ahora;
    ws.cleanupClients();
  }
}

void webPublicar(const Lectura& local, const Lectura& remoto, const EstadoControl& s) {
  storageBloquear();
  lecturaLocal = local;
  ultimoEstado = s;
  hayLecturaLocal = true;
  storageDesbloquear();

  if (ws.count() == 0) return;
  JsonDocument d;
  llenarLecturaCompleta(d, local, remoto, s);
  char buf[768];
  serializeJson(d, buf, sizeof buf);
  ws.textAll(buf);
}

bool webObtenerRemoto(Lectura& out) {
  storageBloquear();
  out = lecturaRemota;
  bool ok = lecturaRemota.conectado;
  storageDesbloquear();
  return ok;
}

void webEnviarLecturaRemota(const Lectura& local) {
  uint32_t ahora = millis();
  if (ahora - ultimoEnvioRemoto < REMOTO_SYNC_MS) return;
  ultimoEnvioRemoto = ahora;

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = (WiFi.SSID() == "AireIoT_Aula") ? "http://192.168.4.1/api/nodo_remoto" : "http://aire.local/api/nodo_remoto";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument d;
  empaquetarNodoJson(d.to<JsonObject>(), local);
  String payload;
  serializeJson(d, payload);

  http.POST(payload);
  http.end();
}

void webLatidoSinSensor() {
  if (ws.count() == 0) return;
  char buf[128];
  snprintf(buf, sizeof buf, "{\"t\":%lu,\"sensor_ok\":false}", (unsigned long)uptimeS());
  ws.textAll(buf);
}

bool webPedidoSilencio() {
  if (!pedidoSilencio) return false;
  pedidoSilencio = false;
  return true;
}
