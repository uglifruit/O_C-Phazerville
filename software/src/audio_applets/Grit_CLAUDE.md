# GritApplet — Claude Notes

Two files form this applet:
- `software/src/audio_applets/GritApplet.h` — HemisphereAudioApplet UI/control layer
- `software/src/Audio/AudioEffectGrit.h` — AudioStream DSP engine

---

## Architecture

```
Input ──► AudioEffectGrit (distortion + tone filter + wet/dry mix) ──► Output
```

Unlike most other applets, **wet/dry mixing is done inside `AudioEffectGrit::update()`**,
not via a separate `AudioMixer<2>`. This avoids allocating an extra audio object and keeps
the block size at 1 input / 1 output.

`GritApplet<Channels>` is templated; currently only `MONO` is instantiated in `_config.h`.

---

## DSP Engine (AudioEffectGrit)

### Parameter handoff

All parameters are `volatile` members set via setters called from `Controller()` (~150 Hz ISR).
`update()` runs from the higher-priority audio interrupt. The pattern matches `AudioEffectGlitch`.

### Mode dispatch

```
MODE_CLIP  (0): hard clip at ±clip_thresh (derived from amt_)
MODE_SAT   (1): x / (1 + |x| × k), k derived from amt_ (Padé rational approx)
MODE_CRUSH (2): int16_t bit truncation: (q >> bits) << bits, bits 0–15
MODE_DECI  (3): sample-hold counter, hold period 1–16 samples
```

Each mode branch is computed from `amt_` once per block (before the sample loop) to avoid
per-sample multiplies where possible.

### Tone filter

Single-pole IIR lowpass on the **wet signal only**, applied after the distortion stage and
before wet/dry mixing:

```cpp
tone_z1_ += tone_coeff_ * (w - tone_z1_);
w = tone_z1_;
```

`tone_coeff_` is precomputed in `Controller()` (not the hot loop):
```cpp
float hz = 800.0f * powf(25.0f, eff_tone);   // 0.0–1.0 → 800–20000 Hz
tone_coeff_ = 1.0f - expf(-2.0f * M_PI * hz / AUDIO_SAMPLE_RATE_EXACT);
```

At `tone=100%`, `tone_coeff_` ≈ 1.0 → filter is fully open (passes everything).
At `tone=0%`, cutoff ≈ 800 Hz.

### Drive mapping

```cpp
float eff_drive = 1.0f + constrain(0.01f * drive + drv_cv.InF(), 0.0f, 1.0f) * 9.0f;
```
`drive=0%` → ×1 (unity), `drive=100%` → ×10.

### CLIP threshold mapping

```cpp
const float clip_thresh = 0.05f + amt * 0.95f;  // amt=0.0 → 0.05, amt=1.0 → 1.0
```
`amt=100%` → threshold=1.0 → no clip at unity drive; `amt=0%` → hard limit near zero.

### SAT knee mapping

```cpp
const float sat_k = (1.0f - amt) * 0.01f + amt * 30.0f;  // 0.01–30
```
Low k → near-linear; high k → hard saturation.

### CRUSH bit mapping

```cpp
const int crush_bits = (int)(amt * 15.0f + 0.5f);  // 0–15
```
0 = 16-bit (clean), 15 = 1-bit (extreme).

### DECI hold mapping

```cpp
const int deci_hold_target = 1 + (int)(amt * 15.0f + 0.5f);  // 1–16
```
1 = every sample updated (clean), 16 = 16× hold.

### State members

```cpp
float   tone_z1_    // 1-pole LP state; reset in Acquire()/Release()
int16_t deci_hold_  // last held sample for DECI mode
int     deci_count_ // decimation counter
```

---

## UI Layer (GritApplet)

### Cursor enum

```
MODE, DRIVE, DRIVE_CV, AMT, AMT_CV, TONE, TONE_CV, MIX, MIX_CV
```

9 positions, single page. MODE has no CV slot.

### Mode display

Mode shown as 4-char label: "CLIP" / "SAT " / "CRUS" / "DECI".
Amt row label changes per mode: "Thr:" / "Kne:" / "Bit:" / "Dec:".

### AuxButton

Cycles mode: `(mode_ + 1) % 4`. Useful for live performance without entering edit mode.

### Data packing

```cpp
data[0] = PackPackables(mode_, drive, amt, tone, mix)   // 5 × int8_t = 40 bits
data[1] = PackPackables(drv_cv, amt_cv, tone_cv, mix_cv) // 4 × CVInputMap = 64 bits
```

`mode_` is stored as `int8_t` (0–3). `OnDataReceive` clamps it to `[0, 3]` for safety.

---

## Known Gotchas

- Wet/dry mixing is inside `AudioEffectGrit`, not in a separate mixer object — there is
  no `AudioMixer` in this applet. Don't add one.
- `tone_coeff_ = 1.0f` bypasses the LP completely (fully open). Setting it to values > 1.0
  would make the filter unstable — the `constrain` on `eff_tone` prevents this.
- `crush_bits=0` → `(q >> 0) << 0` = no change — this is intentional (clean pass at Amt=0%).
- DECI mode uses `in->data[i]` directly for the held sample (pre-drive) to avoid the held
  value drifting with drive changes when the hold is active.
- Default mode is `MODE_SAT` (soft saturation) — the most musical default for first use.
