# MAMIC — Maximum Audio Madness Integrated Chip (design)

This document proposes a modern "virtual chip" for chiptune-style synthesis in MAM. MAMIC captures the musical character of classic PSG (AY/YM) and SID chips without strict emulation constraints, while offering a clean C++17 node API, band-limited oscillators, and quality/feature modes.

We remain license-clean by implementing our own DSP. Optional adapters to third-party cores can be added later (behind flags) for register-accurate playback.

## Goals and benefits

- Creative, modern chiptune synthesis with classic vibes (PSG/SID) but without legacy limitations.
- Musical API first (notes/velocity/params), optional register facade for tracker/VGM workflows.
- High sound quality: band-limited oscillators, optional coloration (grit, DAC steps), tasteful filter/drive.
- Performance-friendly and realtime-safe; identical offline exports.
- Presets that "feel" like AY/YM or SID, plus expanded "custom" mode.

## High-level feature set

- Voices: 3–8 voices (configurable). Each voice has:
  - Band-limited oscillators: square/pulse, triangle, saw (polyBLEP/minBLEP).
  - Noise mix per voice (shared noise generator optional for PSG flavor).
  - Envelopes: ADSR (modern) + loopable AD/AR (PSG-style).
  - Optional sync and ring (for SID-ish flavor) — can be disabled in PSG preset.
  - Panning (equal-power), velocity, per-voice gain.
- Global processing:
  - Multimode filter (state-variable: LP/BP/HP) with optional keytracking.
  - Soft drive/saturation pre/post filter.
  - "Grit" options: bit-depth and sample-rate reduction (opt-in), DAC step quantization.
  - Alias switch (adds intentional clocked aliasing for nostalgia).
- Modes/Presets:
  - PSG mode (MAMIC-3): 3 voices, shared noise, 50% square default, loopable envelopes.
  - SID-ish mode (MAMIC-SIDe): 3 voices, PWM/tri/saw/noise, ring/sync, filter/drive coloration.
  - Custom/Expanded (MAMIC-8): up to 8 voices, all features.

## Node API (musical)

Type: `mam_chip` (aka MAMIC). Modes expose the same API; presets set defaults/constraints.

- Global params (examples):
  - `MODE` (enum: `psg`, `sidish`, `custom`)
  - `MASTER_GAIN` (0..1.5)
  - Filter: `FILTER_TYPE` (lp/bp/hp), `FILTER_CUTOFF_HZ`, `FILTER_RESO`, `FILTER_DRIVE`
  - Color: `GRIT_BITDEPTH` (off|16|12|8|...), `GRIT_SRRATE` (Hz, 0=off), `DAC_STEPS` (0=off)
  - `ALIAS_CLOCK_TOGGLE` (0/1)
  - Noise: `NOISE_SHARED` (0/1), `NOISE_LEVEL_GLOBAL`
  - Polyphony: `NUM_VOICES` (3..8)
- Per-voice params (index N = 1..NUM_VOICES):
  - Pitch: `VOICE_N_NOTE_SEMITONES`, `VOICE_N_FINE_CENTS` (optional), `VOICE_N_GLIDE_MS`
  - Wave: `VOICE_N_WAVE` (square/pulse/tri/saw/noise), `VOICE_N_PULSE_WIDTH` (0.05..0.95)
  - Levels: `VOICE_N_VELOCITY`, `VOICE_N_GAIN`
  - Noise: `VOICE_N_NOISE_MIX`
  - Envelopes: `VOICE_N_ENV_ATTACK_MS`, `VOICE_N_ENV_DECAY_MS`, `VOICE_N_ENV_SUSTAIN`, `VOICE_N_ENV_RELEASE_MS`
  - Extras: `VOICE_N_SYNC` (0/1), `VOICE_N_RING` (0/1), `VOICE_N_PAN` (-1..1)

Events:
- Trigger: `Trigger` on a voice starts the envelope (and retrigger rate if desired by mode).
- SetParam/SetParamRamp: for all params above; transport locks supported.

## Optional register facade (later)

- "Register mode" accepts synthetic register writes: `{ time, reg, value }` mapped to internal voice/global states.
- Format adapters: AY/VGM (for PSG-style), PSID (for SID-style, if we later integrate a permissive core or a thin decoder).
- Always optional to keep main node license-clean.

## DSP architecture

- Oscillators: polyBLEP/minBLEP for pulse, tri, saw to avoid aliasing; configurable oversampling if needed.
- Noise: LFSR-based (PSG flavor) and white noise; shareable generator for PSG mode.
- Envelopes: ADSR (linear/log shapes), optional loopable AR patterns (PSG-like) per voice.
- Mixing: per-voice pan with equal-power law; wide to stereo; headroom preserved.
- Filter: state-variable filter, tunable drive pre/post; optional keytracking, envelope modulation.
- Coloration: bit-depth and sample-rate reduction blocks; DAC step quantization; controllable "alias clock" toggle.
- Realtime safety: no allocations/locks in process; denormals flushed.

## Quality/CPU considerations

- Default quality = band-limited oscillators; filter and drive enabled with moderate CPU.
- Presets tailor cost:
  - PSG: fewer features enabled, shared noise, 3 voices → very light weight.
  - SID-ish: ring/sync and filter drive → moderate.
  - Custom: up to 8 voices → heavier; document CPU guidance.

## Modulation & integration

- Routes: integrate with `ModMatrix` — LFOs to pulse width, cutoff, pan, drive; envelopes to filter.
- Transport: per-voice param locks; step patterns for duty, PWM sweeps, chokes.
- Parameter registry: add `mam_chip` to `ParamMap` with clear ranges, units, smoothing types.

## JSON example

```json
{
  "nodes": [
    { "id": "chip1", "type": "mam_chip",
      "params": {
        "mode": "psg",
        "numVoices": 3,
        "masterGain": 0.9,
        "filterType": "lp", "filterCutoff": 8000, "filterReso": 0.2,
        "noiseShared": 1, "noiseLevelGlobal": 0.1
      }
    }
  ],
  "transport": {
    "bpm": 128, "lengthBars": 4, "resolution": 16,
    "patterns": [
      { "nodeId": "chip1", "steps": "x.x.x.x.x.x.x.x.",
        "locks": [
          { "step": 0,  "param": "VOICE_1_NOTE_SEMITONES", "value": 60 },
          { "step": 0,  "param": "VOICE_1_WAVE",           "value": "pulse" },
          { "step": 0,  "param": "VOICE_1_PULSE_WIDTH",    "value": 0.25 },
          { "step": 4,  "param": "VOICE_1_PULSE_WIDTH",    "value": 0.75 }
        ] }
    ]
  }
}
```

## Presets (examples)

- MAMIC-3 (PSG): square(50%), shared noise, loopable AR envelopes, filter off by default; 3 voices.
- MAMIC-SIDe (SID-ish): PWM/tri/saw/noise, ring/sync on, filter/drive enabled; 3 voices; classic PWM lock ranges.
- MAMIC-8 (Custom): 8 voices, full features; recommended for lush modern chiptunes.

## Testing & parity

- Band-limit validation: spectral plots for each waveform; alias toggle behavior documented.
- Envelope and filter unit tests; headroom tests; denormal handling.
- Realtime/offline parity renders for preset demos.

## Roadmap

1) Scaffold `mam_chip` node in C++17 with musical API and presets; document `ParamMap`.
2) Implement polyBLEP oscillators, envelopes, shared noise; baseline filter/drive.
3) Add modulation routes; examples and docs.
4) Optional register facade with AY/VGM adapter (license-permissive sources only).
5) SID-ish extras: sync/ring; curated PWM ranges; filter character presets.

## Naming and UI

- Family name: **MAMIC** (Maximum Audio Madness Integrated Chip).
- Node/type: `mam_chip` (MAMIC).
- Preset strings: `psg`, `sidish`, `custom`.
- Keep names playful but clear; prioritize authoring ergonomics.

## License posture

- Core MAMIC DSP is original, license-clean.
- Any third-party adapters (AY/SID cores) must be permissive or optional behind build flags; defaults remain clean.
