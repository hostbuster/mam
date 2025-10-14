# MAM Audio Core

## Vision
An innovative, realtime-first audio core that unifies live performance and studio workflows. Individual JSON graphs become self-contained “racks,” multiple racks can run in sync as a Session, and a Timeline Recorder captures parameter changes and triggers for deterministic re-playback and editing.

## Core Concepts
- Racks: Self-contained graphs loaded from JSON (existing mam graphs). Each rack has its own node graph, ports, param map, transport, and local state.
- Session: A collection of racks plus a master transport, clock, routing, and global automation lanes. Sessions define inter-rack sync, shared sidechains/buses, and scene changes.
- Timeline: A record/playback lane for commands (Trigger, SetParam, SetParamRamp), scenes, and markers. Multitrack: per-rack and global lanes.
- Scenes: Snapshots of rack/session state (params, routing, mutes/solos) with smooth transition policies.
- Controller Layer: Binds MIDI/OSC/HID/web to high-level actions. Mappings are saved and recordable to the Timeline.

## Files & APIs
- Rack JSON (existing): Unchanged. Treated as a module with inputs/outputs and optional exports/imports of control endpoints.
- Session JSON (new): References rack JSONs, defines master clock, rack start offsets, buses, sidechains, master inserts, and controller mappings.
- Timeline JSON (new): Sparse, append-only event log; optionally chunked and indexed. Supports destructive/clip-based editing.
- Live API: C++17 host API + C ABI for plugins/embedders. Control via gRPC/WebSocket for remote UIs. Real-time safe command queue as today.

## Scheduling Model
- Master clock: One authoritative transport (bars/beats/frames) with sample-accurate alignment.
- Rack clocks: Derived from master with per-rack offset and stretch (for creative desync/time-warp).
- Event domains: Session-level (global), rack-level (per graph), node-level. All end up as timestamped commands in a unified priority queue per audio device.
- Lookahead: Optional per-rack/event-class lookahead with bounded latency budget.
- Priority: Time-critical (triggers, ramps at boundaries) first; non-critical (long ramps) batched.

## Audio Graph & Routing
- Per-rack graphs instantiated independently, mixed via Session buses.
- Sidechain across racks: Named sidechain buses; latency-compensated.
- Mid/Side and multichannel: Session bus supports MS and N channels; racks declare channel config.
- Inserts/Sends: Session-level inserts (e.g., limiter) and per-rack sends with pre/post options.

## Recording & Playback
- Record modes:
  - Live overdub: Append commands with source tags (MIDI, UI, automation).
  - Write/Touch/Latch: DAW-style automation write for params.
  - Quantize window: Optional, per-lane, for live-tightness.
- Storage:
  - Chunked event files with index (time → file offset). Background compaction.
  - Lossless, monotonic timestamps (uint64 frames) with session sample rate.
- Playback:
  - Merge: Session+Rack+Timeline lanes merged into one RT queue with dedup and ordering (Set before Trigger).
  - Punch in/out and loop regions. Scene recalls are captured as parameter blocks.

## Threading & Performance
- Realtime thread: Single audio callback per device; per-rack process calls are parallelized only if guaranteed bounded and lock-free (optional).
- Feeders: One SPSC producer per device; all pre-scheduled events come from a single producer thread to avoid ABA hazards.
- Persistent thread pool: For offline/session rendering and non-RT analysis (metering exports, spectral features).
- Preallocation: Per-thread/rack buffers, tiled processing, and level-based scheduling in each rack’s `Graph`.
- CPU Telemetry: Per-rack and per-node timing, XRuns, load %. Optional RT-friendly periodic logging.

## Determinism & Parity
- Identical outputs between realtime and offline given same sample rate and flags.
- Strict ordering: SetParam/SetParamRamp applied before Trigger at the same sample.
- Loop integrity: Step-accurate reproduction with transport drift guarded by integer math on frames.
- Latency accounting: Session reports total IO→IO latency; applies compensation in sidechains/buses.

## Control & Mapping
- Unified mapping objects:
  - Input: MIDI CC/Note, OSC paths, HID, Web.
  - Transform: Scale, curve, deadzone, smoothing, quantize.
  - Target: Session/Rack param path (e.g., rack.id:paramName) or high-level actions (SceneNext).
- Bidirectional: State reflects on controllers with feedback (lights, motor faders, UI).

## Scenes & States
- Scene = snapshot of: selected rack params, mixer/bus state, sends, mutes/solos, spectral ducker profiles, etc.
- Transition policies: Instant, crossfade, morph (time-based param ramps), quantized to bars/steps.
- Scene stack: Push/pop for momentary effects.

## Safety & Reliability
- RT-safe: No locks, no allocations in callback. SPSC queues only.
- Watchdog: Detects feeder starvation, xrun bursts, late controllers; raises hints for mitigation.
- Autosave: Timeline chunks flushed periodically; crash-safe indexes.

## Extensibility
- Nodes/racks as plugins: Stable C ABI; DSP in shared libs with versioned caps. Sandboxed optional.
- Metadata: Racks declare exported controls and status endpoints for session UI.
- Scripting: Lua/WASM for non-RT logic (arrangements, generators) feeding RT commands.

## File Formats (sketch)
- Session JSON:
  - sampleRate, buffer, clockSource
  - racks: [{ id, path, startOffsetFrames, gain, channelConfig }]
  - buses: [{ id, channels, inserts }]
  - routes: [{ from: rack.out, to: bus.in, gain }]
  - sidechains: [{ from: rack.out, to: rack.effect.key, latencyComp: true }]
  - scenes: [{ id, params, transitions }]
  - mappings: [{ input, transform, target }]
- Timeline JSON:
  - sampleRate, timebase
  - lanes: [{ id, type: rack|session, events: [ { t, nodeId, type, pid|pname, val, rampMs, src } ] }]
  - markers: [{ t, label }]

## Live Workflow
- Prep: Load session, check telemetry, set safety margins.
- Soundcheck: Snapshot SceneA.
- Performance: Record automation + triggers; use quantized scene transitions.
- Recall: Scenes per song section; punch-in timeline corrections live if needed.

## Studio Workflow
- Compose with racks as modular instruments/effects.
- Record multi-take timelines; comp via scene snapshots.
- Offline export: Parallel, deterministic renders with progress and speedup report.

## Roadmap
1) Session container/runtime, rack embedding, buses, routes.
2) Timeline recorder/player with chunked storage and indexes.
3) Controller mapping engine (MIDI/OSC/Web), with feedback.
4) Scene system with snapshot/morph and quantized transitions.
5) Latency-compensated sidechains across racks and master inserts.
6) Deterministic offline renderer for sessions; per-rack stems export.
7) Networked control and distributed audio devices (future).

## Why this design
- Live-first: robust RT guarantees, scene safety, and minimal-latency control.
- Studio-strong: deterministic playback, editability, offline parity.
- Modular: existing graphs drop in as racks without change.
- Extensible: clean surfaces for new nodes, racks, controllers, and UIs.
