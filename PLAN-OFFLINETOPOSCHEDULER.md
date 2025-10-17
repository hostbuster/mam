## Offline Topological Scheduler — Plan

### Current state
- CLI flag `--offline-scheduler topo|baseline` switches offline path.
- `OfflineTopoScheduler` renders via graph.process with command‑aware segmentation (parity with baseline timeline renderer for correctness).
- Not yet implemented: real topo levels, buffer reuse/aliasing, per‑edge routing, parallelism, metrics, deterministic stable reductions, validation.

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
   - Introduce per‑node input accumulators; apply `gainPercent` and multi‑port routing; dry taps.
   - Stable reduction order (sorted by `from`/`to` ids).

3) BufferPool reuse/aliasing
   - Liveness via last‑use analysis per buffer; reuse freed buffers between levels.
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
- Implement topo builder and levels API (serial execution).
- Swap mixing from graph.process to explicit per‑edge accumulation with stable order.
- Introduce BufferPool lifetimes and reuse in the topo loop.
- Add a hidden `--offline-scheduler topo_debug` to print levels and buffer reuse decisions (dev aid).

### Definition of done
- For sample racks: topo serial == baseline timeline (sample‑exact).
- Enabling parallel levels does not change samples and yields measurable speedup on wide graphs.
- README/docs updated; CLI help shows scheduler options.

