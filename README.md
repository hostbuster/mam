# MAM — Maximum Audio Madness

A modern C++17 audio playfield. The first module is a small macOS command-line app that synthesizes a kick drum and streams it directly to the system’s default output using CoreAudio’s Default Output Audio Unit.

This repository will grow into a platform for rapid prototyping of audio ideas, with a focus on maintainability, performance, and clarity guided by the C++ Core Guidelines.

## Features

- **CoreAudio output**: Low-latency render callback to the default device.
  - Benefit: responsive playback suitable for live tweaking and testing without extra drivers.
- **Procedural kick drum**: Pitch and amplitude envelopes, onset click, gain.
  - Benefit: clean, repeatable synthetic drum without samples; easy to automate and extend.
- **One-shot or tempo-synced loop**: Trigger once or repeat at BPM until Ctrl-C.
  - Benefit: quick auditioning of sounds and patterns; acts as a timing baseline for other nodes.
- **Safe defaults**: Float32, non-interleaved stereo; uses actual device sample rate.
  - Benefit: fewer surprises across devices; avoids clipping and format mismatches.
- **Portable C++17**: Straightforward, clean code with minimal dependencies.
  - Benefit: easy to build, debug, and port; a solid base for future plugins or apps.
- **Modular architecture**: Separated DSP, realtime, offline, and file I/O modules.
  - Benefit: faster builds, clearer responsibilities, and easier testing/reuse per layer.
- **JSON graphs**: Define instruments, mixer, and settings in a portable file.
  - Benefit: versionable, human-editable sessions; easy automation and CI rendering.
- **Realtime + Offline**: CoreAudio streaming and high-quality file rendering (WAV/AIFF/CAF).
  - Benefit: identical musical results in live and batch contexts; predictable exports.
- **Concurrency scaffolding**: Command queue for sample-accurate control, offline job pool.
  - Benefit: glitch-free control changes in realtime and faster-than-realtime offline renders.

### Done

- Modularized build: `mam_core`, `mam_dsp`, `mam_io`, `mam_render` (INTERFACE)
- Executable renamed to `mam`; CMake presets and sanitizers supported
- JSON graph loader with commands and transport support; schema in `docs/`
- Realtime graph renderer with sample-accurate sub-block command processing
- `TransportNode` integrated for realtime pattern emission (BPM, swing scaffold, ramps scaffold)
- Offline renderers: graph, timeline, and parallel variants; `--offline-threads`
- Transport param-locks (realtime + offline); schema and examples updated
- Parameter system with registry, smoothing types (step/linear/exp), and named params via `ParamMap`
- Mixer with per-input gains and master with optional soft clip
- CLI: `--graph`, `--validate`, `--list-nodes`, `--list-params`, `--quit-after`
- IO: `ExtAudioFile` writer (WAV/AIFF/CAF) with 16/24/32f
- Docs: auto-generated `docs/ParamTables.md` from `ParamMap.hpp`; expanded README
- Tools: `gen_params` for docs; JSON examples `two_kicks.json`, `breakbeat.json`, `breakbeat_full.json`
- Offline scaffolds: `BufferPool` and `OfflineTopoScheduler`

### What's new (core evolution)

- Sample-accurate event timing in realtime via sub-block processing
- JSON commands are honored in offline renders (timeline renderer)
- Parallel offline renderer (`--offline-threads N`) for faster exports
- Parameter smoothing scaffold (node gain) via `ParameterRegistry`

## Build (macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Optional: generate an Xcode project

```bash
cmake -S . -B build-xcode -G Xcode
open build-xcode/kickdrum.xcodeproj
```

## Run

- **One-shot (default params):**

```bash
./build/mam
```

- **Continuous at 120 BPM (until Ctrl-C):**

```bash
./build/mam --bpm 120
```

- **Tune the sound:**

```bash
./build/mam \
  --f0 120 \
  --fend 35 \
  --pitch-decay 50 \
  --amp-decay 180 \
  --gain 0.9 \
  --click 0.1
```

- **Help:**

```bash
./build/mam --help
./build/mam --validate breakbeat.json
./build/mam --list-nodes two_kicks.json
./build/mam --list-params kick
```

### Full-feature example (recommended)

Use `breakbeat_full.json` to try multi-pattern transport with swing and tempo ramps:

- Realtime playback:

```bash
./build/mam --graph breakbeat_full.json
```

- Offline render to WAV (48 kHz float32), with optional parallelism:

```bash
./build/mam --graph breakbeat_full.json --wav out.wav --sr 48000
# Parallel offline rendering (e.g., 4 worker threads):
./build/mam --graph breakbeat_full.json --wav out.wav --sr 48000 --offline-threads 4
```

- Validate the example graph:

```bash
./build/mam --validate breakbeat_full.json
```

### Offline rendering

Render without using CoreAudio to an uncompressed audio file. Defaults to 48 kHz float32 WAV.

```bash
./build/mam --wav out.wav --duration 2.0                    # 2 seconds at 48 kHz float32 WAV
./build/mam --wav out.wav --sr 44100 --pcm16                 # 44.1 kHz 16-bit PCM WAV (compat)
./build/mam --wav out.aiff --format aiff --bitdepth 24       # AIFF 24-bit PCM
./build/mam --wav out.caf --format caf --bitdepth 32f        # CAF float32
```

Timed realtime exit:

```bash
./build/mam --graph two_kicks.json --quit-after 10
```

## Graph configuration (JSON)

You can define instruments and their parameters using a JSON graph file and pass it with `--graph path.json`.

### Schema (v1)
See `docs/schema.graph.v1.json` for a machine-readable schema.

- `version` (number): schema version (current: 1)
- `sampleRate` (number, optional): desired sample rate (realtime may override)
- `channels` (number, optional): output channels (default 2)
- `nodes` (array): list of nodes
  - `id` (string): node identifier
  - `type` (string): e.g., `kick`
  - `params` (object): type-specific settings
    - kick params: `f0`, `fend`, `pitchDecayMs`, `ampDecayMs`, `gain`, `click`, `bpm`, `loop`
- `connections` (array, optional): reserved for future routing

### Example (realtime, looping kick)

```json
{
  "version": 1,
  "sampleRate": 48000,
  "channels": 2,
  "nodes": [
    {
      "id": "kick1",
      "type": "kick",
      "params": {
        "f0": 120,
        "fend": 35,
        "pitchDecayMs": 50,
        "ampDecayMs": 180,
        "gain": 0.9,
        "click": 0.1,
        "bpm": 120,
        "loop": true
      }
    }
  ],
  "connections": []
}
```

Run it in realtime:

```bash
./build/mam --graph mygraph.json
```

Render offline to WAV:

```bash
./build/mam --graph mygraph.json --wav out.wav --sr 48000 --duration 2.0
```

### Transport (multi-patterns, swing, ramps)

Example:

```json
{
  "transport": {
    "bpm": 130,
    "lengthBars": 4,
    "resolution": 16,
    "swingPercent": 12,
    "tempoRamps": [ { "bar": 2, "bpm": 138 } ],
    "patterns": [
      { "nodeId": "kick1", "steps": "x...x...x...x..." },
      { "nodeId": "snr1",  "steps": "....x.......x..." },
      { "nodeId": "hat1",  "steps": "x.x.x.x.x.x.x.x." }
    ]
  }
}
```

Notes:
- Unknown node types or params are warned (to be expanded later).
- Multi-node routing via `connections` is reserved for future versions.

## Parameters

| Option | Type | Default | Description |
|---|---|---:|---|
| `--f0` | float (Hz) | 100.0 | Starting frequency of pitch envelope |
| `--fend` | float (Hz) | 40.0 | Target frequency of pitch envelope |
| `--pitch-decay` | float (ms) | 60.0 | Pitch decay time constant (exponential) |
| `--amp-decay` | float (ms) | 200.0 | Amplitude decay time constant (exponential) |
| `--gain` | float (0..1.5) | 0.9 | Output gain (be mindful of headroom) |
| `--bpm` | float | 0.0 | If > 0, the kick re-triggers every beat at this BPM |
| `--duration` | float (sec) | 1.2 | Duration of one-shot mode (ignored in loop mode) |
| `--click` | float (0..1) | 0.0 | Adds a tiny onset click to emphasize attack |
| `--wav` | path | — | If set, render offline to this WAV file and exit |
| `--sr` | float (Hz) | 48000.0 | Output sample rate for offline rendering |
| `--pcm16` | flag | float32 | Write 16-bit PCM instead of float32 when offline |
| `--format` | enum | wav | One of: `wav`, `aiff`, `caf` |
| `--bitdepth` | enum | 32f | One of: `16`, `24`, `32f` (float32) |
| `--offline-threads` | int | 0 | Use parallel offline renderer with N threads (0=single-thread) |
| `--graph` | path | — | Load a JSON graph file to build instruments/mixer |
| `--quit-after` | float (sec) | 0 | Realtime: auto-stop after given seconds (0 = disabled) |
| `--help`, `-h` | flag | — | Print usage |

Notes:
- Audio runs at the device’s sample rate; we request 48 kHz and accept what the device reports.
- Stream format is float32, stereo, non-interleaved (separate buffers per channel).

## Project Structure

- `CMakeLists.txt` — modular build; Apple framework linking
- `src/main.cpp` — CLI/argument parsing, dispatch to realtime or offline
- `src/instruments/...` — DSP implementations (kick, clap)
- `src/core/...` — Graph, Node, ParameterRegistry, ParamMap, config
- `src/core/TransportNode.hpp` — realtime transport (multi-patterns, swing, tempo ramps)
- `src/realtime/...` — CoreAudio output and renderers
- `src/offline/...` — offline renderers and helpers
- `src/io/...` — audio file writers

## Architecture Overview

- **Realtime**: `RealtimeRenderer`/`RealtimeGraphRenderer` open the Default Output AU and install a render callback. The callback pulls samples from the graph. Modular targets: `mam_core`, `mam_dsp`, `mam_io`, `mam_render`.
- **Render Callback**: Lives inside `RealtimeRenderer`. Pure synthesis (no I/O), non-blocking, no heap allocation, no locks.
- **Synthesis**: Exponential amplitude and pitch envelopes, sine oscillator, optional onset click. Left and right receive the same mono signal for now.
- **Timing**: In loop mode, retriggering is based on `BPM` → `framesPerBeat`; one-shot mode just plays for `--duration` seconds.
- **Offline Path**: `OfflineRenderer` calls the same synthesis into an interleaved buffer; `AudioFileWriter` uses ExtAudioFile to emit WAV/AIFF/CAF at 16/24-bit PCM or 32f.
- **Control & Concurrency**: `SpscCommandQueue` for sample-accurate commands; `JobPool` for parallel offline renders; mixer stage with soft clip.

### Why this architecture is future‑proof

- **Node graph with strict RT boundaries**: DSP nodes are side-effect-free and allocation-free in the audio thread, making realtime safe and portable to plugin targets.
- **Event-first control plane**: All control is expressed as timestamped commands. This decouples UI/transport/MIDI from DSP, enabling deterministic offline renders and headless batch.
- **Param registry + maps**: Strongly-typed params with ranges, names, and smoothing unify validation, documentation, and automation—one source of truth.
- **Dual-path parity**: Realtime (TransportNode + rolling horizon) and offline (timeline renderer) use the same command semantics, ensuring identical musical outcomes.
- **Modular build**: Split libraries (`mam_core`, `mam_dsp`, `mam_io`, `mam_render`) allow independent evolution, faster builds, and clean integration points.

## Development Path
- See also `PLAN.md` for the modular upgrade roadmap.

- Short-term
  - Event bucketing: drain commands each block, group by node, and apply `Trigger`/`SetParam`/`SetParamRamp` with sample accuracy.
  - Parameter registry: consistent `paramId` mapping per node type; ramp smoothing.
  - Block scheduler (offline): parallelize graph levels using `JobPool`.
  - More instruments: snare, hat, bass; more mixer controls.
  - Transport param-locks: allow per-step parameter sets/ramps via patterns.
  - Validation: enforce schema on `--validate`; check pattern targets/lengths; clamp params from `ParamMap`.
- Mid-term
  - Transport node (tempo, swing, patterns) driving triggers per node; host sync options. (Scaffold added)
  - Preset schema for instruments and graphs; versioned upgrades.
  - CI with sanitizer presets; clang-tidy gate; format checks.
  - Offline scheduler integration: topo levels, buffer aliasing, per-node latency/preroll.
- Long-term
  - Cross-platform audio backends; plugin targets (AUv3, VST3).
  - GUI editor for node graphs and live performance controls.

### Transport param-locks

Goal: let patterns do more than triggers by attaching parameter changes at specific steps. Both realtime and offline must render identical automation.

- JSON shape:

```json
{
  "transport": {
    "bpm": 128,
    "resolution": 16,
    "patterns": [
      { "nodeId": "kick1", "steps": "x...x...x...x...",
        "locks": [
          { "step": 0,  "param": "GAIN", "value": 0.9 },
          { "step": 8,  "param": "GAIN", "value": 1.0, "rampMs": 50 },
          { "step": 12, "param": "F0",   "value": 140.0, "rampMs": 100 }
        ]
      }
    ]
  }
}
```

- Semantics: at each `step` the engine emits `SetParam` or `SetParamRamp` before processing that sub-block. `param` may be a name or numeric `paramId`.
- Realtime: locks are pre-enqueued into the command queue and/or emitted by `TransportNode` at sample-accurate step boundaries.
- Offline: locks are generated alongside triggers in the timeline.
- Behavior: locks are latched. After a lock (or ramp) applies, the resulting parameter value stays in effect until another command changes it. For temporary bumps, schedule a follow-up lock to restore the previous value.

Authoring notes:
- Top-level `transport` locks support `param` by name (recommended) or `paramId`.
- Inline realtime `transport` node locks currently use `paramId` only. Use `--list-params <type>` to find IDs.

### Planned: Offline topo scheduler (design)

- DAG execution: process nodes by topological level (when connections land), mixing or routing buffers between levels.
- Buffer management: reuse via `BufferPool`; alias analysis to avoid copies; interleaved/planar adapters.
- Latency/preroll: nodes may report latency; timeline rendering inserts pre-roll so steady-state is aligned.
- Parity: results must match non-parallel baseline within very low tolerance.

### Planned: Validation & tooling

- `--validate` enforces JSON Schema in `docs/schema.graph.v1.json`, warns on unknown nodes/params, and clamps out-of-range params (per `ParamMap`).
- `--list-params <type>` prints parameter IDs/names/ranges/smoothing; used to author locks and commands.
- Future: `mam --list-nodes` shows available node types and their key params.

## Development Guidelines

- **Language level**: C++17. Prefer standard library facilities over custom code.
- **C++ Core Guidelines alignment**:
  - Prefer RAII for lifetime management; minimize raw owning pointers
  - Avoid exceptions in real-time audio paths; do not throw from the render callback
  - Use `noexcept` where it clarifies intent (non-throwing hot paths)
  - Use narrow, descriptive names and early returns to reduce nesting
  - Keep data immutable where reasonable; use `std::atomic` only where needed across threads
- **Real-time audio best practices**:
  - No locks, no allocations, no syscalls in the render callback
  - Avoid I/O (logging/printf) in the callback
  - Prefer branch-light DSP code; precompute constants outside the callback
  - Handle denormals if necessary (macOS often flushes to zero; monitor if you add filters)
- **Code style**:
  - Descriptive variable names; avoid abbreviations
  - Small, cohesive functions; guard clauses over deep nesting
  - Only add comments for non-obvious rationale or invariants

## Roadmap (Suggestions)

- **Synth Engine**
  - Add velocity sensitivity and multiple transient models (click/noise/short burst)
  - Multi-stage envelopes (ADSR/DAHDSR) and curved segments
  - Optional drive/distortion, saturation, clipper with oversampling
  - State-variable filter (LP/BP/HP) with keytracking and envelope modulation
  - Stereo width, pan law, and mono-sum-safe output
- **Timing & Control**
  - Sub-beat trigger patterns, swing, ratcheting
  - Tempo sync to host tempo via MIDI Clock and Ableton Link
  - Real-time parameter changes via MIDI CC and/or OSC
- **Host Integration**
  - AUv3 / Audio Unit v2 plug-in target
  - Cross-platform backends (CoreAudio, WASAPI, JACK/ALSA)
- **Performance**
  - SIMD for oscillators and filters
  - Lock-free queues for control messages to the audio thread
  - Benchmarking harness and profiling presets
- **Quality & Tooling**
  - Unit tests for DSP primitives and envelopes
  - Continuous Integration with builds and basic audio render tests
  - `clang-format` and `clang-tidy` configuration aligned with project style
- **Product**
  - Preset system with versioned JSON
  - Release packaging and a small GUI preview app (Qt/SwiftUI)

## Contributing

Pull requests are welcome. Please:
- Keep audio-thread code allocation-free and lock-free
- Follow C++17 and the C++ Core Guidelines
- Prefer small, focused changes with clear motivation
- Include tests for non-trivial DSP changes when feasible

## License

TBD. Until specified, treat the code as All Rights Reserved.

## Acknowledgements

- Apple CoreAudio team and public headers/examples
- The wider audio DSP community for inspiration and best practices

## Commands and realtime control

- Command types (scaffolded): `Trigger`, `SetParam`, `SetParamRamp`
- Transport: events are drained per audio block and applied at exact sample offsets via sub-block processing
- Implementation notes:
  - `SpscCommandQueue` (lock-free, bounded) from control → audio thread
  - `ProcessContext.blockStart` contains the absolute sample start of the current block
  - Nodes implement `handleEvent(const Command&)`; realtime now routes commands by `nodeId`

Smoothed parameters (current):

- kick: `f0`, `fend`, `pitchDecayMs`, `ampDecayMs`, `gain`
- clap: `ampDecayMs`, `gain`

### JSON control example

While commands are typically fed at runtime (e.g., via MIDI/OSC/UI), you can also predefine initial parameters in the graph and drive tempo via `bpm/loop`. Example with two kicks and a clap, plus a mixer, and showing intended command IDs for parameters:

```json
{
  "version": 1,
  "sampleRate": 48000,
  "channels": 2,
  "nodes": [
    { "id": "kick_fast", "type": "kick", "params": { "f0": 120, "fend": 35, "pitchDecayMs": 50, "ampDecayMs": 180, "gain": 0.9, "click": 0.1, "bpm": 120, "loop": true } },
    { "id": "clap1",     "type": "clap", "params": { "ampDecayMs": 180, "gain": 0.8, "bpm": 60,  "loop": true } },
    { "id": "kick_slow", "type": "kick", "params": { "f0": 90,  "fend": 30, "pitchDecayMs": 70, "ampDecayMs": 220, "gain": 0.9, "click": 0.05, "bpm": 60,  "loop": true } }
  ],
  "mixer": {
    "masterPercent": 90,
    "softClip": true,
    "inputs": [
      { "id": "kick_fast", "gainPercent": 80 },
      { "id": "clap1",     "gainPercent": 15 },
      { "id": "kick_slow", "gainPercent": 85 }
    ]
  },
  "connections": []
}
```

Parameter IDs for realtime `SetParam` commands (for developers):

- kick: `F0=1`, `FEND=2`, `PITCH_DECAY_MS=3`, `AMP_DECAY_MS=4`, `GAIN=5`, `CLICK=6`, `BPM=7`, `LOOP=8`
- clap: `AMP_DECAY_MS=1`, `GAIN=2`, `BPM=3`, `LOOP=4`

### Parameter maps (IDs, ranges, units)

Kick (`type: kick`):

- `F0` (id=1): 40–200 Hz
- `FEND` (id=2): 20–120 Hz
- `PITCH_DECAY_MS` (id=3): 10–200 ms
- `AMP_DECAY_MS` (id=4): 50–400 ms
- `GAIN` (id=5): 0.0–1.5 (linear)
- `CLICK` (id=6): 0.0–1.0 (linear)
- `BPM` (id=7): 0–300 (loop rate; 0 disables)
- `LOOP` (id=8): 0.0/1.0 (false/true)

Clap (`type: clap`):

- `AMP_DECAY_MS` (id=1): 20–300 ms
- `GAIN` (id=2): 0.0–1.5 (linear)
- `BPM` (id=3): 0–300 (loop rate; 0 disables)
- `LOOP` (id=4): 0.0/1.0 (false/true)

Validation and clamping:

- Params are validated against ranges at load time. Out-of-range values are clamped to safe min/max per the tables above.
- Commands using named params (`"param": "F0"`) also resolve via the same maps.

Auto-generated tables:

See `docs/ParamTables.md` for an always-up-to-date table generated from the source-of-truth `ParamMap.hpp`.

## Transport (patterns)

You can define patterns and tempo in `transport`. Supports optional swing and tempo ramps.

Schema:

```json
{
  "transport": {
    "bpm": 130,
    "lengthBars": 4,
    "resolution": 16,
    "swingPercent": 10,
    "tempoRamps": [ { "bar": 2, "bpm": 140 } ],
    "patterns": [
      { "nodeId": "kick1", "steps": "x...x..x..x..." },
      { "nodeId": "snr1",  "steps": "..x...x...x..." },
      { "nodeId": "hat1",  "steps": "x.x.x.x.x.x.x.x." }
    ]
  }
}
```

In offline mode, transport generates triggers merged with explicit `commands`. In realtime, a short horizon is pre-enqueued at startup.

### Offline command timeline

Offline renders now honor JSON `commands` with sample-accurate timing using the timeline renderer.

Examples:

```bash
# Short demo with param changes
./build/mam --graph two_kicks.json --wav two_kicks.wav --sr 48000 --duration 8

# Oldschool breakbeat (~16s) with evolving params and parallel render
./build/mam --graph breakbeat.json --wav breakbeat.wav --sr 48000 --duration 16 --offline-threads 4
```

### Realtime Transport (TransportNode)

In realtime, a `transport` node can emit sample-accurate triggers at segment boundaries without blocking the audio thread:

- A lightweight `TransportNode` advances a musical step clock and emits `Trigger` events for matching steps.
- `RealtimeGraphRenderer` delivers these events to the targeted nodes at the exact sample offset; rendering then proceeds for the sub-block.
- For stability, the app also maintains a rolling pre-enqueue horizon (seconds or `--quit-after`) so longer patterns continue seamlessly.

Current limitations: a single inline pattern per `transport` node (scaffold). The JSON `transport` section still generates triggers for offline parity. You can also embed a `transport` node in `nodes` for realtime:

```json
{
  "nodes": [
    { "id": "kick1", "type": "kick", "params": { "bpm": 0, "loop": false } },
    { "id": "transport1", "type": "transport", "params": { "bpm": 130, "resolution": 16, "pattern": { "nodeId": "kick1", "steps": "x...x..x..x..." } } }
  ]
}
```

Command param addressing:

- You can specify parameters by numeric `paramId` (legacy) or by name using `"param": "F0"` etc.
- Names are resolved based on the node’s `type` using the Parameter Maps above.

## Threading strategy

- Realtime: single CoreAudio callback as conductor; future: optional worker threads via Audio Workgroup
- Offline: `JobPool` threads for parallel graph level execution (to be enabled)
- No allocation or locks on audio thread; preallocate all buffers/queues

## Future

- State model
  - Single source of truth: immutable “ProjectState” (graph, nodes, params, connections, transport) with versioned schema and migrations.
  - Unidirectional flow: UI produces Commands → validated → RT-safe CommandQueue → audio thread applies at sample offsets.
  - Deterministic snapshots: serialize/deserialize entire state (JSON/CBOR) for recall, undo/redo, and headless batch.

- Parameter system
  - Strongly-typed parameters with ranges, units, and smoothing strategies (per-parameter ramp modes, slew/one-pole/exponential).
  - Modulation matrix: sources (LFO, env, MIDI, MPE, sidechain) to destinations (params) with depth, summing, and clamping.
  - Namespaced param IDs; registry per node type; host automation mapping.

- Timing and transport
  - Central clock with sample-time, musical-time, tempo map, and time signature; support tempo ramps and bar/beat markers.
  - Sample-accurate event bucketing; split processing at event boundaries (sub-blocks).
  - Latency compensation (per node), offline pre-roll, click-free parameter transitions.

- Graph engine
  - DAG with validated topology; topological levels; block scheduler for offline parallel render; optional realtime workgroup fan-out.
  - Ports with types (audio/control/event/MIDI), explicit channel layouts, and per-edge latency.
  - Zero-copy buffer routing with alias analysis; per-node scratch pools; interleaved/planar adapters.

- Realtime safety
  - No dynamic allocation or locks in callback; preallocated arenas; fixed-capacity queues.
  - RT-logging via lock-free ring and deferred flushing; OSStatus/error breadcrumbs outside audio thread.
  - Denormal handling (FTZ/DAZ), predictable soft-clip/limiter at master.

- DSP quality
  - Oversampling framework per node with polyphase filters; consistent phase and latency reporting.
  - SIMD-first primitives (SSE/NEON/AVX2/AVX512) with scalar fallback; vector-friendly memory layout.
  - Dither/TPDF on PCM output; noise-shaped options; safe headroom policies.

- Concurrency and performance
  - Offline JobPool with work-stealing; block tiling; NUMA-aware affinity (future).
  - AudioWorkGroup (macOS) integration for multi-thread RT when needed; deadline monitoring with telemetry markers.
  - Hot paths profiled; microbench harness; perf counters per node.

- File and IO
  - Unified render API (WAV/AIFF/CAF/FLAC) with streaming writer; capture taps at any edge for debugging (“wiretap”).
  - Async disk IO for sample-based instruments; prefetch and pre-decode caches.

- Extensibility
  - Stable Node ABI with factory registry; dynamic module loading (hot-reload in dev).
  - Plugin hosting (AUv3/VST3) nodes; sandboxing, time-info bridging, parameter proxying.

- Testing and CI
  - Golden-render tests (sample-accurate comparisons with tolerances); property tests (fuzz events/params).
  - Stress: command-rates, max graph depth/width, CPU throttling, denormal storms.
  - CI presets (asan/ubsan/tsan), clang-tidy gates, formatting checks.

- Observability
  - Trace spans (block start/end, node process, enqueue/dequeue) via lightweight markers; export to JSON trace viewer.
  - Metrics: xruns, max block time, avg per node; on-demand perf dump.

- UI integration path
  - Thin UI layer talks to a Command API (no direct state mutation), observes State snapshots over a read-only channel.
  - Reactive view-model with diff patches; background validation (graph changes) before RT adoption (double-buffered state swap).

- Persistence
  - Versioned project/graph schemas; migration registry; preset banks with dependencies.
  - Content-addressable assets (samples, wavetables) with caching and hashing.

If helpful, I can start by:
- Implementing per-node event sub-block processing (sample-accurate).
- Adding a ParameterRegistry with ramp/smoothing and modulation slots.
- Introducing latency reporting and block-level parallel scheduler for offline.
