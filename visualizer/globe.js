/**
 * Starlink Constellation 3D Globe
 * ================================
 * Lightweight Three.js renderer for ~9,636 satellites.
 *
 * Performance principles:
 *  - Screen-space pixel sizing (sizeAttenuation: false)
 *  - No transparency on satellite/ISL geometry
 *  - Visibility edges drawn dynamically per station selection
 *  - Animation pauses when tab hidden
 */

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const GLOBE = window.GLOBE_DATA;
if (!GLOBE) console.warn("GLOBE_DATA not found — run ./visualizer_data first");

/* ============================================================
   Shell colors
   ============================================================ */
const SHELL_COLORS = [
  [1.0, 1.0, 1.0],    // 0: Gen1 Main — white
  [0.8, 0.8, 0.8],    // 1: Gen1 Backup — light gray
  [0.27, 0.53, 1.0],  // 2: Polar — blue
  [0.27, 0.8, 0.27],  // 3: SSO — green
  [0.65, 0.6, 0.5],   // 4: Gen2 — warm gray
];

/* ============================================================
   Helpers
   ============================================================ */
const card = (label, value) =>
  `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;
const fmt = (v, d = 2) => Number(v).toFixed(d);

/* ============================================================
   Scene globals
   ============================================================ */
let scene, camera, renderer, controls;
let satPoints, islLines, stationPoints;
let visLinesObj = null;        // dynamic visibility edges
let selectedStation = -1;      // -1 = none
let animId = null;

// Pre-index visibility edges by station id for fast lookup
let edgesByStation = null;

function indexEdges() {
  if (!GLOBE || !GLOBE.visibility) return;
  edgesByStation = new Map();
  // edges are [satId, stationId, elevDeg, latencyMs]
  for (const e of GLOBE.visibility.edges) {
    const sid = e[1];
    if (!edgesByStation.has(sid)) edgesByStation.set(sid, []);
    edgesByStation.get(sid).push(e);
  }
}

/* ============================================================
   Stats + station dropdown
   ============================================================ */
function populateUI() {
  if (!GLOBE) return;
  indexEdges();
  populateDropdown();
  updateStats();
}

function populateDropdown() {
  const sel = document.getElementById("globe-station");
  if (!sel) return;

  const none = document.createElement("option");
  none.value = "-1";
  none.textContent = "— Select station —";
  sel.appendChild(none);

  GLOBE.stations.forEach(s => {
    const o = document.createElement("option");
    o.value = s.id;
    o.textContent = s.name;
    sel.appendChild(o);
  });
}

function updateStats() {
  const el = document.getElementById("globe-stats");
  if (!el || !GLOBE) return;

  const shells = GLOBE.meta.shells;
  const minAlt = Math.min(...shells.map(s => s.altitude_km));
  const maxAlt = Math.max(...shells.map(s => s.altitude_km));

  const cards = [
    card("Active Satellites", GLOBE.meta.total_satellites.toLocaleString()),
    card("ISL Links", GLOBE.meta.total_isl_links.toLocaleString()),
    card("Orbital Shells", shells.length),
    card("Ground Stations", GLOBE.stations.length),
    card("Altitude Range", `${minAlt} – ${maxAlt} km`),
  ];

  // If a station is selected, show its visibility metrics
  if (selectedStation >= 0 && edgesByStation) {
    const edges = edgesByStation.get(selectedStation) || [];
    const name = GLOBE.stations[selectedStation]?.name || "";
    if (edges.length > 0) {
      let minElev = 90, maxElev = 0, sumLat = 0, minLat = 1e9;
      for (const e of edges) {
        minElev = Math.min(minElev, e[2]);
        maxElev = Math.max(maxElev, e[2]);
        sumLat += e[3];
        minLat = Math.min(minLat, e[3]);
      }
      cards.push(card(`${name} — Visible`, edges.length));
      cards.push(card("Elev Range", `${fmt(minElev, 1)}° – ${fmt(maxElev, 1)}°`));
      cards.push(card("Avg Latency", `${fmt(sumLat / edges.length)} ms`));
      cards.push(card("Min Latency", `${fmt(minLat)} ms`));
    } else {
      cards.push(card(`${name}`, "No visible sats"));
    }
  }

  el.innerHTML = cards.join("");
}

/* ============================================================
   Init
   ============================================================ */
function init() {
  if (!GLOBE) return;
  const box = document.getElementById("globe-container");
  if (!box) return;

  const W = box.clientWidth;
  const H = box.clientHeight || 600;

  scene = new THREE.Scene();

  camera = new THREE.PerspectiveCamera(45, W / H, 0.1, 200);
  camera.position.set(0, 0.8, 3.0);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(W, H);
  renderer.setClearColor(0x000000, 1);
  box.appendChild(renderer.domElement);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableZoom = false;
  controls.enablePan = false;
  controls.autoRotate = true;
  controls.autoRotateSpeed = 0.25;
  controls.enableDamping = true;
  controls.dampingFactor = 0.05;
  controls.minPolarAngle = 0.3;
  controls.maxPolarAngle = Math.PI - 0.3;

  scene.add(new THREE.AmbientLight(0x333344, 2.0));
  const dir = new THREE.DirectionalLight(0xffffff, 0.5);
  dir.position.set(5, 3, 5);
  scene.add(dir);

  buildStarfield();
  buildEarth();
  buildAtmosphere();
  buildSatellites();
  buildISLLinks();
  buildGroundStations();

  window.addEventListener("resize", onResize);
  bindControls();
  observeTab();
}

/* ============================================================
   Starfield
   ============================================================ */
function buildStarfield() {
  const N = 1000;
  const pos = new Float32Array(N * 3);
  for (let i = 0; i < N; i++) {
    const th = Math.random() * Math.PI * 2;
    const ph = Math.acos(2 * Math.random() - 1);
    const r = 50;
    pos[i * 3]     = r * Math.sin(ph) * Math.cos(th);
    pos[i * 3 + 1] = r * Math.sin(ph) * Math.sin(th);
    pos[i * 3 + 2] = r * Math.cos(ph);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  scene.add(new THREE.Points(g, new THREE.PointsMaterial({
    color: 0x888888, size: 1.0, sizeAttenuation: false,
  })));
}

/* ============================================================
   Earth
   ============================================================ */
function buildEarth() {
  const g = new THREE.SphereGeometry(1, 64, 64);
  const m = new THREE.MeshPhongMaterial({
    color: 0x0a0e18, emissive: 0x030508,
    specular: 0x0a0a1a, shininess: 10,
  });
  scene.add(new THREE.Mesh(g, m));
}

/* ============================================================
   Atmosphere — subtle backside glow
   ============================================================ */
function buildAtmosphere() {
  const g = new THREE.SphereGeometry(1.005, 64, 64);
  const m = new THREE.MeshBasicMaterial({
    color: 0x1a3060, transparent: true, opacity: 0.08, side: THREE.BackSide,
  });
  scene.add(new THREE.Mesh(g, m));
}

/* ============================================================
   Satellites — all 9,636 as screen-pixel dots
   ============================================================ */
function buildSatellites() {
  const sats = GLOBE.satellites;
  const N = sats.length;
  const pos = new Float32Array(N * 3);
  const col = new Float32Array(N * 3);

  for (let i = 0; i < N; i++) {
    const s = sats[i];
    pos[i * 3] = s.x; pos[i * 3 + 1] = s.y; pos[i * 3 + 2] = s.z;
    const c = SHELL_COLORS[s.s] || SHELL_COLORS[0];
    col[i * 3] = c[0]; col[i * 3 + 1] = c[1]; col[i * 3 + 2] = c[2];
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  g.setAttribute("color", new THREE.BufferAttribute(col, 3));

  satPoints = new THREE.Points(g, new THREE.PointsMaterial({
    size: 1.2, sizeAttenuation: false, vertexColors: true,
  }));
  scene.add(satPoints);
}

/* ============================================================
   ISL Links — ALL intra-plane links, opaque
   9,636 line segments is trivial for WebGL with opaque material.
   ============================================================ */
function buildISLLinks() {
  const links = GLOBE.isl_links;
  const sats = GLOBE.satellites;
  const pos = new Float32Array(links.length * 6);

  for (let i = 0; i < links.length; i++) {
    const [aId, bId] = links[i];
    const a = sats[aId], b = sats[bId];
    pos[i * 6]     = a.x; pos[i * 6 + 1] = a.y; pos[i * 6 + 2] = a.z;
    pos[i * 6 + 3] = b.x; pos[i * 6 + 4] = b.y; pos[i * 6 + 5] = b.z;
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));

  islLines = new THREE.LineSegments(g, new THREE.LineBasicMaterial({
    color: 0x2a3545,
  }));
  scene.add(islLines);
}

/* ============================================================
   Ground Stations — red dots
   ============================================================ */
function buildGroundStations() {
  const gs = GLOBE.stations;
  const pos = new Float32Array(gs.length * 3);
  for (let i = 0; i < gs.length; i++) {
    pos[i * 3] = gs[i].x; pos[i * 3 + 1] = gs[i].y; pos[i * 3 + 2] = gs[i].z;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));

  stationPoints = new THREE.Points(g, new THREE.PointsMaterial({
    color: 0xcc0000, size: 3.5, sizeAttenuation: false,
  }));
  scene.add(stationPoints);
}

/* ============================================================
   Visibility lines — drawn dynamically per station selection
   ============================================================ */
function rebuildVisibilityLines() {
  // Remove old
  if (visLinesObj) { scene.remove(visLinesObj); visLinesObj.geometry.dispose(); visLinesObj = null; }

  const showVis = document.getElementById("globe-show-visibility");
  if (selectedStation < 0 || !edgesByStation || (showVis && !showVis.checked)) return;

  const edges = edgesByStation.get(selectedStation) || [];
  if (edges.length === 0) return;

  const sats = GLOBE.satellites;
  const gs = GLOBE.stations[selectedStation];
  if (!gs) return;

  const pos = new Float32Array(edges.length * 6);
  for (let i = 0; i < edges.length; i++) {
    const satId = edges[i][0];
    const s = sats[satId];
    pos[i * 6]     = gs.x; pos[i * 6 + 1] = gs.y; pos[i * 6 + 2] = gs.z;
    pos[i * 6 + 3] = s.x;  pos[i * 6 + 4] = s.y;  pos[i * 6 + 5] = s.z;
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));

  visLinesObj = new THREE.LineSegments(g, new THREE.LineBasicMaterial({
    color: 0xcc0000,
  }));
  scene.add(visLinesObj);
}

/* ============================================================
   Controls
   ============================================================ */
function bindControls() {
  const el = (id) => document.getElementById(id);

  const stationSel = el("globe-station");
  const lnk = el("globe-show-links");
  const sta = el("globe-show-stations");
  const vis = el("globe-show-visibility");
  const rot = el("globe-auto-rotate");

  if (stationSel) stationSel.addEventListener("change", () => {
    selectedStation = Number(stationSel.value);
    rebuildVisibilityLines();
    updateStats();
  });

  if (lnk) lnk.addEventListener("change", () => {
    if (islLines) islLines.visible = lnk.checked;
  });
  if (sta) sta.addEventListener("change", () => {
    if (stationPoints) stationPoints.visible = sta.checked;
  });
  if (vis) vis.addEventListener("change", () => {
    rebuildVisibilityLines();
  });
  if (rot) rot.addEventListener("change", () => {
    if (controls) controls.autoRotate = rot.checked;
  });
}

/* ============================================================
   Animation
   ============================================================ */
function animate() {
  animId = requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}
function start() { if (animId === null) animate(); }
function stop()  { if (animId !== null) { cancelAnimationFrame(animId); animId = null; } }

function observeTab() {
  const panel = document.getElementById("constellation");
  if (!panel) return;
  if (panel.classList.contains("active")) start();
  new MutationObserver(() => {
    if (panel.classList.contains("active")) { start(); onResize(); }
    else stop();
  }).observe(panel, { attributes: true, attributeFilter: ["class"] });
}

/* ============================================================
   Resize
   ============================================================ */
function onResize() {
  const box = document.getElementById("globe-container");
  if (!box || !renderer) return;
  const W = box.clientWidth;
  const H = box.clientHeight || 600;
  camera.aspect = W / H;
  camera.updateProjectionMatrix();
  renderer.setSize(W, H);
}

/* ============================================================
   Bootstrap
   ============================================================ */
document.addEventListener("DOMContentLoaded", () => {
  populateUI();
  init();
});
