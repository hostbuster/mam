## Offline Topological Scheduler — Plan

### Current state
- CLI flag `--offline-scheduler topo|baseline` switches offline path.
- `OfflineTopoScheduler` renders via graph.process with command‑aware segmentation (parity with baseline timeline renderer for correctness).
- Topo levels: built from `connections` (serial execution) and printed when `-v` is set.
- Block size knob: `--offline-block N` controls topo scheduler block size.
- Topo flag aliases: `--topo-scheduler`, `--topo-offline-blocks`, `--topo-verbose` for grouped discoverability.
- Determinism: connections are stably sorted once in topo path and applied to the Graph, ensuring fixed per‑edge reduction order.
- Per‑edge mixing: moved into topo scheduler (stable accumulation, multi‑port, dry tap suppression, mixer gains, master/soft‑clip), parity preserved.
- BufferPool reuse: active for per‑node buffers per segment; deeper lifetime‑based reuse across levels pending.
- Parity: sample renders match baseline (peak/RMS, sample‑exact in checks so far).
- Not yet implemented: lifetime‑based BufferPool reuse, parallel levels, metrics, validation, parity tests suite.

### Gaps / TODOs
- Topological execution
  - Build levels from `GraphSpec.connections`; detect cycles and isolated nodes.
  - Execute level by level (no RT constraints), honour declared ports.
- Buffer reuse / aliasing
  - Track per‑node output lifetimes across levels/segments; reuse buffers from `BufferPool`.
  - Prevent alias hazards when a node reads and writes overlapping buffers.
- Per‑edge routing + stable reduction
  - Apply `gainPercent`/`dryPercent` per connection.
  - Multi‑port fan‑in/out (use declared `ports.inputs[].channels` / `ports.outputs[].channels`).
  - Deterministic summation order for floating‑point stability (stable reduction).
- Parallelism (optional)
  - Level‑parallel execution via `JobPool` with per‑level barrier.
  - Heuristics to skip parallelism for tiny graphs to avoid overhead.
- Determinism
  - Fixed traversal/summation order; ensure parallel and serial renders match sample‑exactly.
  - Seed handling for randomness.
- Metrics & observability
  - Per‑node CPU timings (avg/max) and overall speedup; optional JSON trace events.
  - Progress prints `[offline-topo]` like timeline path.
- Scheduler policy
  - Configurable block size; share command segmentation logic; precompute segment split points once.
- Validation & safety
  - Cycle detection; port/channel mismatches; dry/wet double‑count prevention parity with runtime mixer path.
- Tests
  - Parity tests vs. baseline (sample‑exact), stress graphs, deterministic parallel runs.

### Implementation plan (milestones)
1) Topo levels (serial, no parallel)
   - Build graph DAG from `connections`; Kahn topo sort → levels.
   - Execute nodes in level order; keep using graph.process temporarily for mixing.

2) Edge mixing in scheduler
   - DONE: stable connection order + mixing in scheduler (per‑node accumulators, dry/wet, multi‑port, stable reduction, mixer/master/soft‑clip).

3) BufferPool reuse/aliasing
   - NEXT: Liveness via last‑use analysis per buffer; reuse freed buffers between levels/segments.
   - Guard against alias hazards for nodes that read/write the same target.

4) Parallel levels
   - Execute nodes within a level via `JobPool`; barrier at level end.
   - Heuristics: min width/frames to enable; thread count from `--offline-threads`.

5) Determinism & tests
   - Enforce fixed sort keys; add parity tests vs. baseline timeline renderer.
   - Verify parallel == serial outputs (bit‑exact) across sample graphs.

6) Metrics & tracing
   - Capture per‑node timings; print CPU summary; optional `--trace-json` spans from topo path.

7) Flags & docs
   - Expose block size and parallelism knobs under `--offline-scheduler topo`.
   - README section documenting scheduler modes and trade‑offs.

### Work breakdown (next steps)
- Introduce BufferPool lifetimes and reuse in the topo loop (last‑use analysis, free lists per level).
- Add a `topo_debug` mode to print buffer lifetime/reuse decisions.
- Parity tests vs. baseline and sample‑exact checks; then level‑parallel prototype via JobPool.

### Definition of done
- For sample racks: topo serial == baseline timeline (sample‑exact).
- Enabling parallel levels does not change samples and yields measurable speedup on wide graphs.
- README/docs updated; CLI help shows scheduler options.

