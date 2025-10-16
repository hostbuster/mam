## Session Design: Timeline, Commands, Duration, and Looping

This document explores options for session-level timing and control, with the goal of a powerful yet understandable design. It proposes concrete, incremental specs that interoperate with existing features (racks, buses, routes, xfaders) and leave room to grow.

### Goals
- Balance power and simplicity: authoring should be clear for non-developers but expressive enough for complex arrangements.
- Keep realtime/offline parity: sessions should behave the same in the player and when exporting.
- Preserve sample accuracy: all events map cleanly to sample times at render.
- Be future-proof: allow scenes, global tempo maps, and external control later.

### Existing Context (Constraints)
- A session aggregates multiple rack graphs (each with its own transport, nodes, commands, and overrides like `bars`, `loopCount`, `loopMinutes/Seconds`).
- Session routes connect racks to buses and then to the master mix.
- Session supports crossfaders (`xfaders[]`) applied at the mixer in realtime.
- Realtime renderer is the top-level scheduler; offline has a similar plan-first model.

### Timing Addressing Options

1) Absolute Time (Seconds)
- Shape: `{ "timeSec": 12.5, "nodeId": "xfader:main:x", "type": "SetParam", "value": 1.0, "rampMs": 250 }`.
- Pros: simple, transport-agnostic; works even if racks differ in tempo.
- Cons: not musical; authoring by ear or with stopwatch.

2) Musical Time (Bars/Steps) Anchored to a Rack
- Shape: `{ "rack": "rack1", "bar": 8, "step": 1, "res": 16, "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.0, "rampMs": 500 }`.
- Pros: musician-friendly; aligns to a specific rack’s transport (tempo, resolution, ramps).
- Cons: ambiguity when multiple racks differ in tempo/transport; requires a reference rack.

3) Hybrid (Support Both)
- Allow either `timeSec` or (`rack` + `bar`/`step`), resolving to absolute sample time at plan-time.
- Resolution rules per command:
  - If `timeSec` is present → use absolute seconds.
  - Else if `rack` and musical fields are present → resolve with that rack’s transport (tempo ramps) to absolute time, then samples.
  - If both provided → prefer `timeSec` and warn.

### Duration and Looping (Song Semantics)

Session-level duration fields (proposed):
- `durationSec` (number) — hard cap for session play length.
- `loop` (boolean) — if true, the player loops from start when reaching `durationSec`.
- `prerollSec` (number, optional) — derivable; estimated from rack prerolls.
- `tailSec` (number, optional) — auto-suggest, or manual override for long reverb/delay tails.

Rules of thumb:
- If `durationSec` is omitted, compute as the max of: last session command time, last rack content, plus preroll and tail.
- Offline export always renders through preroll and tail.
- Realtime player honors `loop`; commands that spill past loop boundaries wrap or clip depending on an `eventWrapping` policy (default: wrap musical, clip absolute).

### Command Model (Session-Level)

Schema (incremental proposal):

```
{
  "commands": [
    { "timeSec": 4.0,  "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.0, "rampMs": 250 },
    { "rack": "rack1", "bar": 8, "step": 1, "res": 16,
      "nodeId": "xfader:main:x", "type": "SetParam", "value": 1.0, "rampMs": 1000 }
  ]
}
```

- `nodeId` supports session-level targets like `xfader:<id>:x`; can extend to `bus:<id>:param` later.
- `type`: `SetParam` (for now). Future: `Trigger`, explicit `SetParamRamp`.
- `rampMs`: optional; when provided, becomes smoothing horizon for the target.
- Musical addressing uses `rack` + `bar` + optional `step`/`res`. If only `bar` is given, execute at bar start.

Scheduling & priority:
- All are resolved to absolute sample times during session plan.
- Merge session commands with rack-synthesized events; use deterministic ordering (time, nodeId, type).
- Same-sample ordering: SetParams before Triggers; xfader logic before rack mixing.

### Scenes (Future)

Add `scenes[]` to session:

```
{
  "scenes": [
    { "id": "verse",  "xfaders": [{ "id": "main", "x": 0.2 }] },
    { "id": "chorus", "xfaders": [{ "id": "main", "x": 0.9 }] }
  ]
}
```

- Commands can `RecallScene` at time or bar/step.
- Optionally `morphMs` for smooth transitions.

### Player Looping Modes (Future)
- `loop: true` — entire session loops (A→B) using `durationSec` boundaries.
- `loopInSec` / `loopOutSec` — partial loop.
- Musical loop region: `{ "rack": "rack1", "barIn": X, "barOut": Y }` resolved to absolute.

### Parameter Addressing (Naming)
- Session-level now:
  - `xfader:<id>:x`
- Future session-level:
  - `bus:<id>:param`
- Rack/node-level (existing): use prefixed `rackId:nodeId` inside graphs.

### Examples

Absolute time automation:

```
{
  "durationSec": 16,
  "loop": false,
  "commands": [
    { "timeSec": 2.0,  "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.25, "rampMs": 250 },
    { "timeSec": 10.0, "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.85, "rampMs": 500 }
  ]
}
```

Musical (anchored to rack1 transport):

```
{
  "durationSec": 32,
  "loop": false,
  "commands": [
    { "rack": "rack1", "bar": 8,  "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.0, "rampMs": 250 },
    { "rack": "rack1", "bar": 16, "nodeId": "xfader:main:x", "type": "SetParam", "value": 1.0, "rampMs": 1000 }
  ]
}
```

### UX/Docs Defaults
- When `--dump-events` is enabled, print the resolved session plan (absolute times with bar/step labels) before run/export.
- Provide meaningful warnings: ambiguous reference rack, invalid step, overlapping ramps.
- NDJSON includes `event:"xfader"`, `id`, `x`, `gainA`, `gainB` per interval for observability.

### Backward Compatibility
- Sessions without `commands`, `durationSec`, or `loop` behave exactly as before.
- Musical mode requires at least one rack with transport to anchor timing.

### Implementation Status (October 2025)

✅ **Phase 1 COMPLETED**: Add `commands[]` to `SessionSpec` with `timeSec` support (absolute time).
- Session commands are supported in both realtime and offline rendering
- Commands are resolved to sample time and enqueued with proper priority
- Session-level targets like `xfader:<id>:x` are supported

✅ **Phase 2 COMPLETED**: Add musical addressing (`rack`, `bar`, `step`, `res`) resolving to `timeSec` using the referenced rack's transport.
- Musical time commands are fully implemented and tested
- Supports bar-level and step-level precision (e.g., bar 2, step 9)
- Resolution uses the referenced rack's BPM and transport settings
- Works in both realtime and offline rendering modes

✅ **Phase 3 COMPLETED**: Add `durationSec`/`loop` handling in realtime player; offline renders through duration + tail.
- `durationSec` is fully supported for both realtime and offline rendering
- `loop` functionality implemented with proper session restart for realtime playback
- Loop duration calculation based on rack transport settings (BPM, bars, resolution)
- Offline rendering supports loop-aware duration planning and multi-loop rendering
- Seamless musical looping with proper state reset and command feeder restart

❌ **Phase 4 PENDING**: Scenes and `RecallScene` command (optional).
❌ **Phase 5 PENDING**: Extended targets: `bus:<id>:param` (optional, post-mix inserts etc.).

### Current Capabilities

**Supported Session Features:**
```json
{
  // Session-level duration and looping
  "durationSec": 60.0,    // Hard duration cap
  "loop": true,           // Enable looping when duration reached

  "commands": [
    // Absolute time addressing
    { "timeSec": 4.0, "nodeId": "xfader:main:x", "type": "SetParam", "value": 0.0, "rampMs": 250 },

    // Musical time addressing (NEW!)
    { "rack": "rack1", "bar": 8, "step": 1, "res": 16,
      "nodeId": "xfader:main:x", "type": "SetParam", "value": 1.0, "rampMs": 1000 }
  ]
}
```

**Key Features:**
- **Hybrid addressing**: Commands can use either absolute time (`timeSec`) or musical time (`rack` + `bar`/`step`)
- **Priority handling**: When both addressing modes are present, `timeSec` takes precedence with a warning
- **Rack anchoring**: Musical time is resolved using the specified rack's transport (BPM, bars, resolution)
- **Sample accuracy**: All commands are resolved to exact sample times during session planning
- **Offline parity**: Musical addressing works identically in realtime and offline rendering modes
- **Seamless looping**: Realtime and offline sessions support proper loop restart with state reset
- **Loop-aware duration**: Session duration calculation respects loop settings and transport timing

### Rationale
- Supporting both absolute and musical timing covers live performance and song-like authoring.
- Anchoring musical time to a specific rack avoids global tempo coupling while remaining predictable.
- Session-level duration/loop fields make “songs” and “loops” first-class without losing flexibility.


