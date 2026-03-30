# O_C-Phazerville Fork — Claude Context

## Repository
- Fork of `djphazer/O_C-Phazerville` at `uglifruit/O_C-Phazerville`
- Target hardware: **Teensy 4.1** (build env `T41`)
- Build system: **PlatformIO** via `~/.platformio/penv/Scripts/pio.exe`
- Build command: `cd software && ~/.platformio/penv/Scripts/pio.exe run -e T41`
- Hex output: `software/.pio/build/T41/*.hex`

---

## Branch Strategy

Each applet lives on its own branch off `phazerville` (the upstream main). This keeps diffs minimal and PRs clean for upstream submission. A separate test branch merges multiple applets for hardware testing together.

| Branch | Purpose |
|--------|---------|
| `phazerville` | Upstream base — do not commit here |
| `UgliApp` | CV passthrough + voltage display applet (ID 92) |
| `MarkoV` | Markov chain melodic generator (ID 93) |
| `MarkovPerc` | Markov chain rhythmic generator (ID 94) |
| `MarkovTest` | Both MarkoV + MarkovPerc merged for hardware testing |

**Current highest registry ID in use: 94.** Next new applet should use 95+.

To add a new applet branch: `git checkout phazerville && git checkout -b <Name>`

### Updating the test branch

**Never use patch/diff to apply changes to MarkovTest.** The linter modifies files on checkout and patch hunks then fail with "already applied" or "reversed" errors. Always overwrite directly:

```bash
git checkout MarkovTest
git show MarkoV:software/src/applets/MarkoV.h > software/src/applets/MarkoV.h
git show MarkovPerc:software/src/applets/MarkovPerc.h > software/src/applets/MarkovPerc.h
git add -p   # or add specific files
git commit
```

**Workflow order:** Always commit to the clean applet branch(es) first, then sync to the test branch. Never commit applet code directly to MarkovTest.

---

## Adding a New Applet

Two files must be changed; everything else is self-contained in the applet header:

1. **`software/src/applets/<Name>.h`** — the applet class
2. **`software/src/hemisphere_config.h`** — two additions:
   - `#include "applets/<Name>.h"` (alphabetical order, near line 104)
   - `DeclareApplet<Name>{id, category}` in the `AppletRegistry reg{...}` block
   - Category bitmask: `0x01`=LFO, `0x02`=Sequencer, `0x04`=Clock, `0x08`=Pitch, `0x10`=Utility, `0x20`=MIDI, `0x40`=Logic, `0x80`=Audio

User documentation goes in **`docs/_applets/<Name>.md`**. See existing files there for format. The `layout: default` frontmatter is required for the Jekyll site.

---

## Development Workflow & Preferences

### Before writing code
**Discuss non-trivial UX decisions before implementing.** The user thinks in terms of musical behaviour and patch workflow. Performance ergonomics (what a button does live on stage) matter as much as correctness. For any UX choice that isn't obvious — which button does what, how a CV input behaves, how reset interacts with a live performance — present the options and confirm first.

For pure code bugs, just fix them.

### Commit style
- Commit to the clean branch first, then sync to test branch
- Commit messages describe *why*, not just what
- Include `Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>` trailer
- No trailing summaries in responses — the user can read the diff

### Building
After any code change, build MarkovTest to verify:
```bash
cd software && ~/.platformio/penv/Scripts/pio.exe run -e T41
```
The build takes 2–4 minutes. Check the tail of output for SUCCESS/FAILED and memory usage.

---

## Established UI Conventions

These patterns are consistent across MarkoV and MarkovPerc and should be followed in new applets unless there's a good reason not to.

### Display layout (64×64 per hemisphere)
```
y=2   Hint text — right-justified, EditMode only
y=15  Parameter row
y=23  Cursor underlines
y=25  Separator line (gfxLine(0, 25, 63, 25))
y=26–62  Graph / content area
y=63  Baseline (gfxLine(0, 63, 63, 63))
```

**Hint text** (cursor label shown while editing):
```cpp
if (EditMode()) {
    const char* label = cursor_labels[cursor];
    gfxPrint(63 - (strlen(label) * 6), 2, label);
}
```
Right-justified at y=2. Only shown in EditMode. Labels array must match cursor count.

**Parameter row** at y=15. Right edge of usable space is ~x=52 to leave room for the dice icon at x=54. At 100% chaos "100%" is 4 chars = 24px; position chaos at x=25 (MarkoV) or x=22 (MarkovPerc) so it clears the dice.

**Dice icon** (Seed cursor) always at x=54, y=15. Region is `gfxIcon(54, 15, RANDOM_ICON)`.

**Cursor underlines** at y=23:
- Normal params: `gfxCursor(x, 23, w)`
- Params with an Aux secondary action: `gfxSpicyCursor(x, 23, w)` (dotted, hints at Aux)

### Flash indicators
Both use a `uint16_t` counter decremented every ISR tick (~16kHz):

**seed_flash** (encoder re-roll feedback):
- Set to `2700` on re-roll (~170ms)
- While active: draw dice icon 1px higher `gfxIcon(54, 14, RANDOM_ICON)` instead of normal y=15

**reset_flash** (Dig 2 short-press feedback):
- Set to `8000` on reset (~500ms)
- While active: invert the dice cell only `gfxInvert(53, 14, 10, 9)`
- Do NOT invert the whole parameter row — it obscures the profile/values the user just reset to

Both decremented in `Controller()` every tick:
```cpp
if (seed_flash  > 0) --seed_flash;
if (reset_flash > 0) --reset_flash;
```

### Seed & RNG reset system
Store two values: `seed` (start state index) + `rng_seed` (uint32_t, Arduino RNG seed).

| Action | Behaviour |
|--------|-----------|
| Short press Dig 2 | `randomSeed(rng_seed); state = seed;` — exact replay; triggers reset_flash |
| Long press Dig 2 (≥5000 ticks) | `rng_seed = micros(); randomSeed(rng_seed); seed = random(...); state = seed;` |
| Encoder on Seed cursor | Same as long press + triggers seed_flash; stay in EditMode for repeated rolls |
| Aux on Seed cursor | Same as encoder roll |

For percussive applets: never seed on a REST state. Use `random(NUM_STATES - 1) + 1`.

Persist both `seed` and `rng_seed` in `OnDataRequest`/`OnDataReceive`.

### SetHelp string lengths
Strings display in a fixed-width help screen. Keep each string to **8 chars max**; 9+ will truncate or go off-screen.
- Use `"RstSeed"` not `"Rst/Seed"` (slash pushes it over)
- Only add `HELP_EXTRA2` if there is a genuine secondary action worth documenting to a new user

---

## Framework Gotchas

### Internal clock fires on start
When the internal clock is enabled, it fires an immediate first tick when the applet starts. This is intended framework behaviour. It is not a bug. To avoid it: set multiply=0 and use a physical trigger source.

### Floating ADC inputs
`In(ch)` returns a floating ADC value when a CV source is mapped to a channel but nothing is physically plugged in. Floating inputs typically read high, which means Chaos pins at 100% and Density reads maximum. **User must set the CV source to None when not patching that input.** There is no reliable software fix.

### Trigger comparisons and CV jitter
When computing a trigger condition (e.g. "fire when pitch changes"), always compare the value **before** adding any transpose or offset CV. CV inputs jitter by ±1 ADC unit per tick, which will cause spurious triggers if the jittery value is part of the comparison.

```cpp
// CORRECT: compare quantized pitch only, add transpose after
int pitch_cv = HS::Quantize(qselect, state * STATE_CV_STEP);
Out(0, pitch_cv + transpose);
if (pitch_cv != prev_cv) ClockOut(1);
prev_cv = pitch_cv;

// WRONG: transpose jitter causes false triggers
int cv_out = HS::Quantize(...) + transpose;
if (cv_out != prev_cv) ClockOut(1);  // fires on CV noise
```

### Bipolar CV inputs
`In(ch)` and `Proportion()` are unipolar (0 to MAX). For a bipolar input centred at 0V = neutral:
```cpp
// Maps [-MAX..+MAX] → [0..256], neutral at 128
int value = constrain(128 + Proportion(In(ch), HEMISPHERE_MAX_INPUT_CV, 128), 0, 256);
```

### ADC lag
Always call `StartADCLag(ch)` immediately after `Clock(ch)`, then read CVs inside `EndOfADCLag(ch)`. This avoids reading a CV that hasn't settled yet after a clock edge.

### ClockCycleTicks
`ClockCycleTicks(0)` returns the measured period between the last two clocks. Guard against zero: `if (clock_period < 1) clock_period = 1000;`

---

## Data Persistence (OnDataRequest)

State is packed into a `uint64_t` (64 bits total). Plan field widths before writing:

| Field type | Typical width |
|------------|--------------|
| Profile index | `ceil(log2(NUM_PROFILES))` — plan ahead; **expanding this field breaks saved presets** |
| State index (8 states) | 3 bits |
| Quantizer select (4 channels) | 2 bits |
| Chaos 0–100 | 7 bits |
| Seed state | same as state width |
| rng_seed (uint32_t) | 32 bits |

Always `constrain()` every field on receive. Fields silently wrap if the stored value was from a different layout version.

If `NUM_PROFILES` grows beyond a power of 2 boundary, the profile field must widen by 1 bit and all subsequent field offsets shift. Old presets will silently load a wrong (but valid, clamped) profile.

---

## Key API Reference

### I/O
```cpp
bool Clock(int ch)                 // rising edge on digital input ch (0 or 1)
bool Gate(int ch)                  // current gate state
int In(int ch)                     // raw CV input (0 to HEMISPHERE_MAX_INPUT_CV)
float InF(int ch)                  // normalized CV 0.0-1.0
void Out(int ch, int cv)           // set DAC output
void ClockOut(int ch)              // emit trigger pulse on output ch
void StartADCLag(int ch)           // call after Clock() to delay CV read
bool EndOfADCLag(int ch)           // true ~33 ticks after StartADCLag
```

### Quantizer
```cpp
int Quantize(int ch, int cv)                          // quantize cv through this hemisphere's channel ch
int HS::Quantize(int abs_ch, int cv)                  // quantize through absolute channel
int HS::QuantizerLookup(int abs_ch, int note_number)  // note index → CV
void HS::QuantizerEdit(int abs_ch)                    // open quantizer editor UI
HS::qview = channel; HS::PokePopup(QUANTIZER_POPUP);  // show quantizer popup
```

### Maths helpers
```cpp
int Proportion(int value, int max_value, int max_out) // scale value (unipolar)
void Modulate(T &param, int ch, int min, int max)     // CV-modulate a param
Pack(data, PackLocation{bit, width}, value)           // serialize to uint64_t
Unpack(data, PackLocation{bit, width})                // deserialize
```

### Display (64x64 per hemisphere, x auto-offset)
```cpp
gfxPrint(x, y, val)               // text/numbers at position
gfxPos(x, y)                      // set cursor; use graphics.printf() after
gfxRect(x, y, w, h)               // filled rectangle
gfxLine(x, y, x2, y2)             // line
gfxFrame(x, y, w, h)              // outline rectangle
gfxInvert(x, y, w, h)             // invert region
gfxCursor(x, y, w)                // solid underline cursor
gfxSpicyCursor(x, y, w)           // dotted underline cursor (hints at special action)
gfxIcon(x, y, bitmap)             // 8x8 icon
bool EditMode()                    // true when encoder button is held/active
bool CursorBlink()                 // true during blink-on phase
void MoveCursor(cursor, dir, max)  // navigate cursor with bounds
```

### Constants
```cpp
ONE_OCTAVE = 1536              // CV units per octave (12 << 7)
HEMISPHERE_MAX_INPUT_CV        // max ADC CV value (~6V range)
QUANT_CHANNEL_COUNT            // number of quantizer channels available
io_offset                      // hemisphere's base channel offset (0 or 2)
```

---

## MarkoV Applet
**File:** `software/src/applets/MarkoV.h`
**Branch:** `MarkoV`
**Registry ID:** 93, category `0x02` (Sequencer)
**Docs:** `docs/_applets/MarkoV.md`

First-order Markov chain melodic generator. 8 states = scale degrees. On each clock, next state chosen by weighted random from current row of transition matrix. Five profiles.

### I/O
| Jack | Function |
|------|---------|
| Dig 1 | Clock |
| Dig 2 | Reset — short = replay from rng_seed+seed; long (≥5000 ticks) = new seed |
| CV 1 | Chaos offset (unipolar, adds to chaos_base) |
| CV 2 | Transpose V/Oct (added after quantization; NOT included in trigger comparison) |
| Out A | Quantized pitch |
| Out B | Trigger — fires only when quantized pitch (pre-transpose) changes |

### Parameters (4 cursors)
| Cursor | Param | Range |
|--------|-------|-------|
| 0 MATRIX | Profile | S / T / J / G / D |
| 1 SCALE | Quantizer channel | Q1–Q4; Aux opens editor (spicyCursor) |
| 2 CHAOS | Chaos baseline | 0–100% |
| 3 SEED | Seed/RNG | dice icon; encoder/Aux re-rolls |

### Profiles
- **S** Stability — root(0) + fifth(4) attractors, 20:1 ratios
- **T** Tension — stepwise ±1 dominates, snake-like lines
- **J** Jazz — strong pull to 7th(6), root resolution from 7th
- **G** Glacial — ~42% self-loops, ±1 only, no leaps
- **D** Drone — ~83% self-loops, ±1 escape only, no attractors; Out B mostly silent at low chaos

### Display layout
```
                [Seed]   ← hint y=2, EditMode only
[S] [Q1] [42%] [dice]   ← y=15: profile@1, scale@10, chaos@25, dice@54
──────────────────────   ← separator y=25
█  ██ █  ██ █  ██  █    ← bar graph (height=state, 8 bars), baseline y=63
```

### Data Persistence (OnDataRequest bit layout)
| Bits | Field | Width |
|------|-------|-------|
| 0–2 | profile | 3 |
| 3–5 | state | 3 |
| 6–7 | qselect | 2 |
| 8–14 | chaos_base | 7 |
| 15–17 | seed | 3 |
| 18–49 | rng_seed | 32 |

---

## MarkovPerc Applet
**File:** `software/src/applets/MarkovPerc.h`
**Branch:** `MarkovPerc`
**Registry ID:** 94, category `0x02` (Sequencer)
**Docs:** `docs/_applets/MarkovPerc.md`

Rhythmic sibling to MarkoV. States are hit types. On each clock, next hit type chosen by Markov chain. Four profiles.

### I/O
| Jack | Function |
|------|---------|
| Dig 1 | Clock |
| Dig 2 | Reset — short = replay; long (≥5000 ticks) = new seed |
| CV 1 | Chaos offset (unipolar, adds to chaos_base) |
| CV 2 | Density — **bipolar**: 0V = neutral (128), +V = more hits, –V = more rests |
| Out A | Trigger — sub-triggers for ratchets/flams within the beat |
| Out B | Accent CV — 0–5V held for full clock period; level = beat accent of current state |

### Parameters (3 cursors)
| Cursor | Param | Range |
|--------|-------|-------|
| 0 STYLE | Profile | S / T / J / P |
| 1 CHAOS | Chaos baseline | 0–100% |
| 2 SEED | Seed/RNG | dice icon; encoder/Aux re-rolls; never seeds on REST |

### Profiles
- **S** Steady (Rock/Pop) — hits/acc_hits dominate, rests brief, ratchets rare
- **T** Syncopated (Funk/Latin) — rests structurally meaningful, flams very common, ratchet_2 frequent
- **J** Jazz/Free — acc_flams dominant, ratchets escalate R3→R4, extended rests then bursts
- **P** Sparse — heavy REST self-loops, single hits only, ratchets/flams essentially absent

### Hit States & Accent Levels
| State | Triggers | Out B |
|-------|----------|-------|
| REST | none | 0V |
| HIT | 1 | ~2V |
| ACC_HIT | 1 (wide bar) | ~5V |
| FLAM | grace + main | ~3V |
| ACC_FLAM | grace + accented main | ~5V |
| RATCHET_2 | 2 evenly spaced | ~4V |
| RATCHET_3 | 3 evenly spaced | ~3V |
| RATCHET_4 | 4 evenly spaced | ~2V |

### Chaos & Density implementation
```cpp
// Chaos: same formula as MarkoV (blend toward flat weight 8)
// Density: symmetric around 128 (neutral)
// REST:  w = (w * (256 - density)) >> 7   // 128→1×, 256→0, 0→2×
// hits:  w = (w * density) >> 7            // 128→1×, 256→2×, 0→0
// CV mapping: int density = constrain(128 + Proportion(In(1), HEMISPHERE_MAX_INPUT_CV, 128), 0, 256);
```

### Display layout
```
                 [Seed]  ← hint y=2, EditMode only
[S]    [42%]   [dice]   ← y=15: profile@1, chaos@22, dice@54
──────────────────────   ← separator y=25
[bar graph]              ← height=accent level (0–5), horizontal bands for multi-trigger states
──────────────────────   ← baseline y=63 (GY=25, GH=37, ybot=62)
```
Bar graph: `GY=25, GH=37`. REST = dot at baseline. Bands: flam/ratchet_2 = 2, ratchet_3 = 3, ratchet_4 = 4.

### Data Persistence (OnDataRequest bit layout)
| Bits | Field | Width |
|------|-------|-------|
| 0–1 | profile | 2 |
| 2–4 | hit_state | 3 |
| 5–11 | chaos_base | 7 |
| 12–14 | seed | 3 |
| 15–46 | rng_seed | 32 |

---

## UgliApp Applet
**File:** `software/src/applets/UgliApp.h`
**Branch:** `UgliApp`
**Registry ID:** 92, category `0x10` (Utility)

Minimal CV passthrough diagnostic. Reads both CV inputs, passes them to matching outputs, displays live voltage on screen. No parameters, no persistence.
