/* ============================================================
   Starlink C++ Visualizer — SpaceX theme
   Black / white / red only. Monospace data. Sharp edges.
   ============================================================ */

const VIS = window.VIS_DATA || null;
const PACKET = window.PACKET_DATA || null;
const HANDOFF = window.HANDOFF_DATA || null;

const C = {
  bg:         "#0a0a0a",
  grid:       "#1a1a1a",
  gridLabel:  "#444444",
  axis:       "#333333",
  red:        "#cc0000",
  redDim:     "rgba(204,0,0,0.35)",
  white:      "#ffffff",
  whiteDim:   "rgba(255,255,255,0.7)",
  grey:       "#888888",
  greyDim:    "#444444",
  dark:       "#333333",
  transparent:"rgba(0,0,0,0)",
  pktControl: "#cc0000",
  pktRealtime:"#ffffff",
  pktStream:  "#888888",
  pktBulk:    "#444444",
};

const DPR = window.devicePixelRatio || 1;

function byId(id) { return document.getElementById(id); }

function card(label, value) {
  return `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;
}

function fmt(v, d = 2) { return Number(v).toFixed(d); }

/* ---- HiDPI canvas helper ---- */
function hiDpi(canvas) {
  const w = canvas.width;
  const h = canvas.height;
  // Don't set style.width/height — let CSS `width:100%` handle display sizing
  canvas.width  = w * DPR;
  canvas.height = h * DPR;
  const ctx = canvas.getContext("2d");
  ctx.scale(DPR, DPR);
  return { ctx, w, h };
}

/* ---- Tabs ---- */
function initTabs() {
  const buttons = document.querySelectorAll(".tab-button");
  const panels  = document.querySelectorAll(".tab-panel");
  buttons.forEach(btn => {
    btn.addEventListener("click", () => {
      buttons.forEach(b => b.classList.remove("active"));
      panels.forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      byId(btn.dataset.tab).classList.add("active");
    });
  });
}

/* ============================================================
   1. VISIBILITY GRAPH
   ============================================================ */
function setupVisibility() {
  if (!VIS) return;

  const stationSel  = byId("vis-station");
  const minElevIn   = byId("vis-min-elev");
  const minElevSpan = byId("vis-min-elev-val");
  const showEdges   = byId("vis-show-edges");
  const showSats    = byId("vis-show-sats");
  const showStations= byId("vis-show-stations");
  const statsEl     = byId("vis-stats");

  // Populate station dropdown with "All" option
  const allOpt = document.createElement("option");
  allOpt.value = "-1";
  allOpt.textContent = "All stations";
  stationSel.appendChild(allOpt);

  VIS.stations.forEach(s => {
    const o = document.createElement("option");
    o.value = s.id;
    o.textContent = `${s.name}`;
    stationSel.appendChild(o);
  });
  stationSel.value = "0"; // Redmond default

  // Index edges by station
  const edgesByStation = new Map();
  VIS.edges.forEach(e => {
    if (!edgesByStation.has(e.station)) edgesByStation.set(e.station, []);
    edgesByStation.get(e.station).push(e);
  });

  const PAD = { top: 24, right: 20, bottom: 32, left: 44 };

  function getFilteredEdges() {
    const sid = Number(stationSel.value);
    const minE = Number(minElevIn.value);
    let edges;
    if (sid === -1) {
      edges = VIS.edges;
    } else {
      edges = edgesByStation.get(sid) || [];
    }
    return edges.filter(e => e.elev >= minE);
  }

  function updateStats() {
    const edges = getFilteredEdges();
    const n = edges.length;
    if (n === 0) {
      statsEl.innerHTML = [
        card("Visible Edges", "0"),
        card("Min Elevation", "--"),
        card("Avg Elevation", "--"),
        card("Avg Latency", "--"),
        card("Satellites", VIS.satellites.length),
      ].join("");
      return;
    }
    let minE = 90, maxE = 0, sumE = 0, minL = 1e9, sumL = 0;
    edges.forEach(e => {
      minE = Math.min(minE, e.elev);
      maxE = Math.max(maxE, e.elev);
      sumE += e.elev;
      minL = Math.min(minL, e.latency_ms);
      sumL += e.latency_ms;
    });
    statsEl.innerHTML = [
      card("Visible Edges", n),
      card("Min Elevation", `${fmt(minE)}°`),
      card("Max Elevation", `${fmt(maxE)}°`),
      card("Avg Latency", `${fmt(sumL / n)} ms`),
      card("Min Latency", `${fmt(minL)} ms`),
    ].join("");
  }

  function project(lat, lon, w, h) {
    const x = PAD.left + ((lon + 180) / 360) * (w - PAD.left - PAD.right);
    const y = PAD.top  + ((90 - lat) / 180) * (h - PAD.top  - PAD.bottom);
    return { x, y };
  }

  function draw() {
    minElevSpan.textContent = `${minElevIn.value}°`;

    const canvas = byId("vis-canvas");
    const w = 1200, h = 520;
    canvas.width = w; canvas.height = h;
    const { ctx } = hiDpi(canvas);

    // Background
    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, w, h);

    // Grid
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 0.5;
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.fillStyle = C.gridLabel;
    ctx.textAlign = "center";
    ctx.textBaseline = "top";

    for (let lon = -180; lon <= 180; lon += 60) {
      const { x } = project(0, lon, w, h);
      ctx.beginPath(); ctx.moveTo(x, PAD.top); ctx.lineTo(x, h - PAD.bottom); ctx.stroke();
      ctx.fillText(`${lon}°`, x, h - PAD.bottom + 4);
    }

    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let lat = -60; lat <= 60; lat += 30) {
      const { y } = project(lat, 0, w, h);
      ctx.beginPath(); ctx.moveTo(PAD.left, y); ctx.lineTo(w - PAD.right, y); ctx.stroke();
      ctx.fillText(`${lat}°`, PAD.left - 4, y);
    }

    const stationId = Number(stationSel.value);
    const filteredEdges = getFilteredEdges();
    const connectedSatIds = new Set(filteredEdges.map(e => e.sat));

    // Edges
    if (showEdges.checked) {
      filteredEdges.forEach(edge => {
        const sat = VIS.satellites[edge.sat];
        const gs  = VIS.stations[edge.station];
        if (!sat || !gs) return;
        const s = project(sat.lat, sat.lon, w, h);
        const g = project(gs.lat,  gs.lon,  w, h);
        ctx.strokeStyle = C.redDim;
        ctx.lineWidth = edge.elev > 50 ? 1.2 : 0.7;
        ctx.beginPath(); ctx.moveTo(g.x, g.y); ctx.lineTo(s.x, s.y); ctx.stroke();
      });
    }

    // Satellites
    if (showSats.checked) {
      VIS.satellites.forEach(sat => {
        const { x, y } = project(sat.lat, sat.lon, w, h);
        const connected = connectedSatIds.has(sat.id);
        ctx.fillStyle = connected ? C.white : C.dark;
        ctx.beginPath();
        ctx.arc(x, y, connected ? 2.5 : 1.4, 0, Math.PI * 2);
        ctx.fill();
      });
    }

    // Stations
    if (showStations.checked) {
      VIS.stations.forEach(gs => {
        const { x, y } = project(gs.lat, gs.lon, w, h);
        const selected = (stationId === -1 || gs.id === stationId);
        ctx.fillStyle = C.red;
        ctx.beginPath();
        ctx.arc(x, y, selected ? 4.5 : 3, 0, Math.PI * 2);
        ctx.fill();

        if (selected && stationId !== -1) {
          ctx.fillStyle = C.white;
          ctx.font = "bold 10px 'Inter', sans-serif";
          ctx.textAlign = "left";
          ctx.textBaseline = "bottom";
          ctx.fillText(gs.name, x + 7, y - 2);
        }
      });
    }

    updateStats();
  }

  [stationSel, minElevIn, showEdges, showSats, showStations].forEach(el => {
    el.addEventListener(el.tagName === "SELECT" ? "change" : "input", draw);
  });

  draw();
}

/* ============================================================
   2. PACKET ROUTER
   ============================================================ */
function setupPacket() {
  if (!PACKET) return;

  const statsEl = byId("pkt-stats");
  const cbs = {
    3: byId("pkt-show-control"),
    0: byId("pkt-show-realtime"),
    1: byId("pkt-show-streaming"),
    2: byId("pkt-show-bulk"),
  };

  const PAD = { top: 20, right: 20, bottom: 44, left: 56 };

  function updateStats() {
    const m = PACKET.meta;
    const lossRate = ((m.num_dropped / m.num_packets) * 100).toFixed(1);
    statsEl.innerHTML = [
      card("Packets Sent", m.num_packets),
      card("Arrived", m.num_arrived),
      card("Dropped", `${m.num_dropped} (${lossRate}%)`),
      card("Reorder Prob", `${(m.reorder_prob * 100).toFixed(0)}%`),
      card("Output Queues", m.num_queues),
    ].join("");
  }

  function draw() {
    const canvas = byId("pkt-canvas");
    const w = 1200, h = 520;
    canvas.width = w; canvas.height = h;
    const { ctx } = hiDpi(canvas);

    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, w, h);

    const plotW = w - PAD.left - PAD.right;
    const plotH = h - PAD.top  - PAD.bottom;
    const maxX  = PACKET.meta.num_arrived;
    const maxY  = PACKET.meta.num_packets;

    // Grid + axis labels
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 0.5;
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.fillStyle = C.gridLabel;

    // X axis ticks
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    for (let i = 0; i <= 4; i++) {
      const val = Math.round((maxX / 4) * i);
      const x = PAD.left + (val / maxX) * plotW;
      ctx.beginPath(); ctx.moveTo(x, PAD.top); ctx.lineTo(x, PAD.top + plotH); ctx.stroke();
      ctx.fillText(val, x, PAD.top + plotH + 4);
    }

    // Y axis ticks
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    for (let i = 0; i <= 4; i++) {
      const val = Math.round((maxY / 4) * i);
      const y = PAD.top + plotH - (val / maxY) * plotH;
      ctx.beginPath(); ctx.moveTo(PAD.left, y); ctx.lineTo(PAD.left + plotW, y); ctx.stroke();
      ctx.fillText(val, PAD.left - 6, y);
    }

    // Axis titles
    ctx.fillStyle = C.grey;
    ctx.font = "11px 'Inter', sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillText("ARRIVAL ORDER", PAD.left + plotW / 2, h - 14);

    ctx.save();
    ctx.translate(12, PAD.top + plotH / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText("SEQUENCE NUMBER", 0, 0);
    ctx.restore();

    // Diagonal reference (in-order line)
    ctx.strokeStyle = C.dark;
    ctx.setLineDash([4, 4]);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(PAD.left, PAD.top + plotH);
    ctx.lineTo(PAD.left + plotW, PAD.top);
    ctx.stroke();
    ctx.setLineDash([]);

    // Plot border
    ctx.strokeStyle = C.axis;
    ctx.lineWidth = 1;
    ctx.strokeRect(PAD.left, PAD.top, plotW, plotH);

    // Points (draw in priority order: bulk first, control last so important ones are on top)
    const order = [2, 1, 0, 3]; // bulk, streaming, realtime, control
    const colorMap = {
      0: C.pktRealtime,
      1: C.pktStream,
      2: C.pktBulk,
      3: C.pktControl,
    };

    order.forEach(pri => {
      if (!cbs[pri].checked) return;
      ctx.fillStyle = colorMap[pri];
      PACKET.points.forEach(p => {
        if (p.priority !== pri) return;
        const x = PAD.left + (p.arrival / maxX) * plotW;
        const y = PAD.top  + plotH - (p.seq / maxY) * plotH;
        ctx.beginPath();
        ctx.arc(x, y, pri === 3 ? 3 : 2.2, 0, Math.PI * 2);
        ctx.fill();
      });
    });

    // Dropped packets — mark on the Y axis as small red ticks
    ctx.strokeStyle = C.red;
    ctx.lineWidth = 1.5;
    PACKET.gaps.forEach(seq => {
      const y = PAD.top + plotH - (seq / maxY) * plotH;
      ctx.beginPath();
      ctx.moveTo(PAD.left - 4, y);
      ctx.lineTo(PAD.left, y);
      ctx.stroke();
    });
  }

  Object.values(cbs).forEach(cb => cb.addEventListener("change", draw));
  updateStats();
  draw();
}

/* ============================================================
   3. HANDOFF SCHEDULER
   ============================================================ */
function setupHandoff() {
  if (!HANDOFF) return;

  const statsEl = byId("handoff-stats");
  const selectedSet = new Set(HANDOFF.selected || []);
  const totalTimeline = HANDOFF.windows.length > 0
    ? HANDOFF.windows[HANDOFF.windows.length - 1].end - HANDOFF.windows[0].start
    : 0;

  const coveragePct = totalTimeline > 0
    ? ((HANDOFF.stats.coverage_time / totalTimeline) * 100).toFixed(1)
    : "0";

  statsEl.innerHTML = [
    card("Handoffs", HANDOFF.stats.num_handoffs),
    card("Min Signal", `${fmt(HANDOFF.stats.min_signal)} dB`),
    card("Coverage", `${fmt(HANDOFF.stats.coverage_time, 0)}s (${coveragePct}%)`),
    card("Gap Time", `${fmt(HANDOFF.stats.gap_time, 0)}s`),
    card("Windows", HANDOFF.windows.length),
  ].join("");

  const satIds = Array.from(new Set(HANDOFF.windows.map(w => w.sat))).sort((a, b) => a - b);
  const satIdx = new Map(satIds.map((id, i) => [id, i]));

  const minTime = Math.min(...HANDOFF.windows.map(w => w.start));
  const maxTime = Math.max(...HANDOFF.windows.map(w => w.end));
  const timeSpan = maxTime - minTime;

  const ROW_H = 32;
  const PAD = { top: 24, right: 50, bottom: 44, left: 56 };

  function draw() {
    const canvas = byId("handoff-canvas");
    const w = 1400;
    const h = Math.max(400, satIds.length * ROW_H + PAD.top + PAD.bottom + 20);
    canvas.width = w; canvas.height = h;
    const { ctx } = hiDpi(canvas);

    const plotW = w - PAD.left - PAD.right;
    const plotH = h - PAD.top - PAD.bottom;

    ctx.fillStyle = C.bg;
    ctx.fillRect(0, 0, w, h);

    // Time axis grid
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 0.5;
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.fillStyle = C.gridLabel;
    ctx.textAlign = "center";
    ctx.textBaseline = "top";

    const timeStep = timeSpan > 3000 ? 600 : timeSpan > 1000 ? 300 : 60;
    for (let t = Math.ceil(minTime / timeStep) * timeStep; t <= maxTime; t += timeStep) {
      const x = PAD.left + ((t - minTime) / timeSpan) * plotW;
      ctx.beginPath(); ctx.moveTo(x, PAD.top); ctx.lineTo(x, PAD.top + plotH); ctx.stroke();
      const label = t >= 60 ? `${(t / 60).toFixed(0)}m` : `${t.toFixed(0)}s`;
      ctx.fillText(label, x, PAD.top + plotH + 4);
    }

    // Satellite labels
    ctx.fillStyle = C.gridLabel;
    ctx.font = "10px 'JetBrains Mono', monospace";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";
    satIds.forEach((id, i) => {
      const y = PAD.top + i * ROW_H + ROW_H / 2;
      ctx.fillText(`SAT ${id}`, PAD.left - 6, y);
    });

    // Row separators
    ctx.strokeStyle = C.grid;
    ctx.lineWidth = 0.5;
    for (let i = 0; i <= satIds.length; i++) {
      const y = PAD.top + i * ROW_H;
      ctx.beginPath(); ctx.moveTo(PAD.left, y); ctx.lineTo(PAD.left + plotW, y); ctx.stroke();
    }

    // Windows with signal strength curve
    HANDOFF.windows.forEach(win => {
      const row = satIdx.get(win.sat);
      const y0 = PAD.top + row * ROW_H + 4;
      const barH = ROW_H - 8;
      const x1 = PAD.left + ((win.start - minTime) / timeSpan) * plotW;
      const x2 = PAD.left + ((win.end   - minTime) / timeSpan) * plotW;
      const barW = Math.max(2, x2 - x1);
      const inChain = selectedSet.has(win.sat);

      // Background bar
      ctx.fillStyle = inChain ? "rgba(255,255,255,0.08)" : "rgba(255,255,255,0.03)";
      ctx.fillRect(x1, y0, barW, barH);

      // Signal strength curve (parabolic)
      const steps = Math.max(4, Math.floor(barW / 2));
      ctx.beginPath();
      for (let s = 0; s <= steps; s++) {
        const frac = s / steps;
        const t = win.start + frac * (win.end - win.start);
        // Parabolic model: peak at center, 0.7*peak at edges
        const mid = (win.start + win.end) / 2;
        const halfDur = (win.end - win.start) / 2;
        const norm = halfDur > 0 ? (t - mid) / halfDur : 0;
        const signal = win.peak * (1.0 - 0.3 * norm * norm);
        // Map signal to bar height (0 dB = bottom, 30 dB = top)
        const signalNorm = Math.min(1, Math.max(0, signal / 30));
        const px = x1 + frac * barW;
        const py = y0 + barH - signalNorm * barH;
        if (s === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
      }
      // Close the shape
      ctx.lineTo(x2, y0 + barH);
      ctx.lineTo(x1, y0 + barH);
      ctx.closePath();
      ctx.fillStyle = inChain ? "rgba(255,255,255,0.25)" : "rgba(255,255,255,0.08)";
      ctx.fill();

      // Top stroke of the curve
      ctx.beginPath();
      for (let s = 0; s <= steps; s++) {
        const frac = s / steps;
        const t = win.start + frac * (win.end - win.start);
        const mid = (win.start + win.end) / 2;
        const halfDur = (win.end - win.start) / 2;
        const norm = halfDur > 0 ? (t - mid) / halfDur : 0;
        const signal = win.peak * (1.0 - 0.3 * norm * norm);
        const signalNorm = Math.min(1, Math.max(0, signal / 30));
        const px = x1 + frac * barW;
        const py = y0 + barH - signalNorm * barH;
        if (s === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
      }
      ctx.strokeStyle = inChain ? C.white : C.grey;
      ctx.lineWidth = inChain ? 1.2 : 0.6;
      ctx.stroke();

      // Peak dB label
      if (barW > 40) {
        ctx.fillStyle = inChain ? C.white : C.grey;
        ctx.font = "9px 'JetBrains Mono', monospace";
        ctx.textAlign = "center";
        ctx.textBaseline = "bottom";
        const peakX = (x1 + x2) / 2;
        const peakNorm = Math.min(1, Math.max(0, win.peak / 30));
        const peakY = y0 + barH - peakNorm * barH;
        ctx.fillText(`${fmt(win.peak, 1)} dB`, peakX, peakY - 2);
      }
    });

    // Handoff lines
    HANDOFF.handoffs.forEach(ho => {
      const x = PAD.left + ((ho.time - minTime) / timeSpan) * plotW;
      ctx.strokeStyle = C.red;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x, PAD.top);
      ctx.lineTo(x, PAD.top + plotH);
      ctx.stroke();

      // Handoff signal label
      ctx.fillStyle = C.red;
      ctx.font = "bold 10px 'JetBrains Mono', monospace";
      ctx.textAlign = "center";
      ctx.textBaseline = "bottom";
      ctx.fillText(`${fmt(ho.signal, 1)} dB`, x, PAD.top - 4);
    });

    // Time axis title
    ctx.fillStyle = C.grey;
    ctx.font = "11px 'Inter', sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "top";
    ctx.fillText("TIME", PAD.left + plotW / 2, h - 14);
  }

  draw();
}

/* ============================================================
   INIT
   ============================================================ */
document.addEventListener("DOMContentLoaded", () => {
  initTabs();
  setupVisibility();
  setupPacket();
  setupHandoff();
});
