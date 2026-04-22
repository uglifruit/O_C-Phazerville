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
wet   ──► AudioEffectReverbSchroeder (reverb) ──► reverb_out ─────────────────────►┤ AudioMixer<3> ──► Output
Input ──────────────────────────────────────────────────── dry ───────────────────►┘
```

`MistierApplet<Channels>` is templated; currently only `MONO` is instantiated in `_config.h`
(stereo not added to keep RAM1 pressure down).

### MistierChannel struct

Each channel owns:
- `AudioEffectClouds grain_stream` — records audio, spawns/plays grains
- `AudioEffectReverbSchroeder* reverb` — borrowed from `bung_factory` via `GetBungverb()`; may be
  null if all 8 factory slots are in use — code handles this gracefully
- `AudioMixer<3> mixer` — channels: 0=dry, 1=wet, 2=reverb

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

### Two-page layout (10px row spacing, y=15/25/35/45 on page 1, y=15/25/35/45/55 on page 2)

| Page | Rows |
|------|------|
| 1 (`cursor < PITCH`) | Pos, Den, Sz, Spr |
| 2 (`cursor >= PITCH`) | Pitch, Blend+Mode, Tex, Mix, Frz |

### Blend row (page 2, y=25) — three cursors, one row

The Blend row is unusual: three cursors share one display line.

```
BLEND_MODE cursor → underlines the 3-char label ("WD"/"FB"/"RV")
BLEND cursor      → underlines the % value
BLEND_CV cursor   → underlines the CV assignment widget
```

- `BLEND_MODE`: in edit mode, encoder cycles `(blend_mode_ + 3 + direction) % 3` — wraps both ways
- `BLEND_CV`: no CV slot shown separately; it's the third cursor on the same row

### Cursor enum (in order)

```
Page 1: POS, POS_CV, DENSITY, DENSITY_CV, SIZE, SIZE_CV, SPRAY, SPRAY_CV
Page 2: PITCH, PITCH_CV, BLEND, BLEND_CV, BLEND_MODE, TEXTURE, TEXTURE_CV,
        MIX, MIX_CV, FREEZE
```

`BLEND_MODE` sits between `BLEND_CV` and `TEXTURE` — it has no CV slot and is not passed to
`CheckEditInputMapPress`.

### Freeze (page 2, y=55)

- `freeze_input` (`DigitalInputMap`): assign a hardware gate jack — cursor on Frz row, press button
- `manual_freeze_` (bool): latched by **AuxButton** — live performance freeze, no cable needed
- Combined: `frozen = freeze_input.Gate() || manual_freeze_`
- Display: Frz row label inverts while `manual_freeze_` is true

### AuxButton

Latches/unlatches `manual_freeze_`. Does NOT cycle blend mode (that is encoder-only).

### Data packing

```cpp
data[0] = PackPackables(pos, density, size, texture, pitch, psprd, blend, mix)
data[1] = PackPackables(pos_cv, density_cv, size_cv, spray_cv)
data[2] = PackPackables(pitch_cv, blend_cv, texture_cv, mix_cv)
data[3] = PackPackables(freeze_input, (uint8_t)blend_mode_, spray)
```

Note: `spray_cv` is in `data[1]`, `spray` value is in `data[3]`. `psprd` is hidden (no cursor)
but persists in `data[0]`.

---

## RAM1 Notes

- Mono only — no stereo template. Adding `MistierApplet<STEREO>` would roughly halve remaining headroom.
- RAM1 free after build: ~6.9 KB (T41 slot 0)
- If RAM1 overflows: remove reverb chain from `MistierChannel::Start()` (biggest saving) or
  reduce `MAX_GRAINS` in `AudioEffectClouds.h`

---

## Known Gotchas

- `density = 50` → exactly 0 Hz → silence. Default is 75. Never reset this to 50 as a "neutral" default.
- `BLEND_MODE` cursor is NOT in `CheckEditInputMapPress` — correct, it has no input map.
- Reverb `reverb` pointer may be null if bung_factory is exhausted. All reverb accesses are guarded.
- `hann_lut_` is `const` (not `constexpr`) — ends up in `.rodata` (flash). On AVR this would need
  `PROGMEM`; on Teensy 4.1 (IMXRT, von Neumann) `const` is sufficient.
- `psprd_cv` member exists but has no cursor — it's a silent fallback from the original design.
  Its value is always `psprd=0`, CV source unassigned. Safe to leave.
