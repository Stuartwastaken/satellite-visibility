/* ============================================================
   Starlink C++ Visualizer — SpaceX theme
   Black / white / red only. Monospace data. Sharp edges.

   This file handles Packet Router and Handoff Scheduler tabs.
   The Constellation (3D Globe) tab is handled by globe.js.
   ============================================================ */

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
   1. PACKET ROUTER
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
   2. HANDOFF SCHEDULER
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
  setupPacket();
  setupHandoff();
});
