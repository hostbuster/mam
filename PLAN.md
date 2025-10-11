# Modular Upgrade Plan

## Goals
- Improve modularity, testability, and build times by splitting into focused libraries
- Centralize parameter definitions and named-param mapping
- Establish a foundation for transport, scheduling, and observability

## Status

### Done
- Modular split: `mam_core`, `mam_dsp`, `mam_io`, `mam_render` (INTERFACE); executable `mam`
- Build/tooling: warnings-as-errors option, ASan/UBSan/LTO flags, CMake presets
- Parameters: `ParamMap` metadata per node; `ParameterRegistry` with step/linear/exp smoothing; auto-doc generator (`gen_params` → `docs/ParamTables.md`)
- JSON graph: loader with commands and transport; schema tracked in `docs/schema.graph.v1.json`
- Realtime: `RealtimeGraphRenderer` sub-block command processing; `TransportNode` integrated for live pattern emission
- Transport param-locks implemented in realtime and offline (pattern `locks`)
- Offline: graph/timeline/parallel renderers; `--offline-threads` CLI
- Mixer: per-input gains + master gain with optional soft clip
- CLI: `--graph`, `--validate`, `--list-nodes`, `--list-params`, `--quit-after`
- Validation: named-param resolution; transport pattern target/steps checks
- Scaffolds: `BufferPool` and `OfflineTopoScheduler` for future topo/latency work

### Next up
- Transport: finalize swing timing nuances and ramp shapes; ensure realtime/offline parity
- Parameters: complete name→id auto-generation and stricter load-time validation/clamping across all nodes
- Offline scheduler: add topological levels (when edges exist), buffer aliasing, per-node latency reporting and preroll; integrate scheduler path
- Observability: Meter/Wiretap nodes, lightweight JSON trace, per-node perf counters
- Validation & tooling: JSON Schema validation in `mam --validate`, golden renders, fuzzed command streams, CI presets; integrate clang-tidy checks

### Design notes

1) Transport param-locks
- JSON: `transport.patterns[i].locks[]` entries with `{ step, param|paramId, value, rampMs? }`.
- Realtime: pre-enqueue locks or emit via `TransportNode` at step boundary; apply before processing the sub-block. Inline node locks use `paramId`.
- Offline: generate locks into the timeline alongside triggers; preserve sample-accurate order. Top-level transport supports names.

2) Offline topo scheduler
- Topological sort when connections exist; process levels in parallel using `JobPool`.
- BufferPool for scratch; alias analysis to reuse buffers; optional interleaved/planar conversions.
- Per-node latency API with preroll to align rendered audio; tests compare to baseline within -120 dBFS.

3) Validation/tooling
- JSON Schema enforced in `--validate`; include checks for pattern lengths, unknown nodes/params, and ParamMap clamping.
- CLI printing for params/nodes; golden renders and fuzzed event streams in CI presets.

### Proposed acceptance criteria
- Transport param-locks: JSON allows `patterns[i].locks` with `{param|paramId, value|rampMs}`; both realtime and offline render identical automation.
- Schema validation: `--validate` reports unknown node types/params, invalid ranges, and bad pattern targets/lengths; exits non-zero on failures.
- Offline scheduler: rendering via scheduler matches baseline buffer within -120 dBFS; parallel mode does not deadlock; lints clean.
- Param maps: all instrument params documented in `docs/ParamTables.md`; name→id mapping covers all used names in examples.

## Phased Roadmap

### Phase 1: Build and structure
- Split into static libraries:
  - mam_core: Graph, Node, Command, ParameterRegistry, JobPool, GraphConfig
  - mam_dsp: KickSynth, ClapSynth
  - mam_io: AudioFileWriter
- `mam` executable links these libs; warnings-as-errors per-target; presets intact

### Phase 2: Parameters
- Create ParamMap tables per node type: {id, name, unit, min, max, def, smoothing}
- Use ParamMap for:
  - Named param resolution (JSON `param` → id)
  - Clamping and defaulting of `params` in node creation
  - Auto-doc generation (README section)

### Phase 3: Transport
- Introduce TransportNode emitting triggers/param-locks; support tempo ramps, swing
- Keep offline generator as fallback; unify JSON schema

### Phase 4: Offline scheduler
- Topological levels and buffer pool with aliasing
- Per-node latency reporting and preroll

### Phase 5: Observability
- Wiretap/Meter nodes; lightweight tracing (JSON trace), perf counters per node

### Phase 6: Validation & tooling
- JSON Schema validation and `mam validate` CLI command
- Golden renders and fuzzed command streams; CI presets

## Deliverables per phase
- CMake targets, docs updates, build artifacts
- ParamMap headers and integration; README auto-generated table snippet
- TransportNode and schema updates; examples updated
- Scheduler and perf improvements; metrics dump helpers

## Risks
- Public API churn across phases—mitigate with type-safe wrappers and incremental PRs
- Realtime safety regressions—guard with asserts and compile-time options

## Success criteria
- Clean target separation; fast incremental builds
- Named params across all JSON examples; validated/clamped inputs
- Transport patterns and ramps function identically realtime/offline
