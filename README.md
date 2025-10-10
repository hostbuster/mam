# MAM — Maximum Audio Madness

A modern C++17 audio playfield. The first module is a small macOS command-line app that synthesizes a kick drum and streams it directly to the system’s default output using CoreAudio’s Default Output Audio Unit.

This repository will grow into a platform for rapid prototyping of audio ideas, with a focus on maintainability, performance, and clarity guided by the C++ Core Guidelines.

## Features

- **CoreAudio output**: Low-latency render callback to the default device
- **Procedural kick drum**: Pitch and amplitude envelopes, onset click, gain
- **One-shot or tempo-synced loop**: Trigger once or repeat at BPM until Ctrl-C
- **Safe defaults**: Float32, non-interleaved stereo; uses actual device sample rate
- **Portable C++17**: Straightforward, clean code with minimal dependencies
 - **Modular architecture**: Separated DSP, realtime, offline, and file I/O modules

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
```

### Offline rendering

Render without using CoreAudio to an uncompressed audio file. Defaults to 48 kHz float32 WAV.

```bash
./build/mam --wav out.wav --duration 2.0                    # 2 seconds at 48 kHz float32 WAV
./build/mam --wav out.wav --sr 44100 --pcm16                 # 44.1 kHz 16-bit PCM WAV (compat)
./build/mam --wav out.aiff --format aiff --bitdepth 24       # AIFF 24-bit PCM
./build/mam --wav out.caf --format caf --bitdepth 32f        # CAF float32
```

## Graph configuration (JSON)

You can define instruments and their parameters using a JSON graph file and pass it with `--graph path.json`.

### Schema (v1)

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

Notes:
- Unknown node types or params are ignored with a warning (to be expanded later).
- Future versions will allow multi-node routing via `connections`.

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
| `--help`, `-h` | flag | — | Print usage |

Notes:
- Audio runs at the device’s sample rate; we request 48 kHz and accept what the device reports.
- Stream format is float32, stereo, non-interleaved (separate buffers per channel).

## Project Structure

- `CMakeLists.txt` — project and Apple framework linking
- `src/main.cpp` — CLI/argument parsing, dispatch to realtime or offline
- `src/dsp/KickSynth.hpp` — pure DSP kick synth (reusable in all renderers)
- `src/realtime/RealtimeRenderer.hpp` — CoreAudio default output and render callback
- `src/offline/OfflineRenderer.hpp` — offline buffer rendering at a target sample rate
- `src/io/AudioFileWriter.hpp` — ExtAudioFile-based writer (WAV/AIFF/CAF; 16/24/32f)

## Architecture Overview

- **Realtime**: `RealtimeRenderer` opens the Default Output Audio Unit and installs a render callback. The callback pulls samples from `KickSynth`.
- **Render Callback**: Lives inside `RealtimeRenderer`. Pure synthesis (no I/O), non-blocking, no heap allocation, no locks.
- **Synthesis**: Exponential amplitude and pitch envelopes, sine oscillator, optional onset click. Left and right receive the same mono signal for now.
- **Timing**: In loop mode, retriggering is based on `BPM` → `framesPerBeat`; one-shot mode just plays for `--duration` seconds.
- **Offline Path**: `OfflineRenderer` calls the same synthesis per-sample into an interleaved buffer; `AudioFileWriter` uses ExtAudioFile to emit WAV/AIFF/CAF at 16/24-bit PCM or 32f.

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
