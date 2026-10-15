// Aire IoT — lógica del dashboard. Sin dependencias salvo uPlot (local).
// Datos: WebSocket /ws (una lectura cada 3 s) + API REST /api/* (ver README).
"use strict";

const $ = (s) => document.querySelector(s);

// Bandas de IAQ de Bosch: nivel 0..6 que manda el equipo.
const BANDAS = [
  ["Excelente", "El aire está excelente."],
  ["Buena", "Buena calidad de aire."],
  ["Levemente contaminado", "Conviene ventilar un rato."],
  ["Moderadamente contaminado", "Ventilá el ambiente."],
  ["Muy contaminado", "Ventilá ya y evitá fuentes de humo o vapores."],
  ["Severamente contaminado", "Aire muy contaminado: abrí puertas y ventanas."],
  ["Extremadamente contaminado", "Salí del ambiente y ventilá a fondo."],
];
const LEDS = ["Verde", "Amarillo", "Rojo"];
const ALARMAS = ["Sin alarma", "Sonando", "Silenciada"];

// Canales secundarios. estado(): "ok" | "reg" | "mal" — referencias orientativas de confort.
const CANALES = [
  { k: "temp", n: "Temperatura", u: "°C", d: 1, estado: (v) => (v >= 18 && v <= 26 ? "ok" : v >= 16 && v <= 28 ? "reg" : "mal") },
  { k: "hum", n: "Humedad", u: "%", d: 0, estado: (v) => (v >= 30 && v <= 60 ? "ok" : v >= 25 && v <= 70 ? "reg" : "mal") },
  { k: "pres", n: "Presión", u: "hPa", d: 1, estado: (v) => (v >= 980 && v <= 1040 ? "ok" : "reg") },
  { k: "eco2", n: "eCO₂*", u: "ppm", d: 0, estado: (v) => (v < 800 ? "ok" : v < 1200 ? "reg" : "mal") },
  { k: "bvoc", n: "bVOC*", u: "ppm", d: 2, estado: (v) => (v < 1 ? "ok" : v < 3 ? "reg" : "mal") },
];
const ESTADOS = { ok: "Bueno", reg: "Regular", mal: "Malo" };

const estado = {
  config: { umbral_amarillo: 100, umbral_rojo: 200, histeresis: 15, repetir_seg: 30, silencio_min: 15 },
  equipo: null,
  desfase: Date.now() / 1000,  // epoch = desfase + t (t = segundos desde el arranque del equipo)
  vivo: { t: [], iaq: [] },
  trazas: Object.fromEntries(CANALES.map((c) => [c.k, []])),
  hist: { t: [], iaq: [] },
  marcadores: [],
  rango: "1h",
  ultimoMensaje: 0,
};

// ---------- utilidades ----------
const guardar = (k, v) => { try { localStorage.setItem(k, v); } catch (_) { /* navegación privada */ } };
const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
const fmt = (v, d) => (v == null || Number.isNaN(v) ? "—" : v.toLocaleString("es-AR", { minimumFractionDigits: d, maximumFractionDigits: d }));
const hora = (epoch) => new Date(epoch * 1000).toLocaleTimeString("es-AR", { hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();

async function api(ruta, opciones) {
  const r = await fetch(ruta, opciones);
  const tipo = r.headers.get("content-type") || "";
  const cuerpo = tipo.includes("json") ? await r.json() : await r.text();
  if (!r.ok) throw new Error((cuerpo && cuerpo.error) || `Error ${r.status}`);
  return cuerpo;
}
const enviar = (ruta, datos) => api(ruta, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(datos || {}) });

// CSV del equipo → columnas. epoch vacío (sin NTP) se calcula con el desfase.
function parsearCsv(texto) {
  const filas = texto.trim().split("\n");
  const cab = filas.shift().split(",");
  const col = (n) => cab.indexOf(n);
  const iU = col("uptime_s"), iE = col("epoch"), iI = col("iaq");
  const t = [], iaq = [];
  for (const f of filas) {
    const v = f.split(",");
    t.push(v[iE] ? Number(v[iE]) : estado.desfase + Number(v[iU]));
    iaq.push(Number(v[iI]));
  }
  return { t, iaq, filas: filas.map((f) => f.split(",")), cab };
}

// ---------- lectura principal ----------
function mostrarLectura(d) {
  estado.ultimaLectura = d;
  const hay = d && d.iaq != null && d.sensor_ok !== false;
  $("#iaq").textContent = hay ? Math.round(d.iaq) : "—";
  const cat = $("#categoria"), consejo = $("#consejo");
  if (d && d.sensor_ok === false) {
    cat.textContent = "Sensor no detectado";
    consejo.textContent = "Revisá el cableado del BME680 (ver README). El equipo reintenta cada 10 s.";
    document.documentElement.style.setProperty("--muestra", "transparent");
  } else if (hay) {
    const [nombre, texto] = BANDAS[d.nivel] || BANDAS[0];
    cat.textContent = nombre;
    consejo.textContent = texto;
    document.documentElement.style.setProperty("--muestra", css(`--b${d.nivel}`));
  }
  const indice = $("#indice");
  indice.hidden = !hay;
  if (hay) indice.style.left = `${Math.min(d.iaq, 500) / 5}%`;

  const acc = d && d.acc != null ? d.acc : null;
  const cal = $("#calibracion");
  cal.dataset.acc = acc ?? "";
  cal.querySelectorAll(".medidor i").forEach((i, n) => i.classList.toggle("lleno", acc != null && n < acc));
  $("#calibracion-texto").textContent = acc == null ? "Sin datos" : acc === 3 ? "Calibrado 3/3" : `Calibrando ${acc}/3`;
  $("#nota-calibracion").hidden = !(hay && acc <= 1);

  if (d && d.led != null) {
    for (let i = 0; i < 3; i++) $(`#led-${i}`).classList.toggle("on", d.led === i);
    $("#led-texto").textContent = LEDS[d.led];
  }
  if (d && d.alarma != null) {
    $("#alarma").dataset.estado = d.alarma;
    $("#alarma-texto").textContent = ALARMAS[d.alarma];
  }
  if (hay) mostrarCanales(d);
}

function construirCanales() {
  $("#canales").innerHTML = CANALES.map((c) => `
    <li class="canal" id="c-${c.k}">
      <span class="n">${c.n}</span>
      <span class="v"><span>—</span><small>${c.u}</small></span>
      <span class="e">Sin datos</span>
      <svg viewBox="0 0 100 24" preserveAspectRatio="none" aria-hidden="true"><polyline points=""/><circle r="1.6" cx="-9" cy="-9"/></svg>
    </li>`).join("");
}

function mostrarCanales(d) {
  for (const c of CANALES) {
    const v = d[c.k];
    const li = $(`#c-${c.k}`);
    li.querySelector(".v span").textContent = fmt(v, c.d);
    const s = c.estado(v);
    const e = li.querySelector(".e");
    e.dataset.s = s;
    e.textContent = ESTADOS[s];
    dibujarTraza(li.querySelector("svg"), estado.trazas[c.k]);
  }
}

// Mini traza de pluma (últimos ~15 min) para cada canal.
function dibujarTraza(svg, datos) {
  const vals = datos.filter((v) => v != null);
  if (vals.length < 2) return;
  const min = Math.min(...vals), max = Math.max(...vals), rango = max - min || 1;
  const paso = 100 / (datos.length - 1);
  const pts = datos.map((v, i) => (v == null ? null : `${(i * paso).toFixed(1)},${(22 - ((v - min) / rango) * 20).toFixed(1)}`)).filter(Boolean);
  svg.querySelector("polyline").setAttribute("points", pts.join(" "));
  const [x, y] = pts[pts.length - 1].split(",");
  const punto = svg.querySelector("circle");
  punto.setAttribute("cx", x);
  punto.setAttribute("cy", y);
}

// ---------- gráficos (uPlot) ----------
let graficoVivo, graficoHist;

// Patrón de trama para las zonas impresas del papel.
function trama(color, densidad, cruzada) {
  const c = document.createElement("canvas");
  const n = 8 * devicePixelRatio;
  c.width = c.height = n;
  const x = c.getContext("2d");
  x.strokeStyle = color;
  x.globalAlpha = 0.35;
  x.lineWidth = devicePixelRatio;
  const linea = (a, b, cc, dd) => { x.beginPath(); x.moveTo(a, b); x.lineTo(cc, dd); x.stroke(); };
  for (let i = -n; i <= n; i += n / densidad) {
    linea(i, n, i + n, 0);
    if (cruzada) linea(i, 0, i + n, n);
  }
  return c;
}

// Zonas de umbral detrás del trazo: verde limpio, amarillo rayado, rojo cuadriculado.
function dibujarZonas(u) {
  const { ctx } = u;
  const { left, top, width, height } = u.bbox;
  const cfg = estado.config;
  const y = (v) => Math.max(top, Math.min(top + height, u.valToPos(v, "y", true)));
  const zonas = [
    [0, cfg.umbral_amarillo, css("--b0"), 0, false],
    [cfg.umbral_amarillo, cfg.umbral_rojo, css("--b2"), 2, false],
    [cfg.umbral_rojo, u.scales.y.max, css("--b4"), 3, true],
  ];
  ctx.save();
  for (const [desde, hasta, color, densidad, cruzada] of zonas) {
    const y1 = y(hasta), y0 = y(desde);
    if (y0 <= y1) continue;
    ctx.globalAlpha = Number(css("--zona-relleno"));
    ctx.fillStyle = color;
    ctx.fillRect(left, y1, width, y0 - y1);
    if (densidad) {
      ctx.globalAlpha = Number(css("--zona-trama")) / 0.35;
      ctx.fillStyle = ctx.createPattern(trama(color, densidad, cruzada), "repeat");
      ctx.fillRect(left, y1, width, y0 - y1);
    }
  }
  // líneas de umbral impresas
  ctx.globalAlpha = 0.8;
  ctx.strokeStyle = css("--impresion");
  ctx.lineWidth = devicePixelRatio;
  ctx.setLineDash([4 * devicePixelRatio, 3 * devicePixelRatio]);
  for (const v of [cfg.umbral_amarillo, cfg.umbral_rojo]) {
    ctx.beginPath();
    ctx.moveTo(left, Math.round(y(v)) + 0.5);
    ctx.lineTo(left + width, Math.round(y(v)) + 0.5);
    ctx.stroke();
  }
  ctx.restore();
}

// Marcas de la pluma de eventos: línea vertical + etiqueta.
function dibujarMarcas(u) {
  const { ctx } = u;
  const { left, top, width, height } = u.bbox;
  const color = css("--pluma-evento");
  const r = devicePixelRatio;
  ctx.save();
  ctx.strokeStyle = ctx.fillStyle = color;
  ctx.lineWidth = 1.5 * r;
  ctx.font = `600 ${11 * r}px system-ui, sans-serif`;
  for (const m of estado.marcadores) {
    const t = estado.desfase + m.t;
    if (t < u.scales.x.min || t > u.scales.x.max) continue;
    const x = Math.round(u.valToPos(t, "x", true));
    ctx.beginPath();
    ctx.moveTo(x, top);
    ctx.lineTo(x, top + height);
    ctx.stroke();
    const texto = m.label;
    const ancho = ctx.measureText(texto).width;
    const tx = x + 4 * r + ancho > left + width ? x - 4 * r - ancho : x + 4 * r;
    ctx.fillText(texto, tx, top + 12 * r);
  }
  ctx.restore();
}

// La pluma: punto de tinta en el último valor y brazo hasta el borde del tambor.
function dibujarPluma(u) {
  const [xs, ys] = u.data;
  let i = ys.length - 1;
  while (i >= 0 && ys[i] == null) i--;
  if (i < 0 || i !== ys.length - 1) return;  // pluma levantada: el último valor es un hueco (sin conexión)
  const { ctx } = u;
  const r = devicePixelRatio;
  const x = u.valToPos(xs[i], "x", true), y = u.valToPos(ys[i], "y", true);
  ctx.save();
  ctx.strokeStyle = css("--impresion");
  ctx.lineWidth = r;
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(u.bbox.left + u.bbox.width, y);
  ctx.stroke();
  ctx.fillStyle = css("--pluma");
  ctx.beginPath();
  ctx.arc(x, y, 4 * r, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function crearGrafico(el, cursor, conPluma) {
  const tinta = css("--tinta-2"), reticula = css("--reticula");
  const alto = () => Math.max(192, Math.min(320, Math.round(el.clientWidth * 0.5)));
  const eje = { stroke: tinta, font: "12px system-ui, sans-serif", grid: { stroke: reticula, width: 1 }, ticks: { stroke: reticula, width: 1, size: 4 } };
  const u = new uPlot({
    width: el.clientWidth,
    height: alto(),
    padding: [16, 12, 0, 0],
    legend: { show: false },
    cursor: { drag: { x: false, y: false }, points: { size: 7, fill: css("--pluma") } },
    scales: {
      x: { time: true },
      y: { range: (_u, _min, max) => [0, Math.max((max || 0) * 1.15, estado.config.umbral_rojo * 1.25)] },
    },
    axes: [
      // solo minutos enteros: con etiquetas HH:MM nunca se repiten
      { ...eje, space: 72, incrs: [60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600], values: (_u, vals) => vals.map(hora) },
      { ...eje, size: 42, space: 36, incrs: [50, 100, 250] },
      // retícula fina del papel (sin rótulos): cada 10 IAQ y una subdivisión de tiempo
      { scale: "x", size: 0, space: 14, incrs: [15, 30, 60, 300, 600, 900, 1800, 3600], values: () => [], ticks: { show: false }, grid: { stroke: css("--reticula-fina"), width: 1 } },
      { scale: "y", side: 3, size: 0, space: 6, incrs: [10, 25, 50], values: () => [], ticks: { show: false }, grid: { stroke: css("--reticula-fina"), width: 1 } },
    ],
    series: [
      { value: (_u, v) => (v == null ? "" : hora(v)) },
      { label: "IAQ", stroke: css("--pluma"), width: 2, spanGaps: false, points: { show: false } },
    ],
    hooks: {
      drawClear: [dibujarZonas],
      draw: conPluma ? [dibujarMarcas, dibujarPluma] : [dibujarMarcas],
      setCursor: [(u) => {
        const i = u.cursor.idx;
        cursor.textContent = i == null || u.data[1][i] == null ? "" : `${hora(u.data[0][i])} · IAQ ${Math.round(u.data[1][i])}`;
      }],
    },
  }, [[], []], el);
  new ResizeObserver(() => {
    if (el.clientWidth && el.clientWidth !== u.width) u.setSize({ width: el.clientWidth, height: alto() });
  }).observe(el);
  return u;
}

function construirGraficos() {
  for (const g of [graficoVivo, graficoHist]) if (g) g.destroy();
  graficoVivo = crearGrafico($("#grafico-vivo"), $("#cursor-vivo"), true);
  graficoHist = crearGrafico($("#grafico-hist"), $("#cursor-hist"), false);
  graficoVivo.setData([estado.vivo.t, estado.vivo.iaq]);
  graficoHist.setData([estado.hist.t, estado.hist.iaq]);
}

// ---------- datos ----------
function recibir(d) {
  estado.ultimoMensaje = Date.now();
  if (d.iaq == null) { mostrarLectura(d); return; }  // latido sin sensor: nada que graficar
  const t = estado.desfase + d.t;
  const v = estado.vivo;
  v.t.push(t);
  v.iaq.push(d.iaq);
  while (v.t.length && v.t[0] < t - 15 * 60) { v.t.shift(); v.iaq.shift(); }
  for (const c of CANALES) {
    const tr = estado.trazas[c.k];
    tr.push(d[c.k]);
    if (tr.length > 300) tr.shift();
  }
  graficoVivo.setData([v.t, v.iaq]);
  mostrarLectura(d);
}

async function cargarEquipo() {
  const d = await api("/api/current");
  estado.equipo = d.equipo;
  estado.desfase = d.equipo.epoch_arranque || Date.now() / 1000 - d.equipo.uptime;
  mostrarEquipo(d.equipo);
  return d;
}

function mostrarEquipo(e) {
  const dias = Math.floor(e.uptime / 86400), h = Math.floor((e.uptime % 86400) / 3600), m = Math.floor((e.uptime % 3600) / 60);
  const valores = {
    modo: e.modo === "AP" ? "Punto de acceso propio (AireIoT)" : e.modo === "STA" ? "Conectado a la red" : "Conectando…",
    ip: e.ip,
    rssi: e.modo === "STA" ? `${e.rssi} dBm` : "—",
    uptime: dias ? `${dias} d ${h} h` : `${h} h ${m} min`,
    heap: `${Math.round(e.heap / 1024)} KB`,
    bsec: e.bsec,
  };
  for (const [k, v] of Object.entries(valores)) $(`#equipo [data-k="${k}"]`).textContent = v;
}

async function cargarVivo() {
  const { t, iaq, filas, cab } = parsearCsv(await api("/api/history?range=live"));
  estado.vivo = { t, iaq };
  for (const c of CANALES) {
    const i = cab.indexOf(c.k);
    estado.trazas[c.k] = filas.map((f) => Number(f[i]));
  }
  graficoVivo.setData([t, iaq]);
}

async function cargarHistorial() {
  const { t, iaq } = parsearCsv(await api(`/api/history?range=${estado.rango}`));
  estado.hist = { t, iaq };
  graficoHist.setData([t, iaq]);
}

async function cargarConfig() {
  estado.config = await api("/api/config");
  const f = $("#form-config");
  for (const [k, v] of Object.entries(estado.config)) if (f.elements[k]) f.elements[k].value = v;
}

async function cargarMarcadores() {
  estado.marcadores = await api("/api/markers");
  const lista = $("#lista-eventos");
  if (!estado.marcadores.length) {
    lista.innerHTML = `<li class="lista-vacia">Todavía no hay eventos marcados.</li>`;
  } else {
    lista.innerHTML = "";
    for (const m of [...estado.marcadores].reverse()) {
      const li = document.createElement("li");
      const hh = hora(estado.desfase + m.t);
      li.innerHTML = `<time></time><input maxlength="23" aria-label="Etiqueta del evento de las ${hh}"><button type="button" class="boton">Renombrar</button>`;
      li.querySelector("time").textContent = hh;
      const input = li.querySelector("input");
      input.value = m.label;
      li.querySelector("button").addEventListener("click", async (e) => {
        e.target.disabled = true;
        try { await enviar("/api/marker", { id: m.id, label: input.value.trim() }); await cargarMarcadores(); }
        catch (err) { input.setCustomValidity(err.message); input.reportValidity(); }
        finally { e.target.disabled = false; }
      });
      input.addEventListener("input", () => input.setCustomValidity(""));
      lista.appendChild(li);
    }
  }
  graficoVivo.redraw();
  graficoHist.redraw();
}

async function cargarTodo() {
  try {
    const actual = await cargarEquipo();
    await Promise.all([cargarConfig(), cargarVivo(), cargarHistorial()]);
    await cargarMarcadores();
    mostrarLectura(actual);
  } catch (e) {
    console.warn("No se pudo cargar el estado inicial:", e);
  }
}

// ---------- conexión en vivo ----------
let espera = 1000;
function conectar() {
  const ws = new WebSocket(`${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws`);
  let perdido = false;
  // Único camino de desconexión: lo usan onclose y el vigía (un par muerto puede no cerrar nunca).
  const perder = () => {
    if (perdido) return;
    perdido = true;
    clearInterval(vigia);
    ws.onclose = ws.onmessage = null;
    try { ws.close(); } catch (_) { /* ya cerrado */ }
    setConectado(false);
    // La pluma se levanta: un hueco en el trazo marca el tiempo sin datos.
    const v = estado.vivo;
    // El hueco va 1 s después del último punto: uPlot exige tiempos crecientes.
    if (v.t.length && v.iaq[v.iaq.length - 1] != null) { v.t.push(v.t[v.t.length - 1] + 1); v.iaq.push(null); graficoVivo.setData([v.t, v.iaq]); }
    setTimeout(conectar, espera);
    espera = Math.min(espera * 2, 30000);
  };
  ws.onopen = () => { espera = 1000; setConectado(true); cargarTodo(); };
  ws.onmessage = (e) => { try { recibir(JSON.parse(e.data)); } catch (err) { console.warn(err); } };
  ws.onclose = perder;
  // Si el Wi-Fi se cae sin cerrar el socket, no llega nada: se da por perdido a los 10 s.
  const vigia = setInterval(() => {
    if (ws.readyState !== WebSocket.CONNECTING && Date.now() - estado.ultimoMensaje > 10000) perder();
  }, 2000);
}

function setConectado(si) {
  $("#aviso-conexion").hidden = si;
  const l = $("#lampara");
  l.classList.toggle("encendida", si);
  l.setAttribute("aria-label", si ? "Conectado" : "Sin conexión");
  if (si) estado.ultimoMensaje = Date.now();
}

// ---------- controles ----------
$("#silenciar").addEventListener("click", async (e) => {
  e.target.disabled = true;
  try { await enviar("/api/silence"); } catch (err) { console.warn(err); }
  finally { e.target.disabled = false; }
});

document.querySelectorAll(".segmentado button").forEach((b) => b.addEventListener("click", () => {
  document.querySelectorAll(".segmentado button").forEach((o) => o.setAttribute("aria-pressed", String(o === b)));
  estado.rango = b.dataset.rango;
  cargarHistorial().catch(console.warn);
}));

$("#form-config").addEventListener("submit", async (e) => {
  e.preventDefault();
  const f = e.target, msg = $("#config-estado");
  const datos = Object.fromEntries([...new FormData(f)].map(([k, v]) => [k, Number(v)]));
  msg.className = "estado-form";
  msg.textContent = "Guardando…";
  try {
    await enviar("/api/config", datos);
    await cargarConfig();
    graficoVivo.redraw(); graficoHist.redraw();
    msg.classList.add("ok");
    msg.textContent = "Umbrales guardados en el equipo.";
  } catch (err) {
    msg.classList.add("error");
    msg.textContent = `No se guardó: ${err.message}.`;
  }
});

$("#form-evento").addEventListener("submit", async (e) => {
  e.preventDefault();
  const input = $("#evento-nuevo");
  try {
    await enviar("/api/marker", { label: input.value.trim() });
    input.value = "";
    await cargarMarcadores();
  } catch (err) { input.setCustomValidity(err.message); input.reportValidity(); }
});
$("#evento-nuevo").addEventListener("input", (e) => e.target.setCustomValidity(""));

// Tema: automático (sistema) → claro → oscuro.
const TEMAS = ["auto", "light", "dark"], NOMBRES_TEMA = { auto: "automático", light: "claro", dark: "oscuro" };
function aplicarTema(t) {
  if (t === "auto") delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = t;
  $("#tema").textContent = `Tema: ${NOMBRES_TEMA[t]}`;
  construirGraficos();
  if (estado.ultimaLectura) mostrarLectura(estado.ultimaLectura);
}
$("#tema").addEventListener("click", () => {
  const actual = document.documentElement.dataset.theme || "auto";
  const sig = TEMAS[(TEMAS.indexOf(actual) + 1) % TEMAS.length];
  guardar("tema", sig);
  aplicarTema(sig);
});
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
  if (!document.documentElement.dataset.theme) construirGraficos();
});

// ---------- arranque ----------
construirCanales();
aplicarTema(TEMAS.includes(leer("tema")) ? leer("tema") : "auto");
conectar();
setInterval(() => cargarEquipo().catch(() => {}), 10000);
setInterval(() => { cargarHistorial().catch(() => {}); cargarMarcadores().catch(() => {}); }, 60000);
