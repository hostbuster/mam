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
- **Instruments: kick, clap, TB‑303 (extended)**: Param maps, modulation, transport locks.
  - Benefit: classic drum/synth palette with named‑param automation and modulation matrix.
- **Spectral sidechain ducking (beta)**: multiband/FFT ducking so keys only duck overlapping frequencies.
  - Benefit: preserves brightness and space; kick ducks bass freqs without dulling mids/highs.
- **Concurrency scaffolding**: Command queue for sample-accurate control, offline job pool.
  - Benefit: glitch-free control changes in realtime and faster-than-realtime offline renders.

### New in this build

- **Ports visibility (`--print-ports`)**: Print declared `ports.inputs[]` / `ports.outputs[]` per node, including roles and channel counts. Handy to author sidechains and multi-channel graphs.
- **Preroll reported in offline export**: Export summary now includes computed graph preroll in ms (derived from node latencies) for transparent start transients.
- **Performance trace export (`--trace-json`)**: Write Chrome/Perfetto-compatible JSON traces with per-node timings to analyze hot spots.

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
- CLI: `--rack` (preferred; `--graph` deprecated), `--validate`, `--list-nodes`, `--list-params`, `--quit-after`
- IO: `ExtAudioFile` writer (WAV/AIFF/CAF) with 16/24/32f
- Docs: auto-generated `docs/ParamTables.md` from `ParamMap.hpp`; expanded README
- Tools: `gen_params` for docs; JSON examples under `examples/` (`demo.json`, `demo2.json`, sidechain variants)
- Offline scaffolds: `BufferPool` and `OfflineTopoScheduler`

### What's new (core evolution)

- Sample-accurate event timing in realtime via sub-block processing
- JSON commands are honored in offline renders (timeline renderer)
- Parallel offline renderer (`--offline-threads N`) for faster exports
- Parameter smoothing scaffold (node gain) via `ParameterRegistry`
- Routed, topological graph execution with `connections` and per-edge gains (MVP)
- Per-edge wet/dry semantics: `gainPercent` (wet into destination) and `dryPercent` (dry tap to master)
- Realtime loop diagnostics gated by `--verbose`
- New nodes: `compressor` (port-1 sidechain detector) and `reverb` (Schroeder-style, demo-quality)
- Ports (MVP): nodes can declare `ports.inputs[]`/`ports.outputs[]` and route via `fromPort`→`toPort`
- Per-rack and per-bus meters in realtime: `--meters` prints periodic rack and bus peak/RMS. Offline, `--meters` prints mix meters after export.
- Per-node meters: `--meters-per-node` prints peak/RMS per node; nodes with no audio are marked `inactive`
- Looping UX: `--loop-minutes` / `--loop-seconds` auto-derives loop-count; export prints planned duration (incl. preroll/tail)
- Validation/CLI: schema enforcement hook; `--list-node-types` prints supported node types
  - Strict JSON Schema can be enabled at configure time via `-DMAM_USE_JSON_SCHEMA=ON` (requires bundled `third_party/json-schema.hpp`)
  - You can also enforce schema at runtime using `--schema-strict` (both realtime and offline)
- Realtime parity fixes: apply SetParam/SetParamRamp before Trigger at identical timestamps; precise loop length from bars; multi‑loop pre‑synthesis to avoid boundary drift.
- Diagnostics: new `--dump-events` prints synthesized commands with time/bar/step; improved `--print-triggers` callback logging.
- TB‑303 extended: LFO phase routing (`destParam: "LFO.<id>.phase"`) and pseudo‑params `LFO1_FREQ_HZ`/`LFO2_FREQ_HZ` for per‑step LFO rate.

## Build (macOS)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Optional: generate an Xcode project

```bash
cmake -S . -B build-xcode -G Xcode
open build-xcode/mam.xcodeproj
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
./build/mam --validate examples/demo.json
./build/mam --list-nodes examples/demo.json
./build/mam --list-params kick
./build/mam --print-ports --rack examples/rack/demo.json           # show ports per node
./build/mam --trace-json trace.json --rack examples/rack/demo.json  # export performance trace (Chrome/Perfetto)
./build/mam --rack examples/rack/swing_extreme.json --wav --sha1     # export WAV and print SHA1 of samples
./build/mam --dump-events --rack examples/rack/acid303_sidechain.json   # print command timeline (time/bar/step)
```

### Full-feature example (recommended)

Use `examples/rack/demo.json` to try multi-pattern transport with swing and tempo ramps:

- Realtime playback:

```bash
./build/mam --rack examples/rack/demo.json
./build/mam --rack examples/rack/demo.json --verbose                 # loop diagnostics
./build/mam --rack examples/rack/demo.json --random-seed 42          # deterministic randomness override
./build/mam --rack examples/rack/acid303_sidechain.json              # TB-303 + kick sidechain groove
```

Longer techno demo (16-bar) in `examples/rack/demo2.json`:

Realtime:

```bash
./build/mam --rack examples/rack/demo2.json --verbose
# For loop testing, you can set lengthBars to 1 and verify seamless boundaries
```

Offline export (auto-duration from transport bars, includes preroll/tail):

```bash
./build/mam --rack examples/rack/demo2.json --wav techno.wav --sr 48000 --normalize --peak-target -1.0
./build/mam --rack examples/rack/demo2.json --schema-strict --wav techno.wav  # validate against schema before export
```

- Offline render to WAV (48 kHz float32), with optional parallelism:

```bash
./build/mam --rack examples/rack/demo.json --wav out.wav --sr 48000
./build/mam --rack examples/rack/demo.json --wav out.wav --random-seed 123  # deterministic export
# Parallel offline rendering (e.g., 4 worker threads):
./build/mam --rack examples/demo.json --wav out.wav --sr 48000 --offline-threads 4
```

- Validate the example graph:

```bash
./build/mam --validate examples/rack/demo.json
```

### Offline rendering

Render without using CoreAudio to an uncompressed audio file. Defaults to 48 kHz float32 WAV.

```bash
./build/mam --wav out.wav --duration 2.0                    # 2 seconds at 48 kHz float32 WAV
./build/mam --wav out.wav --sr 44100 --pcm16                 # 44.1 kHz 16-bit PCM WAV (compat)
./build/mam --wav out.aiff --format aiff --bitdepth 24       # AIFF 24-bit PCM
./build/mam --wav out.caf --format caf --bitdepth 32f        # CAF float32
```

#### Auto-duration and export flags

- By default, export length follows what you authored:
  - If `transport` is present: renders exactly `lengthBars` (tempo ramps and swing respected), then adds a short tail (default 250 ms) so decays finish.
  - Else if `commands` are present: renders until the last command time, plus the tail.
  - Else: renders a sensible default (2.0 s) plus tail.
- Override flags (CLI wins over JSON):
  - `--duration SEC`: hard duration; bypasses auto logic.
  - `--bars N`: force N bars from `transport` (if present).
  - `--loop-count N`: repeat the transport sequence N times.
  - `--start-bar B`: start rendering from bar B (0-based) when using transport.
  - `--end-bar E`: end rendering at bar E (exclusive) when using transport. Combined with start-bar to render a subrange.
  - `--loop-minutes M` / `--loop-seconds S`: auto-derive loop-count to reach at least M minutes / S seconds.
  - `--tail-ms MS`: change the decay tail (default 250).
  - Preroll: offline export automatically adds graph preroll derived from node latencies (e.g., delay lines) so transients start fully formed.
  - When looping, export prints planned duration (incl. preroll/tail).
  - Auto naming: if you pass `--wav` without a filename, the exporter auto-names the file as `<rack_basename>_<frames>f.wav` (or `render_<frames>f.wav` if no rack path is set).
  - Sample hash: `--sha1` prints `SHA1(samples)` of the rendered float samples for deterministic comparisons across renders.

Examples:

```bash
# Auto-duration from transport (bars + tail)
./build/mam --rack examples/rack/demo.json --wav demo.wav

# Force 8 bars with a shorter tail
./build/mam --rack examples/rack/demo.json --wav demo.wav --bars 8 --tail-ms 100

# Hard 10-second render (overrides everything)
./build/mam --rack examples/rack/demo.json --wav demo.wav --duration 10
```

#### Normalization

- `--normalize`: normalize output peak to -1.0 dBFS.
- `--peak-target dB`: normalize peak to a specific target (e.g., `-0.3`).
- The exporter prints both pre-/post-peak and applied gain in dB. Normalization is applied prior to file write and never clips.
 - Tip: for realtime/export parity, render offline at your device sample rate using `--sr <Hz>` and keep peaks ≤ −1 dBFS.

#### Topology and meters

- `--print-topo`: print a simple topological order derived from `connections` (MVP). Helpful to validate routing intent.
- `--print-ports`: print declared ports (inputs/outputs), roles, and channel counts per node.
- `--meters`: realtime sessions print periodic per-rack and per-bus meters (when buses are defined); offline export prints a concise mix line with peak and RMS in dBFS. Adjust interval with `--meters-interval SEC` (min 0.05s, default 1.0s).
- `--metrics-ndjson path.ndjson`: write NDJSON metrics each interval for tooling (one line per rack/bus). Scope with `--metrics-scope racks,buses`.
- `--verbose`: in realtime, print loop counter and elapsed time at loop boundaries.
- `--meters-per-node`: print per-node peak/RMS and mark nodes with no audio as `inactive`.
  - When combined with `--verbose` in realtime, per-node meters are printed each time the loop boundary is crossed.
- `--schema-strict`: enforce JSON Schema `docs/schema.graph.v1.json` on load (realtime/offline). Enabled by default in dev builds (MAM_USE_JSON_SCHEMA=ON). Requires validator available (see build note).
- `--trace-json path.json`: write Chrome/Perfetto-compatible trace events with per-node timings; open in Chrome `chrome://tracing` or Perfetto.

LFOs and modulation matrix:
- See `docs/LFO.md` for a guide to authoring LFOs, routing to params, LFO-on-LFO frequency modulation, per-step transport locks, and mapped routes (`min`/`max`, `map: linear|exp`).
 - TB‑303 extras: per‑step pseudo‑params `LFO1_FREQ_HZ`/`LFO2_FREQ_HZ` and LFO phase target via `"LFO.<id>.phase"`.

##### Sidechain routing (MVP)

- Nodes can declare input/output ports with `ports.inputs[]` / `ports.outputs[]`.
- Use `connections[].fromPort`/`toPort` to route into specific ports. Example sidechain compressor:

```json
{
  "nodes": [
    { "id": "comp1", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain" } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "bass",  "to": "comp1", "fromPort": 0, "toPort": 0 },
    { "from": "kick",  "to": "comp1", "fromPort": 0, "toPort": 1 }
  ]
}
```

See also: docs/Sidechain.md for an in-depth guide and additional patterns.

Wiretap (debugging):
- Insert a `wiretap` node to record the input of the effect chain to a WAV file during offline export.

```json
{
  "nodes": [
    { "id": "wt1", "type": "wiretap", "params": { "path": "tap.wav", "enabled": true },
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [ { "from": "snr1", "to": "wt1" } ]
}
```

Sidechain cookbook (more examples):

- Mono key into stereo compressor (2→1 sidechain, already supported):

```json
{
  "nodes": [
    { "id": "compA", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" },
                                 { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "bass", "to": "compA", "fromPort": 0, "toPort": 0 },
    { "from": "kick", "to": "compA", "fromPort": 0, "toPort": 1 }
  ]
}
```

- One key, many listeners (kick sidechains both pad and bass):

```json
{
  "nodes": [
    { "id": "compPad",  "type": "compressor", "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ], "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } },
    { "id": "compBass", "type": "compressor", "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ], "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "pad",  "to": "compPad",  "fromPort": 0, "toPort": 0 },
    { "from": "bass", "to": "compBass", "fromPort": 0, "toPort": 0 },
    { "from": "kick", "to": "compPad",  "fromPort": 0, "toPort": 1 },
    { "from": "kick", "to": "compBass", "fromPort": 0, "toPort": 1 }
  ]
}
```

- Post-effect keying (duck reverb tail with pre-fader tap via wiretap):

```json
{
  "nodes": [
    { "id": "wtKick", "type": "wiretap", "params": { "path": "kick_key.wav", "enabled": true },
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main", "channels": 1 } ] } },
    { "id": "compRev", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } },
    { "id": "rev", "type": "reverb",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "vocals", "to": "rev",     "fromPort": 0, "toPort": 0 },
    { "from": "rev",    "to": "compRev", "fromPort": 0, "toPort": 0 },
    { "from": "kick",   "to": "wtKick",  "fromPort": 0, "toPort": 0 },
    { "from": "wtKick", "to": "compRev", "fromPort": 0, "toPort": 1 }
  ]
}
```

Examples:

```bash
# Print topo order without exporting (dry inspection)
./build/mam --rack examples/rack/demo.json --print-topo --validate examples/rack/demo.json

# Export and show meters (offline)
./build/mam --rack examples/rack/demo.json --wav demo.wav --meters

# Export, show topo order and meters together
./build/mam --rack examples/rack/demo.json --wav demo.wav --print-topo --meters
```

Transport and looping notes (realtime):
- Loop length is computed exactly from transport bars × bar duration to avoid boundary gaps.
- Transport events are emitted sample-accurately at segment boundaries to ensure continuous groove.

Timed realtime exit:

```bash
./build/mam --rack examples/rack/demo.json --quit-after 10
```

#### Channel adapters

Generalized N↔M adaptation guided by declared port channel counts:
- mono→stereo (or mono→N): average source to mono and duplicate across destination width
- stereo→mono (or N→mono): average to mono, then duplicate to graph width
- N→M where N,M>1: simple modulo mapping within graph channel width
- Multi-port routing (sidechain example):

```json
{
  "nodes": [
    { "id": "compA", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "pad",   "to": "compA", "fromPort": 0, "toPort": 0 },
    { "from": "kick1", "to": "compA", "fromPort": 0, "toPort": 1 }
  ]
}
```

Use `examples/rack/multiport_sidechain.json` and run:

```bash
./build/mam --rack examples/rack/multiport_sidechain.json --print-triggers --quit-after 3
```

Authoring:
- Declare `channels` in `ports.inputs[].channels` / `ports.outputs[].channels` to hint adapters. `0` or omitted means “use graph channels”.
- If neither side declares channels, the graph’s channel count is used.
 - Tip: Use `--print-ports` to confirm declared port layouts while designing graphs/sidechains.

## Graph configuration (JSON)

You can define instruments and their parameters using a JSON rack file and pass it with `--rack path.json`.
By default, relative paths are resolved against the current working directory. If a relative rack path isn’t found, the loader will also try `examples/rack/` and any additional directories listed in the `MAM_SEARCH_PATHS` environment variable (colon-separated).

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
- `connections` (array, optional): routed execution
  - Each item: `{ "from": "nodeId", "to": "nodeId", "gainPercent": 100 }`
  - The engine computes a topological order and executes nodes per block; per-edge gain is applied when summing upstream audio into a node’s input.

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
./build/mam --rack myrack.json
```

Render offline to WAV:

```bash
./build/mam --rack myrack.json --wav out.wav --sr 48000 --duration 2.0
```

### Transport (multi-patterns, swing, ramps)

Example:

```json
{
  "transport": {
    "bpm": 130,
    "lengthBars": 4,
    "resolution": 16,
    "swingPercent": 10,
    "swingExponent": 1.0,
    "tempoRamps": [ { "bar": 2, "bpm": 140 } ],
    "patterns": [
      { "nodeId": "kick1", "steps": "x...x..x..x..." },
      { "nodeId": "snr1",  "steps": "..x...x...x..." },
      { "nodeId": "hat1",  "steps": "x.x.x.x.x.x.x.x." }
    ]
  }
}
```

Notes:
- Unknown node types or params are warned (to be expanded later).
- Multi-node routing via `connections` is reserved for future versions.
- Swing: `swingPercent` shifts odd steps; shape it with `swingExponent` (1=linear; >1 softer feel; <1 stronger). See `examples/rack/swing_linear.json` and `examples/rack/swing_shaped.json`.

### Deterministic random seed

You can make random-driven nodes deterministic by setting a project-level seed:

```json
{
  "version": 1,
  "randomSeed": 123456,
  "nodes": [ /* ... */ ]
}
```

The engine seeds its global RNG once at load; use non-zero seeds for repeatable results across realtime and offline.

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
| `--rack` | path | — | Load a JSON rack (graph) file to build instruments/mixer |
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
  - Validation: enforce schema on `--validate`; check pattern targets/lengths; clamp params from `ParamMap`; warn for duplicate mixer inputs and dry+mixed double-count scenarios; basic type param sanity for delay/meter.
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

### Session commands (rack-time addressing)

- Author session-level commands under `session.commands[]` to affect racks during realtime playback or offline export.
- Address targets with the full, prefixed node id: `"nodeId": "<rackId>:<nodeId>"`.
- Timing options:
  - Absolute time: `{ "timeSec": 0.5, ... }` executes exactly at 0.5 s.
  - Musical rack-time: `{ "rack": "rackId", "bar": 1, "step": 5, "res": 16, ... }` resolves to absolute time using that rack’s transport (frames per bar), then executes at that time.
- Parameter addressing:
  - Prefer names: `{ "param": "F0" }` (resolved using the rack node’s type → ParamMap). Numeric `paramId` also supported.
  - Session command names are mapped to ids at load using graph specs (stable), not runtime types.
- Ordering at the block boundary: SetParam/SetParamRamp are applied before Trigger for sample-accurate results.
- Transport interaction and precedence:
  - Transport locks in the rack may subsequently set the same parameter within the bar; the last write at a given time wins.
  - For unambiguous audible proof, choose params the transport doesn’t touch (e.g., `GAIN`) or schedule session SETs after the transport step.
- Debugging and proof:
  - `--rt-debug-session` prints resolved musical times: `resolved musical command: rack=... bar=... step=... -> X.XXX sec`.
  - `--print-triggers` shows SET/RAMP/TRIGGER at their exact execution times in the audio callback.
  - Use `--quit-after` long enough to reach the scheduled events; or schedule near-start `timeSec` events for immediate proof.

Implementation notes:
- On realtime startup, the engine enqueues a globally time-sorted combined list (rack transport triggers + session commands) before starting audio, so no downbeat is missed.
- Session param names are mapped to ids with the node type taken from the rack’s graph specs (`kick`, `clap`, `tb303_ext`, `mam_chip`).

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
./build/mam --rack two_kicks.json --wav two_kicks.wav --sr 48000 --duration 8

# Oldschool breakbeat (~16s) with evolving params and parallel render
./build/mam --rack breakbeat.json --wav breakbeat.wav --sr 48000 --duration 16 --offline-threads 4
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

#### Loop diagnostics and trigger printing (realtime)

- `--verbose`: prints "Loop N" exactly at transport loop boundaries in the audio callback, before any triggers in that block.
- `--print-triggers`: prints sample-accurate set/ramp/trigger lines with absolute time, 1-based bar/step, node id, and command info.
  - Output includes an origin tag: `RACK` (transport/graph) or `SESS` (session command).
    - Example: `0.750000  SET  SESS  node=rack1:kick1   F0  800.000`
- `--rt-debug-session`: extra realtime session diagnostics (initial command preview, drained-per-block counts).
- `--dump-events`: offline/prepare-time dump of the command list with time/bar/step for debugging authoring issues.
- When `--print-triggers` is active, main-thread loop prints are suppressed to avoid duplicates; boundary printing remains precise in the callback.
- Bar/step indices are derived from `framesNow % loopLen` and `framesPerBar` for exact musical alignment.

Examples:

```bash
./build/mam --rack examples/rack/sidechain_mono_key.json --verbose --print-triggers
# Combine with per-node meters at realtime loop boundaries:
./build/mam --rack examples/rack/demo2.json --verbose --meters-per-node

# Realtime session with rack meters every ~1s:
./build/mam --session examples/session_minimal.json --meters

# Faster realtime meters (e.g., every 0.25s):
./build/mam --session examples/session_minimal.json --meters --meters-interval 0.25

# Emit NDJSON metrics (racks+buses):
./build/mam --session examples/session_minimal.json --meters --metrics-ndjson meters.ndjson --metrics-scope racks,buses
```

Command param addressing:

- You can specify parameters by numeric `paramId` (legacy) or by name using `"param": "F0"` etc.
- Names are resolved based on the node’s `type` using the Parameter Maps above.

## Threading strategy

- Realtime: single CoreAudio callback as conductor; future: optional worker threads via Audio Workgroup
- Offline: `JobPool` threads for parallel graph level execution (to be enabled)
- No allocation or locks on audio thread; preallocate all buffers/queues

## Performance and Performance Tuning

This project is designed for realtime playback and identical offline exports. This section explains how to measure performance, how to interpret the numbers, and what levers you can pull to stay glitch‑free in live setups.

### Realtime model

- Single realtime audio thread renders the graph in the CoreAudio callback.
  - No locks, no heap allocations, no syscalls in the callback.
  - Control is fed via a bounded SPSC command queue (pre‑synthesized events).
- Background threads handle non‑RT work (event generation, logging, file I/O).
- Latency‑adding nodes report algorithmic latency via `latencySamples()`; the engine prints a graph preroll estimate at first loop boundary.

### Measuring CPU in realtime and offline

- Flags:
  - `--cpu-stats`: print block CPU avg/max time (ms) and average/max load (% of deadline), block count, and xrun count.
  - `--cpu-stats-per-node`: additionally print per‑node average/max processing time (µs).
- Realtime:
  - Stats are printed at loop boundaries (regardless of `--verbose`) so they’re musically aligned.
- Offline:
  - A summary is printed at the end of the render.

Example (realtime):

```bash
./build/mam --rack examples/rack/acid303_sidechain_spectral_midSide.json --cpu-stats --cpu-stats-per-node
```

Interpretation:

- Avg block time ≈ average CPU cost per audio block.
- Max block time and max % reflect worst‑case spikes; if a block exceeds its deadline, `xruns` increments.
- Per‑node times help identify hot nodes.

Rules of thumb for live:

- Aim for avg CPU < 50% of the deadline and max < 80%, with `xruns=0` over long runs.
- Occasional spikes can be hidden by the device buffer, but sustained overruns will crackle.

### Tuning strategies (realtime)

- Lower algorithmic cost:
  - Reduce complexity of heavy nodes. For `spectral_ducker`:
    - Prefer `"applyMode": "multiply"` (cheaper) over `"dynamicEq"`.
    - Reduce the number of bands (e.g., 3 → 2) and/or lower `q`.
    - Reduce `lookaheadMs` (e.g., 6 → 3–4 ms) to cut memory traffic and delay.
    - Use `"stereoMode": "LR"` vs `"MidSide"`, or reduce `"msSideScale"`.
  - Keep sample rate and block size sane; higher SR costs more.
- Increase tolerance:
  - Raise the hardware/IO buffer size in your audio device settings (e.g., 256 → 512 frames).
  - Avoid background CPU spikes (browsers, spotlight indexing, app nap).
- Engine knobs:
  - Event horizon is pre‑synthesized (multi‑loop) to avoid RT stalls; keep it that way.
  - Flush denormals to zero; keep DSP branch‑light and SIMD‑friendly when possible.

### Parallelism and threading guidance

- Realtime: keep a single audio thread. Fanning out DSP to general worker threads inside the callback risks OS scheduling jitter and missed deadlines.
- If you must parallelize realtime, use a dedicated audio workgroup and a lock‑free job system with preallocated buffers; pin threads. Measure carefully—overhead can outweigh gains on small graphs.
- Offline: use `--offline-threads N` to leverage the parallel renderer when enabled.

### Latency and preroll

- Nodes can introduce algorithmic latency (e.g., lookahead). The graph’s preroll is computed and printed, and offline exports automatically include preroll so transients start fully formed.
- Realtime latency is the sum of device IO + algorithmic latencies; reduce lookahead on live rigs if needed.

### Troubleshooting checklist

- Watch `--cpu-stats` over several minutes. If `xruns` grows or max % approaches the deadline:
  - Reduce node complexity (bands, Q, lookahead; pick cheaper modes).
  - Increase device buffer size.
  - Prefer `stereoMode: "LR"` over `"MidSide"` for heavy patches.
  - Switch to offline export for pristine results.

### Quick recipes

- Lighten spectral ducking:
  - `"applyMode": "multiply"`, 2 bands, `q ≈ 0.8–1.0`, `lookaheadMs ≈ 3–4`.
- Preserve width while ducking lows:
  - `"stereoMode": "MidSide"`, `"msSideScale": 0.3–0.5`, bands centered at 60–120 Hz.

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

### Connections (routed processing)

- Each connection mixes upstream audio into a downstream node, then the graph mixes sinks to the master output.
- Per-edge fields:
  - `gainPercent` (wet): level of the source signal into the destination node. 100 = unity (0 dB), 50 ≈ -6 dB, 200 ≈ +6 dB.
  - `dryPercent` (dry tap): additional copy of the source mixed directly into the final output bus, bypassing the destination node. 0 = none, 100 = unity.
  - `fromPort`/`toPort` (future): multi-port routing indices (0 = main).
  - Dry/wet ergonomics: to prevent double-count, if a source is present in the mixer inputs, its dry taps are suppressed in the final mix. Prefer either a mixer input or a dry tap for the same source, not both.

Example:

```json
{
  "connections": [
    { "from": "kick1",  "to": "delay1", "gainPercent": 100, "dryPercent": 0 },
    { "from": "snr1",   "to": "delay1", "gainPercent": 80,  "dryPercent": 20 },
    { "from": "delay1", "to": "delay2", "gainPercent": 100, "dryPercent": 0 }
  ]
}
```

## Innovations (next ideas)

- Modulation matrix (realtime‑safe)
  - Status: foundational routing in place via `NodeFactory` routes and `ModMatrix` for LFOs; sample‑accurate event delivery implemented; per‑parameter smoothing scaffold is present.
  - Next: unify envelopes/sidechain as modulation sources; expose depth/summing/clamping; doc autogenerated param tables (partially done).

- Live JSON hot‑reload
  - Goal: validate JSON diffs, build new graph instance off thread, and swap state at exact loop boundaries without audio dropouts.
  - Plan: double‑buffer graph + port descriptors; command queue drains before swap; optional safety checks and rollback.

- Spectral processors
  - Status: spectral sidechain ducking implemented (IIR multiband), lookahead with reported latency, Dynamic EQ mode, Mid/Side, detector HPF; examples and docs provided.
  - Next: linear‑phase multiband mode (offline quality preset) and STFT per‑bin ducking; automation for band params; dithered oversampling where needed.

- Pattern intelligence
  - Goal: seed‑driven groove suggestions and param‑lock templates (deterministic via `randomSeed`).
  - Plan: library of humanized patterns; lock macros (bars/steps) emitted as JSON.

- Micro‑nodes / sandboxing
  - Goal: WASM or embedded DSL nodes, precompiled and allocation‑free at render time for safe extensibility.

- Tracing, metrics, and A/B diff
  - Status: `--cpu-stats` and `--cpu-stats-per-node` implemented; per‑block summaries at loop boundaries and offline end.
  - Next: optional JSON trace export (block, per‑node timings, loop marks); side‑by‑side render diff at boundaries.

- Tail advisor
  - Goal: analyze decay characteristics (delay/reverb) and suggest a tail (`--tail-ms`) unless explicitly overridden.

- Multi‑port ergonomics and validation
  - Status: ports declared in JSON and used for sidechain; adapters for N↔M channels in Graph.
  - Next: `--print-ports` CLI; validation for dry/wet double‑count; channel layout hints.

- Offline parallelism (timeline)
  - Status: baseline single‑thread timeline and parallel prototypes; progress + speedup printing; CPU stats in offline.
  - Plan: topo‑level scheduler with persistent thread pool; segment (event‑split) → level execution with barriers; heuristic gating to avoid parallel overhead on small graphs; stable reduction order option for deterministic summing.

## Diagram export (Mermaid)
You can print Mermaid diagrams for documentation and paste them into README/wiki.

- Session (racks/buses/routes):
```bash
./build/mam --export-mermaid-session examples/session/session_minimal.json > session.mmd
```

-- Rack (nodes/connections/mixer):
```bash
./build/mam --export-mermaid-graph examples/rack/demo.json > graph_topo.mmd
```

Render in GitHub/Markdown using a mermaid code block:
```markdown
```mermaid
(flowchart content here)
```
```

## Session crossfaders (realtime)

You can define crossfaders at the session level to blend racks with equal‑power or linear laws. Crossfaders are applied in the realtime session mixer path and can be LFO‑driven.

JSON example:
```json
{
  "xfaders": [
    {
      "id": "main",
      "racks": ["rack1", "rack2"],
      "law": "equal_power",           // or "linear"
      "smoothingMs": 10.0,            // slew for click‑free moves
      "lfo": { "wave": "sine", "freqHz": 0.25, "phase01": 0.0 }
    }
  ]
}
```

Runtime usage:
```bash
./build/mam --session examples/session_minimal_xfaders.json --meters --meters-interval 0.25
# Optional NDJSON metrics (includes xfader events):
./build/mam --session examples/session_minimal_xfaders.json --meters \
  --meters-interval 0.25 --metrics-ndjson xfader.ndjson
```

Notes:
- Equal‑power uses gA=cos(pi/2·x), gB=sin(pi/2·x) for smooth perceived loudness.
- Set both rack base gains to comparable values for a balanced crossfade.
- LFO is optional; future versions will expose a param `xfader:<id>:x` for automation.

## JSON file kinds (discriminator)

Add a top-level field `kind` to future-proof loading and validation:

- Rack (graph) files:

```json
{
  "kind": "rack",
  "version": 1,
  "nodes": [ /* ... */ ]
}
```

- Session files:

```json
{
  "kind": "session",
  "version": 1,
  "racks": [ /* ... */ ]
}
```

Behavior:
- If `kind` is present and mismatches the loader, the app errors clearly.
- If `kind` is missing, the loader infers and prints a warning (backward compatible).
- `--schema-strict` can be used in conjunction with schemas to enforce `kind`.

### Path resolution policy (racks and sessions)

- Racks (`--rack path.json`):
  - If `path.json` is absolute or exists relative to the current working directory, it’s used as-is.
  - Otherwise, the loader tries `examples/rack/path.json`.
  - Finally, it searches each directory listed in `MAM_SEARCH_PATHS` (colon-separated), prepending each to the given relative path.

- Sessions (`--session path.json`):
  - `racks[].path` inside the session is resolved relative to the session file’s directory if it’s a relative path. This makes sharing a self-contained folder (one session + its racks) work without installing into global folders.
  - Absolute rack paths continue to work as-is.
  - Realtime session startup enqueues a globally time-sorted combined list (rack transport triggers + session-level commands) before starting audio so the first downbeat is never missed.
