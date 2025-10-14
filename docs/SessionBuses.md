## Session Buses, Routes, and Sidechains

This document introduces session-level buses so multiple racks can be mixed, routed to shared effects, and sidechained.

### Concepts
- Buses: Named summing/mixing points with N channels.
- Routes: Connections from `rack.output` to `bus.input` with gain and optional pre/post options.
- Inserts: Per-bus processing chains (e.g., limiter, spectral_ducker, reverb).
- Sidechains: Named key inputs to inserts sourced from any rack/bus.

### Session JSON (draft)
```json
{
  "version": 1,
  "sampleRate": 48000,
  "channels": 2,
  "racks": [ { "id": "rack1", "path": "examples/mamic_song.json" } ],
  "buses": [
    { "id": "main", "channels": 2, "inserts": [
      { "type": "spectral_ducker", "id": "specduck", "params": { "mix": 0.8 },
        "sidechains": [ { "id": "key", "from": "rack2" } ] }
    ]}
  ],
  "routes": [
    { "from": "rack1", "to": "main", "gain": 1.0 },
    { "from": "rack2", "to": "main", "gain": 0.9 }
  ]
}
```

### Examples
1) Two racks to a `main` bus with shared limiter.
2) Drum rack sidechains a bass rack via `spectral_ducker` insert on `main`.
3) FX bus with reverb; sends from both racks.

See `examples/session_buses_samples.json` for a runnable sample.

### How to run (MVP)
1) Export session with buses:
```bash
./build/mam --session examples/session_buses_samples.json --wav session_buses.wav --progress-ms 200
```
2) At MVP stage, inserts are placeholders; bus routing and summing works. Next milestones add working inserts (e.g., spectral_ducker) with latency compensation.

### Realtime sessions with buses
```bash
./build/mam --session examples/session_buses_samples.json --print-triggers --quit-after 5
```
- Prints a startup banner and per‑rack command counts.
- Racks are rendered in realtime and routed to buses. If a rack has no route, its output is summed to main (fallback).
- Spectral ducker insert runs in realtime on bus buffers.

### Roadmap
- Implement bus graph: build inserts as a dedicated Graph mixed with rack outputs.
- Latency compensation: account for insert/bus latency.
- Meters per bus and per insert.

