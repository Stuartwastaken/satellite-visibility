# Starlink C++ Visualizer

A weekend project simulating core Starlink ground station engineering challenges: **orbital mechanics, packet routing, and satellite handoff scheduling**.

I built this to explore the systems-level constraints of LEO satellite constellations. The core logic is a high-performance C++ backend that computes orbital trajectories, visibility graphs, and scheduling optimization, while a custom WebGL frontend visualizes the data in real-time.

![Starlink Visualizer Preview](https://github.com/user-attachments/assets/placeholder.png)
*(Note: Visualizer runs locally in the browser)*

## Project Architecture

The system is designed to mirror a real ground station architecture: **C++ for compute, Web for operations.**

```
[C++17 Backend] ----------------------> [JSON Data Bridge] -----------------> [Browser Frontend]
- Walker Delta constellation gen         - Compact wire format                 - Three.js 3D Globe
- Visibility graph construction          - Pre-computed state                  - Packet reorder plots
- Packet router simulation                                                     - Handoff Gantt charts
- Dynamic programming scheduler
```

## Core C++ Modules

The `src/visualizer_data.cpp` engine (1000+ lines) implements three distinct solvers:

### 1. Constellation & Visibility Graph
Generates a full **9,636-satellite** constellation across 5 orbital shells (matching Starlink's FCC filing).
- **Orbital Mechanics**: Implements proper Keplerian projection using RAAN, argument of latitude, and Walker Delta phasing.
- **Visibility Engine**: Performs O(S×G) geometric computations (192,720 pairs) to determine satellite visibility for 20 ground stations based on elevation angle (min 25°) and Earth occlusion.
- **ISL Topology**: Computes the intra-plane inter-satellite link ring network.

### 2. Packet Router Simulator
Simulates the network-layer challenges of multi-path satellite routing.
- **Reordering**: Models packet arrival disorder caused by different slant ranges and path latencies.
- **Priority Queuing**: Implements strict priority scheduling (Control > Real-time > Streaming > Bulk).
- **Lock-free Design**: Simulates high-throughput packet processing using ring buffer logic.

### 3. Handoff Scheduler (Dynamic Programming)
Solves the "make-before-break" problem: finding the optimal chain of satellites to maintain 100% uptime.
- **Algorithm**: O(n²) dynamic programming optimization.
- **Objective**: Maximize total coverage time while ensuring signal strength > 5dB at every handoff.
- **Timing**: Binary search to find the precise "equal-signal crossover point" for seamless transitions.

## The Visualizer (Browser)

I wrote a custom frontend to verify the C++ results visually. It includes three interactive tabs:

1.  **Constellation Globe (3D)**: Interactive WebGL globe rendering all 9,636 satellites and ISL links. Features dynamic visibility lines from selected ground stations (e.g., Redmond, WA) showing real-time connectivity cones.
2.  **Packet Router**: Scatter plot visualizing sequence number gaps and reordering patterns.
3.  **Handoff Scheduler**: Gantt chart showing satellite pass overlaps, signal strength curves, and the optimal handoff path selected by the DP algorithm.

## Build & Run

**Requirements**: CMake 3.16+, C++17 compiler, Python 3 (for local server).

```bash
# 1. Build the C++ data generator
mkdir -p build && cd build
cmake .. && make visualizer_data

# 2. Run the simulation (generates visualizer/data/data.js)
./visualizer_data
# Output:
#   Globe: 9636 satellites, 9636 ISL links, 2105 visibility edges...
#   Wrote ../visualizer/data/data.js

# 3. Launch the visualizer
cd ../visualizer
python3 -m http.server 8080
```

Open **http://localhost:8080** to view the results.

## Technical Details

- **Language**: C++17 (for `std::filesystem`, structured bindings)
- **Math**: Custom 3D vector algebra and spherical geometry implementations.
- **Performance**: The visualizer renders ~20,000 objects at 60fps using screen-space sizing and opaque geometry batching.
- **Data**: All orbital parameters (550km/53°, 570km/70°, etc.) are based on public Starlink Gen1/Gen2 specs.

---
*Created by Stuart Ray*
