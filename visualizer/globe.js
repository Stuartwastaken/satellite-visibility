/**
 * Starlink Constellation 3D Globe
 * ================================
 * Lightweight Three.js renderer for ~9,636 satellites.
 *
 * Performance principles (modeled after satellitemap.space):
 *  - Screen-space pixel sizing for points (sizeAttenuation: false)
 *  - No transparency on satellite points (avoids alpha sort/overdraw)
 *  - ISL links: sampled subset with opaque thin lines
 *  - Minimal scene: 5 draw calls total
 *  - No custom shaders — standard materials only
 */

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const GLOBE = window.GLOBE_DATA;
if (!GLOBE) console.warn("GLOBE_DATA not found — run ./visualizer_data first");

/* ============================================================
   Shell color palette
   ============================================================ */
const SHELL_COLORS = [
  [1.0, 1.0, 1.0],   // 0: Gen1 Main — white
  [0.8, 0.8, 0.8],   // 1: Gen1 Backup — light gray
  [0.27, 0.53, 1.0],  // 2: Polar — blue
  [0.27, 0.8, 0.27],  // 3: SSO — green
  [0.65, 0.6, 0.5],   // 4: Gen2 — warm gray
];

/* ============================================================
   Stats panel
   ============================================================ */
function populateStats() {
  const el = document.getElementById("globe-stats");
  if (!el || !GLOBE) return;

  const c = (label, value) =>
    `<div class="stat-card"><h4>${label}</h4><div class="value">${value}</div></div>`;

  const shells = GLOBE.meta.shells;
  const minAlt = Math.min(...shells.map((s) => s.altitude_km));
  const maxAlt = Math.max(...shells.map((s) => s.altitude_km));

  el.innerHTML = [
    c("Active Satellites", GLOBE.meta.total_satellites.toLocaleString()),
    c("ISL Links", GLOBE.meta.total_isl_links.toLocaleString()),
    c("Orbital Shells", shells.length),
    c("Ground Stations", GLOBE.stations.length),
    c("Altitude Range", `${minAlt} – ${maxAlt} km`),
  ].join("");
}

/* ============================================================
   Scene globals
   ============================================================ */
let scene, camera, renderer, controls;
let satPoints, islLines, stationPoints;
let animId = null;

function init() {
  if (!GLOBE) return;
  const box = document.getElementById("globe-container");
  if (!box) return;

  const W = box.clientWidth;
  const H = box.clientHeight || 600;

  /* ---- Scene ---- */
  scene = new THREE.Scene();

  /* ---- Camera ---- */
  camera = new THREE.PerspectiveCamera(45, W / H, 0.1, 200);
  camera.position.set(0, 0.8, 3.0);

  /* ---- Renderer ---- */
  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2)); // cap at 2x
  renderer.setSize(W, H);
  renderer.setClearColor(0x000000, 1);
  box.appendChild(renderer.domElement);

  /* ---- Controls ---- */
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableZoom = false;
  controls.enablePan = false;
  controls.autoRotate = true;
  controls.autoRotateSpeed = 0.25;
  controls.enableDamping = true;
  controls.dampingFactor = 0.05;
  controls.minPolarAngle = 0.3;
  controls.maxPolarAngle = Math.PI - 0.3;

  /* ---- Lighting (minimal) ---- */
  scene.add(new THREE.AmbientLight(0x333344, 2.0));
  const dir = new THREE.DirectionalLight(0xffffff, 0.5);
  dir.position.set(5, 3, 5);
  scene.add(dir);

  /* ---- Build scene ---- */
  buildStarfield();
  buildEarth();
  buildAtmosphereRing();
  buildSatellites();
  buildISLLinks();
  buildGroundStations();

  /* ---- Events ---- */
  window.addEventListener("resize", onResize);
  bindControls();
  observeTab();
}

/* ============================================================
   Starfield — 1000 tiny dots on a distant sphere
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
    color: 0x888888,
    size: 1.0,
    sizeAttenuation: false,  // screen pixels
  })));
}

/* ============================================================
   Earth — dark sphere, standard material, no texture
   ============================================================ */
function buildEarth() {
  const g = new THREE.SphereGeometry(1, 64, 64);
  const m = new THREE.MeshPhongMaterial({
    color: 0x0a0e18,
    emissive: 0x030508,
    specular: 0x0a0a1a,
    shininess: 10,
  });
  scene.add(new THREE.Mesh(g, m));
}

/* ============================================================
   Atmosphere — simple additive ring, no custom shader
   Uses a slightly larger sphere with wireframe-like approach
   ============================================================ */
function buildAtmosphereRing() {
  const g = new THREE.SphereGeometry(1.005, 64, 64);
  const m = new THREE.MeshBasicMaterial({
    color: 0x1a3060,
    transparent: true,
    opacity: 0.08,
    side: THREE.BackSide,
  });
  scene.add(new THREE.Mesh(g, m));
}

/* ============================================================
   Satellites — screen-space pixel dots, NO transparency
   Single draw call, ~9,636 points
   ============================================================ */
function buildSatellites() {
  const sats = GLOBE.satellites;
  const N = sats.length;
  const pos = new Float32Array(N * 3);
  const col = new Float32Array(N * 3);

  for (let i = 0; i < N; i++) {
    const s = sats[i];
    pos[i * 3]     = s.x;
    pos[i * 3 + 1] = s.y;
    pos[i * 3 + 2] = s.z;

    const c = SHELL_COLORS[s.s] || SHELL_COLORS[0];
    col[i * 3]     = c[0];
    col[i * 3 + 1] = c[1];
    col[i * 3 + 2] = c[2];
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  g.setAttribute("color", new THREE.BufferAttribute(col, 3));

  satPoints = new THREE.Points(g, new THREE.PointsMaterial({
    size: 1.2,               // screen pixels
    sizeAttenuation: false,  // constant size regardless of distance
    vertexColors: true,
  }));
  scene.add(satPoints);
}

/* ============================================================
   ISL Links — sampled subset for visual clarity + performance
   Only render every Nth link per shell to show orbital structure
   without melting the GPU. ~2,400 lines instead of 9,636.
   ============================================================ */
function buildISLLinks() {
  const links = GLOBE.isl_links;
  const sats = GLOBE.satellites;

  // Sample: keep every 4th link (shows orbital ring structure clearly)
  const sampled = [];
  for (let i = 0; i < links.length; i++) {
    if (i % 4 === 0) sampled.push(links[i]);
  }

  const pos = new Float32Array(sampled.length * 6);
  for (let i = 0; i < sampled.length; i++) {
    const [aId, bId] = sampled[i];
    const a = sats[aId];
    const b = sats[bId];
    pos[i * 6]     = a.x;
    pos[i * 6 + 1] = a.y;
    pos[i * 6 + 2] = a.z;
    pos[i * 6 + 3] = b.x;
    pos[i * 6 + 4] = b.y;
    pos[i * 6 + 5] = b.z;
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));

  islLines = new THREE.LineSegments(g, new THREE.LineBasicMaterial({
    color: 0x334455,      // dim blue-gray, opaque — no alpha blending
  }));
  scene.add(islLines);
}

/* ============================================================
   Ground Stations — 20 red dots, slightly larger
   ============================================================ */
function buildGroundStations() {
  const gs = GLOBE.stations;
  const pos = new Float32Array(gs.length * 3);
  for (let i = 0; i < gs.length; i++) {
    pos[i * 3]     = gs[i].x;
    pos[i * 3 + 1] = gs[i].y;
    pos[i * 3 + 2] = gs[i].z;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));

  stationPoints = new THREE.Points(g, new THREE.PointsMaterial({
    color: 0xcc0000,
    size: 3.5,              // screen pixels
    sizeAttenuation: false,
  }));
  scene.add(stationPoints);
}

/* ============================================================
   UI Controls
   ============================================================ */
function bindControls() {
  const el = (id) => document.getElementById(id);

  const lnk = el("globe-show-links");
  const sta = el("globe-show-stations");
  const rot = el("globe-auto-rotate");

  if (lnk) lnk.addEventListener("change", () => {
    if (islLines) islLines.visible = lnk.checked;
  });
  if (sta) sta.addEventListener("change", () => {
    if (stationPoints) stationPoints.visible = sta.checked;
  });
  if (rot) rot.addEventListener("change", () => {
    if (controls) controls.autoRotate = rot.checked;
  });
}

/* ============================================================
   Animation — only runs when Constellation tab is active
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
  populateStats();
  init();
});
