# Mistier Applet — Claude Notes

Two files form this applet:
- `software/src/audio_applets/MistierApplet.h` — HemisphereAudioApplet UI/control layer
- `software/src/Audio/AudioEffectClouds.h` — AudioStream DSP engine

---

## History

Built as a Clouds-inspired evolution of MistApplet. Deliberately not called "Clouds" to avoid
any IP issues with Mutable Instruments / Emilie Gillet's work. The DSP class retains the name
`AudioEffectClouds` internally (no user-facing string), but the applet is named **Mistier**.

MistApplet is **unchanged and still present** alongside Mistier in the mono processor pool.

---

## Architecture

```
Input ──► AudioEffectClouds (grain_stream) ──► wet ──────────────────────────────►┐
Input ──────────────────────────────────────── dry ───────────────────────────────►┤ AudioMixer<2> ──► AudioEffectFreeverb ──► Output
```

`MistierApplet<Channels>` is templated; currently only `MONO` is instantiated in `_config.h`
(stereo not added to keep RAM1 pressure down).

`AudioEffectFreeverb` is acquired from the shared `verb_factory` pool in `HemisphereAudioApplet`
via `GetFreeverb()`. Controlled by the `Rvb` param (0–100%) on page 2. If the pool is exhausted
(returns nullptr), the applet falls back to a direct mixer→output connection (no reverb).

### MistierChannel struct

Each channel owns:
- `AudioEffectClouds grain_stream` — records audio, spawns/plays grains
- `AudioMixer<2> mixer` — channels: 0=dry, 1=wet

---

## DSP Engine (AudioEffectClouds)

### Buffer

Same `CloudsCircBuffer<int16_t>` pattern as MistCircBuffer. Size: 1 second @ sample rate in PSRAM,
or half-size in OCRAM if no PSRAM (`external_psram_size` check at construction time).

### Grain lifecycle

Same as Mist (phase accumulator → spawnGrain → Hermite read → accum → scale), with:

1. **Density centred at 0 = silence** — `density_` ∈ [−20, +20] Hz
   - Negative: regular periodic spawning
   - Positive: stochastic — each advance jittered ×[0.5, 1.5]
   - Zero: `if (cur_density != 0.0f)` guard skips spawning entirely
2. **Texture morph** — no `switch` statement, single inner loop:
   - `tex_lo` / `tex_hi` precomputed per-grain at spawn from `texture_` ∈ [0, 1]
   - `w = tri + tex_lo*(1−tri) + tex_hi*(hann−tri)` — 6 float ops per sample
   - Hann read from 256-entry Q15 LUT (`hann_lut_[256]`, `const` static → `.rodata` / flash)
   - No `sinf` or `arm_sin_f32` in the hot loop
3. **Feedback** — `feedback_buf_[128]` member array (not stack), holds `scaled * feedback_` from
   previous block, mixed back into incoming audio before writing to record buffer

### Density UI→DSP mapping

```
UI range:  0–100 (int8_t)
DSP range: −20..+20 Hz float
Formula:   eff_density = 0.4f * (density - 50)
Display:   int8_t d_hz = (int8_t)(0.4f * (density - 50))  → shows e.g. "+10", "- 5"
```

Default: `density = 75` → +10 Hz stochastic cloud. **Do not change default to 50** — that maps
to exactly 0.0f, skipping all grain spawning and producing silence.

---

## UI Layer (MistierApplet)

### Two-page layout

| Page | Rows | y positions |
|------|------|-------------|
| 1 (`cursor < PITCH`) | Pos, Den, Sz, Spr, PSp | y=15/25/35/45/55 (10px spacing) |
| 2 (`cursor >= PITCH`) | Pt, Fdb, Rvb, Tex, Mix, Frz | y=13/20/27/34/41/48 (7px spacing) |

Page 2 has six rows at 7px pitch to accommodate the new Rvb row.

### Cursor enum (in order)

```
Page 1: POS, POS_CV, DENSITY, DENSITY_CV, SIZE, SIZE_CV, SPRAY, SPRAY_CV,
        PSPRD, PSPRD_CV
Page 2: PITCH, PITCH_CV, FDB, FDB_CV, RVB, RVB_CV, TEXTURE, TEXTURE_CV,
        MIX, MIX_CV, FREEZE
```

All page 2 cursors are symmetric: value cursor + CV cursor pairs, plus FREEZE (DigitalInputMap).

`PSPRD`/`PSPRD_CV` appear on page 1 row 5 (y=55). `psprd_cv` is packed in `data[3]`.

### Freeze (page 2, y=55)

- `freeze_input` (`DigitalInputMap`): assign a hardware gate jack — cursor on Frz row, press button
- `manual_freeze_` (bool): latched by **AuxButton** — live performance freeze, no cable needed
- Combined: `frozen = freeze_input.Gate() || manual_freeze_`
- Display: Frz row label inverts while `manual_freeze_` is true

### AuxButton

Latches/unlatches `manual_freeze_`.

### Data packing

```cpp
data[0] = PackPackables(pos, density, size, texture, pitch, psprd, fdb, mix)  // 8×8 = 64 bits
data[1] = PackPackables(pos_cv, density_cv, size_cv, spray_cv)                 // 4×16 = 64 bits
data[2] = PackPackables(pitch_cv, fdb_cv, texture_cv, mix_cv)                  // 4×16 = 64 bits
data[3] = PackPackables(freeze_input, spray, psprd_cv, rvb, rvb_cv)            // 16+8+16+8+16 = 64 bits
```

Note: `spray_cv` is in `data[1]`, `spray` value is in `data[3]`. `psprd` value is in
`data[0]`, `psprd_cv` is in `data[3]`. `rvb` and `rvb_cv` are appended at the end of `data[3]`.

---

## RAM1 Notes

- Mono only — no stereo template. Adding `MistierApplet<STEREO>` would roughly halve remaining headroom.
- RAM1 free after build: ~6.9 KB (T41 slot 0). Position LFO adds 48 bytes (12 floats); reverb uses shared pool (no static RAM cost).
- If RAM1 overflows: reduce `MAX_GRAINS` in `AudioEffectClouds.h`.

---

## Known Gotchas

- `density = 50` → exactly 0 Hz → silence. Default is 75. Never reset this to 50 as a "neutral" default.
- `hann_lut_` is `const` (not `constexpr`) — ends up in `.rodata` (flash). On AVR this would need
  `PROGMEM`; on Teensy 4.1 (IMXRT, von Neumann) `const` is sufficient.
- `rvb = 0` default means no reverb on first load — intentional. Start dry and dial in.
- `fdb = 0` default means no feedback on first load — intentional, avoids surprising runaway on startup.
- If `GetFreeverb()` returns nullptr (all 8 pool slots in use), the applet operates without reverb silently.
- Reverb `roomsize` maps to 0.5–0.95; `damping` maps 0.6→0.3 as Rvb increases (brighter reverb at high amounts).
