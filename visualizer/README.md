## Starlink C++ Visualizer

This GUI lets you **see** what the C++ interview prep projects are doing:

- Visibility graph (satellites ↔ ground stations)
- Packet reordering + priority routing
- Handoff scheduling with overlap constraints

### 1) Build the generator

From `projects/satellite-visibility`:

```
cmake -S . -B build
cmake --build build
```

### 2) Generate visualization data

```
./build/visualizer_data
```

Optional flags (example):

```
./build/visualizer_data --planes 48 --sats 24 --stations 12 --packets 600 --reorder 0.25 --drop 0.05
```

This writes:

```
visualizer/data/data.js
```

### 3) Open the GUI

On macOS:

```
open visualizer/index.html
```

If you change parameters, re-run `visualizer_data` and refresh the browser tab.
