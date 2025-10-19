## Offline Topological Scheduler

This document describes the offline topological (DAG) scheduler used for non‑realtime renders, its flags, metrics, heuristics, and implementation status.

### Overview

The topo scheduler executes graph nodes in topological order with command‑aware segmentation. It provides:

- Deterministic, per‑edge mixing with stable connection ordering
- Multi‑port accumulation (e.g., compressor sidechain on port 1)
- Lifetime‑ID `BufferPool` reuse and early release on last downstream use
- Optional level‑parallel execution via a `JobPool` (barrier per level)
- Metrics and tracing for performance diagnostics
- SHA1 parity checks to ensure results match the baseline path

Enable it with either flag:

```bash
--topo-scheduler topo   # (preferred)
# or
--offline-scheduler topo
```

### Current Implementation

- Topology and connections
  - Connections are stably sorted once to ensure deterministic reduction order.
  - Topological levels are built by Kahn’s algorithm.
- Per‑edge routing/mixing
  - Gain/dry percent applied per connection; mixer inputs and master gain respected.
  - Multi‑port fan‑in: port 0 is the main input; sidechain on port 1 for compressor.
- Buffer reuse
  - `BufferPool` with lifetime IDs; early release when the last downstream consumer is satisfied.
- Parallelism and heuristics
  - `--topo-threads N` enables level‑parallel execution (JobPool).
  - Defaults to hardware concurrency when not specified.
  - Heuristics: parallelize only when level width ≥ `--topo-min-width` and segment frames ≥ `--topo-min-seg-frames`.
  - Threads are clamped to level width to avoid oversubscription.
- Metrics
  - `--topo-metrics` (alias for `--cpu-stats` + `--cpu-stats-per-node`).
  - Prints `[offline-topo]` block averages/max and parallel barrier timing; per‑node avg/max µs.
- Tracing
  - `--topo-trace path.json` writes Chrome/Perfetto compatible spans for node executions and level barriers.
- Parity
  - Topo serial/parallel outputs are bit‑exact vs baseline (timeline) on sample racks.
  - `tools/topo_parity.sh` automates SHA1 parity checks (includes metrics/trace flags).

### Flags

- Scheduler selection
  - `--topo-scheduler topo|baseline` (alias: `--offline-scheduler ...`)
- Block size
  - `--topo-offline-blocks N` (alias: `--offline-block N`, min 64; default 1024)
- Parallelism
  - `--topo-threads N` (0 → default to hardware concurrency)
  - `--topo-min-width N` (default 2)
  - `--topo-min-seg-frames N` (default 128)
- Metrics
  - `--topo-metrics` (enables `--cpu-stats` and `--cpu-stats-per-node`)
- Tracing
  - `--topo-trace path.json` (Chrome/Perfetto JSON)
- Verbose topology
  - `--topo-verbose` (prints levels once at start)

### Example

```bash
./build/mam --rack examples/rack/acid303_sidechain_spectral.json \
  --wav out.wav --sr 48000 --duration 2 \
  --topo-scheduler topo --topo-threads 4 \
  --topo-min-width 2 --topo-min-seg-frames 128 \
  --topo-metrics --topo-trace topo_trace.json --sha1
```

### Output Interpretation

- `[offline-topo] CPU block avg/max` — average/max time per segment (ms) and load % of segment budget; `blocks` = segments processed; `overruns` = segments exceeding budget.
- `barrier avg/max (levels, parallel)` — average/max time spent at level barriers; `levels` counted; `parallel` levels executed with parallel work.
- `node=... avg/max` — per‑node average/max processing time (µs).
- Export summary — frames, duration, sample rate, channels, format, peak/RMS, preroll.
- `SHA1(samples)` — hash of rendered samples for parity checks.
- A zeroed “CPU block … (baseline)” section may follow; it’s the baseline Graph stats. When using topo, these counters are unused and print zeros.

### Parity Testing

Run the helper script to assert topo serial/parallel == baseline:

```bash
bash tools/topo_parity.sh ./build/mam examples/rack/acid303_sidechain_spectral.json 2 48000 42
```

### Roadmap / Remaining Work

- Buffer reuse across segments/levels beyond early release (deeper lifetime analysis)
- Validation: cycle detection; port/channel mismatches; dry/wet double‑count prevention parity with runtime
- Optional audio meters per node (peak/RMS) in topo path
- Stress graphs and determinism tests at scale; extended trace with thread IDs


