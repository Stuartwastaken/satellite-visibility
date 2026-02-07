# Starlink C++ Visualizer

**[Live Demo](https://satellite-visibility.vercel.app/)**

A weekend project simulating core Starlink ground station engineering challenges in C++17: **orbital mechanics, packet routing, and satellite handoff scheduling** — with an interactive WebGL visualizer. Inspired by https://satellitemap.space/constellation/starlink

The C++ backend computes the full constellation state, and the browser renders it. This mirrors real ground station architecture: high-performance compiled code for compute, lightweight frontends for operations.

## Project Architecture

```
[C++17 Backend]                        [JSON Bridge]              [Browser Frontend]
visualizer_data.cpp (1,072 lines)      data.js (776 KB)           Three.js + Canvas 2D
├─ Orbital mechanics engine      ───>  window.GLOBE_DATA    ───>  3D Globe (9,636 sats)
├─ Visibility graph builder      ───>  window.PACKET_DATA   ───>  Packet scatter plot
├─ Packet router simulator       ───>  window.HANDOFF_DATA  ───>  Handoff Gantt chart
└─ DP handoff scheduler
```

---

## The Three Simulations

### 1. Constellation & Visibility Globe

**Problem**: Which satellites can a ground station see at any instant, and how good are the links?

The C++ generates a full **9,636-satellite** Walker Delta constellation across 5 orbital shells matching Starlink's FCC filing:

| Shell | Planes × Sats | Altitude | Inclination | Count |
|-------|---------------|----------|-------------|-------|
| Gen1 Main | 72 × 22 | 550 km | 53.0° | 1,584 |
| Gen1 Backup | 72 × 22 | 540 km | 53.2° | 1,584 |
| Polar | 36 × 20 | 570 km | 70.0° | 720 |
| SSO | 6 × 58 | 560 km | 97.6° | 348 |
| Gen2 | 120 × 45 | 525 km | 53.0° | 5,400 |

Each satellite's position is computed via **Keplerian orbit projection** — RAAN (Right Ascension of Ascending Node), argument of latitude, and Walker Delta F=1 phasing for uniform global distribution. The engine then performs **192,720 geometric calculations** (9,636 sats × 20 stations) to build the visibility graph: haversine distance, elevation angle, slant range, and propagation latency for every satellite-station pair.

The Three.js frontend renders the constellation on a 3D globe with NASA Blue Marble + Earth at Night textures. Select any ground station from the dropdown to see its visibility cone — red lines fanning out to every satellite above 25° elevation, with per-station metrics (visible count, elevation range, latency).

**C++ techniques**: Spherical trigonometry, law of cosines on Earth-satellite triangle, coordinate frame transforms (geographic → 3D Cartesian), compact JSON serialization.

**Starlink relevance**: This is the fundamental state vector that ground station software recomputes continuously as satellites orbit at 27,000 km/h. It determines antenna pointing, beam scheduling, and routing decisions.

---

### 2. Packet Reordering + Priority Router

**Problem**: When packets arrive from multiple satellites via different beams, they arrive out of order. How do you reorder them and prioritize critical traffic?

A ground station receives downlinks from multiple satellites simultaneously, each at a different elevation angle (different slant range = different latency). A packet from an overhead satellite arrives in ~1.8ms; the same packet from a low-elevation satellite takes ~3.7ms. Result: **multi-path reordering**.

The scatter plot visualizes this. X = arrival order, Y = sequence number. The diagonal is perfect in-order delivery. Deviations show reordering. The simulation runs 400 packets through:

- **18% reorder probability** — adjacent packets swap positions, modeling multi-beam path diversity
- **1.8% drop rate** — atmospheric attenuation and interference losses
- **4 priority classes**: Control (beam management), Real-time (voice/gaming), Streaming (video), Bulk (downloads)
- **8 output queues** — parallel egress to ISP uplinks

The near-perfect diagonal with small local perturbations demonstrates the system working correctly: most packets arrive in order, but the multi-path effect creates exactly the kind of jitter that satellite ground stations must handle with reorder buffers and priority scheduling.

**C++ techniques**: `std::mt19937` seeded RNG for reproducibility, priority queue scheduling, ring buffer reorder logic.

**Starlink relevance**: Production ground stations use kernel-bypass packet processing (DPDK) with lock-free ring buffers — the same pattern modeled here. Each priority class gets a different reorder buffer policy: small buffers for real-time (tolerate some disorder, minimize latency), large buffers for bulk (perfect ordering, latency doesn't matter).

---

### 3. Satellite Handoff Scheduler (Dynamic Programming)

**Problem**: Each satellite is visible for only 3–10 minutes. How do you chain them together for seamless, uninterrupted coverage?

This is the most algorithmically deep component. Given overlapping satellite visibility windows with time-varying signal quality, find the optimal chain that **maximizes total uptime** while keeping signal strength above 5 dB at every handoff.

**The DP formulation**:
```
State:    dp[i] = max coverage time for schedule ending at window i
Base:     dp[i] = window[i].duration
Transfer: For valid predecessor j with ≥2s overlap:
            t = binary_search_handoff_time(j, i)     // equal-signal crossover
            if signal(j, t) ≥ 5dB AND signal(i, t) ≥ 5dB:
              dp[i] = max(dp[i], dp[j] + window[i].end − window[j].end)
Answer:   max(dp[0..n−1])
```

The signal model is parabolic: peak at mid-pass (satellite overhead), degrading quadratically toward the edges (rising/setting). The **binary search handoff timing** (50 iterations) finds the exact moment where the declining signal of the outgoing satellite equals the rising signal of the incoming one — maximizing link quality margin at the transition.

The Gantt chart shows the result: 10 satellite passes chained into 9 handoffs achieving **100% coverage (4,063s) with 0s gap time** and a worst-case handoff signal of 6.99 dB. Every handoff is **make-before-break** — the new link is established before the old one is released.

**C++ techniques**: O(n²) dynamic programming, binary search optimization, parabolic signal modeling, parent-pointer backtracking for solution reconstruction.

**Starlink relevance**: This is the central scheduling problem in LEO satellite communications. Each user terminal runs this independently (embarrassingly parallel across millions of terminals). The ground station broadcasts constellation state vectors; terminals compute their own optimal schedules locally.

---

## Build & Run Locally

**Requirements**: CMake 3.16+, C++17 compiler.

```bash
# Build and run the C++ data generator
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release && make visualizer_data
./visualizer_data
# Output: Globe: 9636 satellites, 9636 ISL links, ~2100 visibility edges

# Serve the visualizer
cd ../visualizer && python3 -m http.server 8080
# Open http://localhost:8080
```

## Technical Stack

| Layer | Technology | Purpose |
|-------|-----------|---------|
| Compute | C++17 | Orbital mechanics, DP scheduling, packet simulation |
| Build | CMake | Multi-target compilation with compile-time path injection |
| Data | JSON (776 KB) | Pre-computed state bridge between C++ and browser |
| 3D Rendering | Three.js (ES modules) | WebGL globe with NASA textures, ~20K objects at 60fps |
| 2D Rendering | Canvas 2D (HiDPI) | Scatter plots and Gantt charts |
| Deploy | Vercel | Static site hosting with pre-built C++ data |

---

*Created by Stuart Ray*
