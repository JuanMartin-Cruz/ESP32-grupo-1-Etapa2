#!/usr/bin/env python3
"""Servidor de prueba del dashboard, sin ESP32.

Sirve web/ y emula /ws y /api/* con el MISMO generador sintético que el firmware
(firmware/aire_iot/sensor.h, función simular). La lógica de LEDs y alarma es una copia
simplificada de control.cpp, solo para que la UI muestre estados reales.

Uso:
  python3 tools/mock_server.py                # http://localhost:8080
  python3 tools/mock_server.py --rapido       # el tiempo corre 10× más rápido
  python3 tools/mock_server.py --selftest     # verifica el generador contra los valores del test C++
"""
import argparse
import asyncio
import json
import math
import pathlib
import time
from collections import deque

from aiohttp import web

WEB = pathlib.Path(__file__).resolve().parent.parent / "web"
CABECERA = "uptime_s,epoch,iaq,siaq,acc,eco2,bvoc,temp,hum,pres,gas,led,evento"


def simular(t: float) -> dict:
    """Copia exacta de simular() en sensor.h. Si cambiás una, cambiá la otra."""
    p = math.fmod(t, 1200.0)
    pico = 0 if p < 300 else 180 * (p - 300) / 120 if p < 420 else 180 * math.exp(-(p - 420) / 120)
    iaq = 60 + 15 * math.sin(2 * math.pi * t / 3600) + pico + 3 * math.sin(1.7 * t) * math.sin(0.31 * t)
    iaq = min(max(iaq, 0), 500)
    return {
        "iaq": iaq, "siaq": iaq * 0.95, "eco2": 400 + 5 * max(iaq - 25, 0), "bvoc": 0.5 * math.exp(iaq / 100),
        "temp": 23 + 1.5 * math.sin(2 * math.pi * t / 7200),
        "hum": 45 + 5 * math.sin(2 * math.pi * t / 5400) + 0.03 * pico,
        "pres": 1013 + 0.5 * math.sin(2 * math.pi * t / 10800),
        "gas": 150000 * math.exp(-iaq / 150),
        "acc": 0 if t < 120 else 1 if t < 300 else 2 if t < 600 else 3,
    }


def banda(iaq: float) -> int:
    return sum(iaq > lim for lim in (50, 100, 150, 200, 250, 350))


def siguiente_led(led: int, iaq: float, c: dict) -> int:
    """Copia de siguienteLed() en control.cpp (0 verde, 1 amarillo, 2 rojo)."""
    if iaq > c["umbral_rojo"]:
        return 2
    if led == 2 and iaq >= c["umbral_rojo"] - c["histeresis"]:
        return 2
    if iaq > c["umbral_amarillo"]:
        return 1
    if led != 0 and iaq >= c["umbral_amarillo"] - c["histeresis"]:
        return 1
    return 0


class Equipo:
    def __init__(self, velocidad: float):
        self.velocidad = velocidad
        self.sin_sensor = False
        self.inicio = time.monotonic()
        self.epoch_arranque = int(time.time())
        self.config = {"umbral_amarillo": 100, "umbral_rojo": 200, "histeresis": 15,
                       "repetir_seg": 30, "silencio_min": 15}
        self.led, self.alarma, self.fin_silencio = 0, 0, 0.0
        self.vivo = deque(maxlen=300)
        self.historial = deque(maxlen=1440)
        self.marcadores = deque(maxlen=64)
        self.proximo_id = 1
        self.ultima = None
        self.clientes = set()

    def ahora(self) -> float:
        return (time.monotonic() - self.inicio) * self.velocidad

    def paso(self) -> dict:
        t = self.ahora()
        if self.sin_sensor:  # igual que el firmware con el BME680 desconectado
            return {"t": int(t), "sensor_ok": False}
        l = simular(t)
        self.led = siguiente_led(self.led, l["iaq"], self.config)
        if self.led != 2:
            self.alarma = 0
        elif self.alarma == 0 or (self.alarma == 2 and t >= self.fin_silencio):
            self.alarma = 1
        fila = {"t": int(t), **l, "nivel": banda(l["iaq"]), "led": self.led, "alarma": self.alarma,
                "sensor_ok": True}
        self.vivo.append(fila)
        if not self.historial or int(t) - self.historial[-1]["t"] >= 60:
            self.historial.append(fila)
        self.ultima = fila
        return fila

    def csv(self, filas, paso: int) -> str:
        lineas = [CABECERA]
        for f in filas:
            ev = next((m["label"] for m in self.marcadores if f["t"] <= m["t"] < f["t"] + paso), "")
            lineas.append(f'{f["t"]},{self.epoch_arranque + f["t"]},{f["iaq"]:.1f},{f["siaq"]:.1f},{f["acc"]},'
                          f'{round(f["eco2"])},{f["bvoc"]:.2f},{f["temp"]:.2f},{f["hum"]:.2f},{f["pres"]:.1f},'
                          f'{round(f["gas"])},{f["led"]},{ev}')
        return "\n".join(lineas) + "\n"


def app(eq: Equipo) -> web.Application:
    rutas = web.RouteTableDef()

    @rutas.get("/")
    async def index(_):
        return web.FileResponse(WEB / "index.html")

    @rutas.get("/ws")
    async def ws(request):
        sock = web.WebSocketResponse()
        await sock.prepare(request)
        eq.clientes.add(sock)
        try:
            async for _ in sock:
                pass
        finally:
            eq.clientes.discard(sock)
        return sock

    @rutas.get("/api/current")
    async def current(_):
        return web.json_response({**(eq.ultima or {}), "sensor_ok": not eq.sin_sensor, "equipo": {
            "modo": "STA", "ip": "127.0.0.1", "rssi": -58, "uptime": int(eq.ahora()),
            "heap": 180000, "bsec": "simulación (mock)", "epoch_arranque": eq.epoch_arranque}})

    @rutas.get("/api/history")
    async def history(request):
        rango = request.query.get("range", "1h")
        if rango == "live":
            return web.Response(text=eq.csv(eq.vivo, 3), content_type="text/csv")
        minutos = {"1h": 60, "6h": 360, "24h": 1440}.get(rango)
        if not minutos:
            return web.json_response({"error": "range debe ser live, 1h, 6h o 24h"}, status=400)
        return web.Response(text=eq.csv(list(eq.historial)[-minutos:], 60), content_type="text/csv")

    @rutas.get("/api/export.csv")
    async def export(_):
        return web.Response(text=eq.csv(eq.historial, 60), content_type="text/csv",
                            headers={"Content-Disposition": 'attachment; filename="aire_iot.csv"'})

    @rutas.get("/api/markers")
    async def markers(_):
        return web.json_response(list(eq.marcadores))

    @rutas.post("/api/marker")
    async def marker(request):
        d = await request.json()
        label = str(d.get("label", "")).replace(",", " ").replace('"', " ").replace("\n", " ")[:23]
        if not label:
            return web.json_response({"error": "Falta la etiqueta (label)"}, status=400)
        if "id" in d:
            m = next((m for m in eq.marcadores if m["id"] == d["id"]), None)
            if not m:
                return web.json_response({"error": "No existe ese marcador"}, status=400)
            m["label"] = label
            return web.json_response({"id": m["id"]})
        eq.marcadores.append({"id": eq.proximo_id, "t": int(eq.ahora()), "label": label})
        eq.proximo_id += 1
        return web.json_response({"id": eq.proximo_id - 1})

    @rutas.get("/api/config")
    async def get_config(_):
        return web.json_response(eq.config)

    @rutas.post("/api/config")
    async def post_config(request):
        c = {**eq.config, **{k: v for k, v in (await request.json()).items() if k in eq.config}}
        ok = (0 < c["umbral_amarillo"] < c["umbral_rojo"] <= 500 and 0 <= c["histeresis"] < c["umbral_amarillo"]
              and 5 <= c["repetir_seg"] <= 600 and 1 <= c["silencio_min"] <= 120)
        if not ok:
            return web.json_response({"error": "Valores fuera de rango"}, status=400)
        eq.config = c
        return web.json_response({"ok": True})

    @rutas.post("/api/silence")
    async def silence(_):
        if eq.alarma:
            eq.alarma, eq.fin_silencio = 2, eq.ahora() + eq.config["silencio_min"] * 60
        return web.json_response({"ok": True})

    async def muestreo(_app):
        async def bucle():
            while True:
                fila = eq.paso()
                msg = json.dumps(fila)
                for c in list(eq.clientes):
                    try:
                        await c.send_str(msg)
                    except ConnectionError:  # el navegador se fue a mitad de envío
                        eq.clientes.discard(c)
                await asyncio.sleep(3 / eq.velocidad)
        tarea = asyncio.create_task(bucle())
        yield
        tarea.cancel()

    a = web.Application()
    a.add_routes(rutas)
    a.router.add_static("/", WEB)  # style.css, app.js, uplot.*
    a.cleanup_ctx.append(muestreo)
    return a


def precargar(eq: Equipo):
    """Llena 24 h de historial hacia atrás, así los gráficos tienen datos al abrir."""
    for m in range(1440, 0, -1):
        t = -m * 60
        l = {**simular(t + 86400), "acc": 3}  # el historial previo ya está calibrado
        eq.historial.append({"t": t, **l, "nivel": banda(l["iaq"]), "led": siguiente_led(0, l["iaq"], eq.config),
                             "alarma": 0, "sensor_ok": True})


def selftest():
    # Mismos valores de referencia que testSimular() en tools/test_nativo.cpp
    casos = {0: (60.0, 575.0), 360: (157.101, 1060.505), 450: (207.926, 1314.63), 4000: (222.044, 1385.22)}
    for t, (iaq, eco2) in casos.items():
        l = simular(t)
        assert abs(l["iaq"] - iaq) < 0.01 and abs(l["eco2"] - eco2) < 0.05, (t, l)
    c = {"umbral_amarillo": 100, "umbral_rojo": 200, "histeresis": 15}
    assert siguiente_led(1, 85, c) == 1 and siguiente_led(1, 84.9, c) == 0 and siguiente_led(2, 184, c) == 1
    print("OK: mock coincide con el firmware")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--rapido", action="store_true", help="tiempo 10× más rápido")
    ap.add_argument("--puerto", type=int, default=8080)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--sin-sensor", action="store_true", help="emula el BME680 desconectado")
    args = ap.parse_args()
    if args.selftest:
        selftest()
    else:
        equipo = Equipo(10 if args.rapido else 1)
        equipo.sin_sensor = args.sin_sensor
        precargar(equipo)
        web.run_app(app(equipo), port=args.puerto)
