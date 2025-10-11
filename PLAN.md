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
- Export UX: auto-duration (transport bars/ramps or last command) with tail; flags `--duration`, `--bars`, `--loop-count`, `--tail-ms`
 - Export normalization: `--normalize` (-1 dBFS) and `--peak-target dB`, with printed pre-/post-peak and applied gain
- Mixer: per-input gains + master gain with optional soft clip
- Routing: topological execution via `connections` with per-edge gains; cycle checks; per-edge `dryPercent` tap to master; `fromPort`/`toPort` fields scaffolded
 - Latency & preroll: nodes report latency; offline export adds preroll automatically to capture full transients
- CLI: `--graph`, `--validate`, `--list-nodes`, `--list-params`, `--quit-after`
- Validation: named-param resolution; transport pattern target/steps checks; duplicate mixer input detection; dry+mixed double-count warnings; type param sanity (delay, meter)
- Scaffolds: `BufferPool` and `OfflineTopoScheduler` for future topo/latency work

### Top priority (next)
- Multi-port routing and port descriptors
  - Declare per-node ports (inputs/outputs with index/name/type/channels/role) in schema and JSON.
  - Enforce `fromPort`/`toPort` against node descriptors in validation.
  - Implement per-port accumulation in `Graph` (MVP collapse done); expose hooks for port-aware nodes.
  - Channel adapters (mono↔stereo) with validation warnings.
- JSON Schema validation in CLI
  - Integrate a draft 2020-12 validator and validate against `docs/schema.graph.v1.json` in `--validate`.
  - Keep semantic checks (cycles, ranges, param maps) after schema pass.

### Next up
- Routing engine (Phase 4 MVP → full):
  - Add explicit audio ports and per-edge gains; execute graph strictly by topological order from `connections` (both offline and realtime). Implement multi-port mixing.
  - Replace global-insert effects with per-connection processing; maintain `MixerNode` as a terminal node.
  - Report per-node latency; add preroll and optional compensation (offline first).
- Transport timing: finalize swing nuances and ramp curves; add seek and loop ranges.
- Parameters: complete name→id auto-generation; stricter load-time validation/clamping across all nodes.
- Observability: expand Meter/Wiretap nodes; JSON perf trace; per-node counters.
- Validation & tooling: JSON Schema validation in `mam --validate` (integrate draft 2020-12); golden renders; CI with sanitizers and clang-tidy.

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

### Phase 4: Routing and scheduler
- Ports, per-edge gains, and validated DAG `connections`
- Topological execution order and buffer reuse/aliasing
- Per-node latency reporting and preroll; offline compensation first

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
