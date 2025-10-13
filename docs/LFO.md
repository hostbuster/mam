## LFO Modules and Modulation Matrix

This page explains how to author and use the general-purpose LFO system and the realtime-safe modulation matrix. It mirrors the style of `Sidechain.md` and includes concepts, authoring basics, examples, validation, and best practices.

### Concept

- LFOs are low-frequency oscillators that generate a periodic bipolar signal [-1, +1].
- The modulation matrix routes sources (LFOs) to destinations:
  - Node parameters (e.g., `GAIN`, `F0`, `AMP_DECAY_MS`)
  - Other LFO properties (currently frequency): `LFO.<id>.freqHz`
- All modulation is zero-alloc and realtime-safe; smoothing and clamping applied to avoid artifacts.

### Authoring basics

- Declare LFOs and routes under a node’s `mod` block:

```json
{
  "id": "snr1",
  "type": "clap",
  "params": { "ampDecayMs": 180, "gain": 0.85 },
  "mod": {
    "lfos": [
      { "id": 1, "wave": "sine", "freqHz": 4.0, "phase": 0.0 },
      { "id": 2, "wave": "triangle", "freqHz": 0.2, "phase": 0.0 }
    ],
    "routes": [
      { "sourceId": 1, "destParam": "GAIN", "min": 0.3, "max": 1.1, "map": "linear" },
      { "sourceId": 2, "destParam": "LFO.1.freqHz", "min": 0.5, "max": 8.0, "map": "exp" }
    ]
  }
}
```

- Supported waves: `sine`, `triangle`, `saw`, `square`.
- `depth` scales the bipolar source. `offset` adds a constant before summing.
- Destinations:
  - Any node param by name (e.g., `GAIN`, `F0`)
  - LFO frequency target as `"LFO.<id>.freqHz"`

Mapping and curves:
- Instead of `depth`/`offset`, you can specify a target range and curve:
  - `min` / `max`: maps source [-1,+1] to [min,max]
  - `map`: `linear` (default) or `exp` (uses t^2 curve)
  - If `min < max`, this mapping overrides `depth`/`offset` for that route

Smoothing:
- LFO frequency changes are slewed internally to avoid zipper noise when modulated.

### Per-step modulation via transport locks

You can change LFO frequency per step with transport param locks using pseudo-parameters:

- For clap:
  - `LFO1_FREQ_HZ` (id 101)
  - `LFO2_FREQ_HZ` (id 102)

Example:

```json
{
  "transport": {
    "bpm": 126,
    "lengthBars": 1,
    "resolution": 16,
    "patterns": [
      { "nodeId": "snr1", "steps": "....x.......x...",
        "locks": [
          { "step": 0,  "param": "LFO1_FREQ_HZ", "value": 4.0 },
          { "step": 4,  "param": "LFO1_FREQ_HZ", "value": 8.0 },
          { "step": 8,  "param": "LFO1_FREQ_HZ", "value": 12.0 },
          { "step": 12, "param": "LFO1_FREQ_HZ", "value": 16.0 }
        ]
      }
    ]
  }
}
```

### Examples

- Tremolo with evolving rate (used in `examples/demo.json` on clap):
  - LFO1 (4 Hz) → `GAIN` depth 0.6
  - LFO2 (0.2 Hz) → `LFO.1.freqHz` depth 1.5 offset 0.5
  - Transport locks: per-step `LFO1_FREQ_HZ` to 4 / 8 / 12 / 16 Hz

- Kick tone wobble with nested modulation:
  - LFO1 (2 Hz) → `F0` depth 40
  - LFO2 (0.2 Hz) → `LFO.1.freqHz` depth 1.5 offset 0.5

### Validation and limits

- LFO frequency is clamped to a safe range in-engine (min ~0.01 Hz).
- Pseudo-parameters for locks are defined in ParamMap (e.g., clap ids 101/102), enabling name-based authoring.

### Best practices

- For frequency-like targets, prefer slow, exponential-like sweeps; avoid large per-sample jumps.
- Use modest `depth` on audio-rate-sensitive params; larger `depth` is suitable for `GAIN` or send-level style effects.
- Combine transport locks with slow LFO routing for musical variety that remains deterministic.

### Reference

- See `examples/demo.json` for a working LFO-on-LFO + per-step-rate demo.
- See `docs/schema.graph.v1.json` for graph structure.
- Node-specific parameter names and ranges: `docs/ParamTables.md`.


