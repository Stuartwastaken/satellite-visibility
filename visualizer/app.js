const VIS = window.VIS_DATA || null;
const PACKET = window.PACKET_DATA || null;
const HANDOFF = window.HANDOFF_DATA || null;

const COLORS = {
  background: "#ffffff",
  grid: "#e2e8f0",
  satellite: "#2563eb",
  satelliteHighlight: "#0ea5e9",
  station: "#ef4444",
  edge: "rgba(14, 116, 144, 0.35)",
  edgeLow: "rgba(148, 163, 184, 0.25)",
  priority: {
    0: "#f97316", // real-time
    1: "#2563eb", // streaming
    2: "#9ca3af", // bulk
    3: "#dc2626", // control
  }
};

const PRIORITY_LABELS = {
  0: "Real-time",
  1: "Streaming",
  2: "Bulk",
  3: "Control"
};

function byId(id) {
  return document.getElementById(id);
}

function initTabs() {
  const buttons = document.querySelectorAll(".tab-button");
  const panels = document.querySelectorAll(".tab-panel");

  buttons.forEach((btn) => {
    btn.addEventListener("click", () => {
      buttons.forEach((b) => b.classList.remove("active"));
      panels.forEach((p) => p.classList.remove("active"));
      btn.classList.add("active");
      byId(btn.dataset.tab).classList.add("active");
    });
  });
}

function formatNumber(value, digits = 2) {
  return Number(value).toFixed(digits);
}

// ============================================================
// Visibility Graph
// ============================================================
function setupVisibility() {
  if (!VIS) return;

  const stationSelect = byId("vis-station");
  const minElev = byId("vis-min-elev");
  const minElevVal = byId("vis-min-elev-val");
  const showEdges = byId("vis-show-edges");
  const showSats = byId("vis-show-sats");
  const showStations = byId("vis-show-stations");
  const statsContainer = byId("vis-stats");

  VIS.stations.forEach((station) => {
    const opt = document.createElement("option");
    opt.value = station.id;
    opt.textContent = `${station.name} (ID ${station.id})`;
    stationSelect.appendChild(opt);
  });

  stationSelect.value = VIS.stations[0]?.id ?? 0;
  minElevVal.textContent = `${minElev.value}°`;

  function updateStats() {
    const s = VIS.stats;
    statsContainer.innerHTML = [
      card("Edges", s.edge_count),
      card("Min Elev", `${formatNumber(s.min_elev)}°`),
      card("Avg Elev", `${formatNumber(s.avg_elev)}°`),
      card("Max Elev", `${formatNumber(s.max_elev)}°`),
      card("Avg Latency", `${formatNumber(s.avg_latency)} ms`),
    ].join("");
  }

  function card(label, value) {
    return `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;
  }

  const edgesByStation = new Map();
  VIS.edges.forEach((edge) => {
    if (!edgesByStation.has(edge.station)) {
      edgesByStation.set(edge.station, []);
    }
    edgesByStation.get(edge.station).push(edge);
  });

  function draw() {
    minElevVal.textContent = `${minElev.value}°`;

    const canvas = byId("vis-canvas");
    const ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    drawLatLonGrid(ctx, canvas);

    const stationId = Number(stationSelect.value);
    const edges = edgesByStation.get(stationId) || [];
    const minElevValue = Number(minElev.value);

    if (showEdges.checked) {
      edges.forEach((edge) => {
        if (edge.elev < minElevValue) return;
        const sat = VIS.satellites[edge.sat];
        const station = VIS.stations[edge.station];
        if (!sat || !station) return;
        const satXY = project(sat.lat, sat.lon, canvas);
        const gsXY = project(station.lat, station.lon, canvas);
        ctx.strokeStyle = edge.elev > 35 ? COLORS.edge : COLORS.edgeLow;
        ctx.lineWidth = edge.elev > 50 ? 1.6 : 1.0;
        ctx.beginPath();
        ctx.moveTo(gsXY.x, gsXY.y);
        ctx.lineTo(satXY.x, satXY.y);
        ctx.stroke();
      });
    }

    if (showSats.checked) {
      const connectedSatIds = new Set(edges.map((e) => e.sat));
      VIS.satellites.forEach((sat) => {
        const { x, y } = project(sat.lat, sat.lon, canvas);
        ctx.fillStyle = connectedSatIds.has(sat.id)
          ? COLORS.satelliteHighlight
          : COLORS.satellite;
        ctx.beginPath();
        ctx.arc(x, y, connectedSatIds.has(sat.id) ? 3.2 : 2.0, 0, Math.PI * 2);
        ctx.fill();
      });
    }

    if (showStations.checked) {
      VIS.stations.forEach((station) => {
        const { x, y } = project(station.lat, station.lon, canvas);
        ctx.fillStyle = COLORS.station;
        ctx.beginPath();
        ctx.arc(x, y, station.id === stationId ? 5 : 3.6, 0, Math.PI * 2);
        ctx.fill();
      });
    }
  }

  stationSelect.addEventListener("change", draw);
  minElev.addEventListener("input", draw);
  showEdges.addEventListener("change", draw);
  showSats.addEventListener("change", draw);
  showStations.addEventListener("change", draw);

  updateStats();
  draw();
}

function drawLatLonGrid(ctx, canvas) {
  const width = canvas.width;
  const height = canvas.height;
  ctx.strokeStyle = COLORS.grid;
  ctx.lineWidth = 1;

  for (let lat = -60; lat <= 60; lat += 30) {
    const y = ((90 - lat) / 180) * height;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  for (let lon = -180; lon <= 180; lon += 60) {
    const x = ((lon + 180) / 360) * width;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
}

function project(lat, lon, canvas) {
  const x = ((lon + 180) / 360) * canvas.width;
  const y = ((90 - lat) / 180) * canvas.height;
  return { x, y };
}

// ============================================================
// Packet Router
// ============================================================
function setupPacket() {
  if (!PACKET) return;

  const statsContainer = byId("pkt-stats");

  const checkboxMap = {
    3: byId("pkt-show-control"),
    0: byId("pkt-show-realtime"),
    1: byId("pkt-show-streaming"),
    2: byId("pkt-show-bulk"),
  };

  function updateStats() {
    statsContainer.innerHTML = [
      card("Packets", PACKET.meta.num_packets),
      card("Arrived", PACKET.meta.num_arrived),
      card("Dropped", PACKET.meta.num_dropped),
      card("Reorder P", PACKET.meta.reorder_prob),
      card("Drop P", PACKET.meta.drop_prob),
      card("Queues", PACKET.meta.num_queues),
    ].join("");
  }

  function card(label, value) {
    return `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;
  }

  function draw() {
    const canvas = byId("pkt-canvas");
    const ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    drawPacketAxes(ctx, canvas);

    const maxX = PACKET.meta.num_arrived;
    const maxY = PACKET.meta.num_packets;

    // Diagonal reference
    ctx.strokeStyle = "#94a3b8";
    ctx.setLineDash([6, 6]);
    ctx.beginPath();
    ctx.moveTo(0, canvas.height);
    ctx.lineTo(canvas.width, 0);
    ctx.stroke();
    ctx.setLineDash([]);

    PACKET.points.forEach((p) => {
      if (!checkboxMap[p.priority].checked) return;
      const x = (p.arrival / maxX) * canvas.width;
      const y = canvas.height - (p.seq / maxY) * canvas.height;
      ctx.fillStyle = COLORS.priority[p.priority];
      ctx.beginPath();
      ctx.arc(x, y, 2.5, 0, Math.PI * 2);
      ctx.fill();
    });
  }

  Object.values(checkboxMap).forEach((cb) => cb.addEventListener("change", draw));

  updateStats();
  draw();
}

function drawPacketAxes(ctx, canvas) {
  ctx.strokeStyle = COLORS.grid;
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.rect(0, 0, canvas.width, canvas.height);
  ctx.stroke();
}

// ============================================================
// Handoff Scheduler
// ============================================================
function setupHandoff() {
  if (!HANDOFF) return;

  const statsContainer = byId("handoff-stats");
  statsContainer.innerHTML = [
    card("Min Signal", `${formatNumber(HANDOFF.stats.min_signal)} dB`),
    card("Coverage Time", `${formatNumber(HANDOFF.stats.coverage_time)} s`),
    card("Gap Time", `${formatNumber(HANDOFF.stats.gap_time)} s`),
    card("Handoffs", HANDOFF.stats.num_handoffs),
  ].join("");

  function card(label, value) {
    return `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;
  }

  const canvas = byId("handoff-canvas");
  const ctx = canvas.getContext("2d");

  const satIds = Array.from(new Set(HANDOFF.windows.map((w) => w.sat))).sort((a, b) => a - b);
  const satIndex = new Map(satIds.map((id, idx) => [id, idx]));

  const minTime = Math.min(...HANDOFF.windows.map((w) => w.start));
  const maxTime = Math.max(...HANDOFF.windows.map((w) => w.end));

  const rowHeight = 24;
  const paddingTop = 20;
  canvas.height = Math.max(300, satIds.length * rowHeight + paddingTop + 20);

  function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = COLORS.background;
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    // Grid
    ctx.strokeStyle = COLORS.grid;
    ctx.lineWidth = 1;
    for (let i = 0; i < satIds.length; i++) {
      const y = paddingTop + i * rowHeight;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(canvas.width, y);
      ctx.stroke();
    }

    // Windows
    HANDOFF.windows.forEach((w) => {
      const y = paddingTop + satIndex.get(w.sat) * rowHeight + 4;
      const x1 = ((w.start - minTime) / (maxTime - minTime)) * canvas.width;
      const x2 = ((w.end - minTime) / (maxTime - minTime)) * canvas.width;
      const width = Math.max(2, x2 - x1);
      const strength = Math.min(1, Math.max(0, (w.peak - 6) / 20));
      const color = `rgba(${Math.round(60 + strength * 120)}, ${Math.round(130 + strength * 80)}, 90, 0.75)`;
      ctx.fillStyle = color;
      ctx.fillRect(x1, y, width, rowHeight - 8);
    });

    // Handoff lines
    HANDOFF.handoffs.forEach((h) => {
      const x = ((h.time - minTime) / (maxTime - minTime)) * canvas.width;
      ctx.strokeStyle = "#ef4444";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x, paddingTop - 6);
      ctx.lineTo(x, canvas.height - 8);
      ctx.stroke();
    });

    // Labels
    ctx.fillStyle = "#0f172a";
    ctx.font = "12px sans-serif";
    satIds.forEach((id, idx) => {
      const y = paddingTop + idx * rowHeight + 14;
      ctx.fillText(`Sat ${id}`, 8, y);
    });
  }

  draw();
}

// ============================================================
// Init
// ============================================================
document.addEventListener("DOMContentLoaded", () => {
  initTabs();
  setupVisibility();
  setupPacket();
  setupHandoff();
});
