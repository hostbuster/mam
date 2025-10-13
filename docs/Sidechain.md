## Sidechain Routing Guide

This page explains how to author and debug sidechain routing in the MAM graph engine. It expands on the brief README section with end-to-end examples, authoring tips, validation behavior, and troubleshooting.

### Concept

- A sidechain is a secondary audio input used to control a processor (e.g., a compressor) without contributing audio to the processor’s main input sum.
- In the graph, this is modeled with explicit input ports on a `compressor` node:
  - `inputs[0]` role `main`: program material to be processed
  - `inputs[1]` role `sidechain`: detector/key signal (usually kick)
- Engine mixing policy:
  - Only connections to `toPort=0` contribute to the node’s main input mix.
  - Other ports (e.g., `toPort=1` sidechain) are isolated and read by port-aware nodes internally.

### Authoring basics

- Declare ports in the node’s `ports.inputs[]` and `ports.outputs[]` with `index`, `type`, and optional `channels`/`role`.
- Create `connections[]` from source nodes to the compressor’s `toPort=0` (main) and `toPort=1` (sidechain key).
- Channel adapters are automatic:
  - 2->1 (stereo key to mono detector): averaged to mono
  - 1->2 (mono source into stereo engine): duplicated to graph width
  - N->M (N,M>1): modulo mapping per channel within graph width
- Best practice: set sidechain input `channels` to 1 for classic mono keying.

### Minimal schema shape

```json
{
  "nodes": [
    { "id": "comp1", "type": "compressor",
      "ports": { "inputs": [
        { "index": 0, "type": "audio", "role": "main" },
        { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 }
      ],
      "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "bass", "to": "comp1", "fromPort": 0, "toPort": 0 },
    { "from": "kick", "to": "comp1", "fromPort": 0, "toPort": 1 }
  ]
}
```

### Cookbook examples

#### 1) Mono key into stereo compressor (2->1 sidechain)

```json
{
  "nodes": [
    { "id": "compA", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" },
                                 { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "pad",  "to": "compA", "fromPort": 0, "toPort": 0 },
    { "from": "kick", "to": "compA", "fromPort": 0, "toPort": 1 }
  ]
}
```

#### 2) One key driving multiple compressors (shared key)

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

#### 3) Post-effect ducking (duck reverb tail using pre-fader key via wiretap)

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

#### 4) Stereo key to mono detector (2->1)

If your key source is stereo and the detector is mono (recommended), leave the sidechain `channels` at `1`; the engine will average L/R.

```json
{
  "nodes": [
    { "id": "comp", "type": "compressor",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "stereoKickBus", "to": "comp", "fromPort": 0, "toPort": 1 }
  ]
}
```

#### 5) Bus keying (aux key bus feeds multiple sidechains)

```json
{
  "nodes": [
    { "id": "keyBus", "type": "meter",
      "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" } ],
                  "outputs": [ { "index": 0, "type": "audio", "role": "main", "channels": 1 } ] } },
    { "id": "compA", "type": "compressor", "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ], "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } },
    { "id": "compB", "type": "compressor", "ports": { "inputs": [ { "index": 0, "type": "audio", "role": "main" }, { "index": 1, "type": "audio", "role": "sidechain", "channels": 1 } ], "outputs": [ { "index": 0, "type": "audio", "role": "main" } ] } }
  ],
  "connections": [
    { "from": "kick",  "to": "keyBus", "fromPort": 0, "toPort": 0 },
    { "from": "pad",   "to": "compA",  "fromPort": 0, "toPort": 0 },
    { "from": "bass",  "to": "compB",  "fromPort": 0, "toPort": 0 },
    { "from": "keyBus", "to": "compA",  "fromPort": 0, "toPort": 1 },
    { "from": "keyBus", "to": "compB",  "fromPort": 0, "toPort": 1 }
  ]
}
```

### Validation and warnings

- Schema checks (when enabled) verify that `fromPort`/`toPort` indices exist and port types are compatible (`audio`->`audio`).
- Engine warnings:
  - Duplicate mixer input IDs
  - Dry tap double-count risks when also mixing source directly
  - Sidechain advisory: compressor without a `toPort=1` key prints a warning and falls back to self-detection

### Debugging tips

- Print topology with ports and channels:

```bash
./build/mam --graph demo2.json --print-topo
```

- Inspect meters after export or at realtime loop boundaries:

```bash
# Realtime
./build/mam --graph demo2.json --verbose --meters-per-node

# Offline
./build/mam --graph demo2.json --wav out.wav --meters
```

- Enforce schema before running (requires validator available):

```bash
./build/mam --graph demo2.json --schema-strict --verbose
```

### Best practices

- Use mono sidechain detectors (`channels: 1`) for consistent keying.
- Keep sidechain signals clean and transient-rich (e.g., short kick thump) to avoid pumping.
- Avoid double-counting: do not set `dryPercent` on edges feeding the same source that is also mixed directly in the mixer at unity.
- Prefer moderate compressor settings first (e.g., threshold −2..−6 dBFS, ratio 1.2:1..2:1, attack 10–30 ms, release 150–300 ms), then tune by ear.

### Reference

- See `docs/schema.graph.v1.json` for the node and connection shapes.
- See `README.md` for an overview and quick examples.
- See `demo2.json` for a practical 16-bar graph including sidechain use.
