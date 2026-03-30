# O_C-Phazerville Fork — Claude Context

## Repository
- Fork of `djphazer/O_C-Phazerville` at `uglifruit/O_C-Phazerville`
- Target hardware: **Teensy 4.1** (build env `T41`)
- Build system: **PlatformIO** via `~/.platformio/penv/Scripts/pio.exe`
- Build command: `cd software && ~/.platformio/penv/Scripts/pio.exe run -e T41`
- Hex output: `software/.pio/build/T41/*.hex`

## Branch Strategy
Each applet lives on its own branch off `phazerville` (the upstream main). This keeps diffs minimal and PRs clean for upstream submission.

| Branch | Purpose |
|--------|---------|
| `phazerville` | Upstream base — do not commit here |
| `UgliApp` | CV passthrough + voltage display applet |
| `MarkoV` | Markov chain melodic generator applet |

To add a new applet branch: `git checkout phazerville && git checkout -b <name>`

## Adding a New Applet
Two files must be changed, everything else is self-contained in the applet header:

1. **`software/src/applets/<Name>.h`** — the applet class (see below)
2. **`software/src/hemisphere_config.h`** — two additions:
   - `#include "applets/<Name>.h"` (alphabetical order near line 104)
   - `DeclareApplet<Name>{id, category}` in the `AppletRegistry reg{...}` block
   - ID must be unique across all applets (highest in use: 93 for MarkoV)
   - Category bitmask: `0x01`=LFO, `0x02`=Sequencer, `0x04`=Clock, `0x08`=Pitch, `0x10`=Utility, `0x20`=MIDI, `0x40`=Logic, `0x80`=Audio

## Applet Class Structure
All applets extend `HemisphereApplet`. Pure virtual methods that must be implemented:

```cpp
const char* applet_name()          // max 9 chars
void Start()                       // one-time init
void Controller()                  // runs every tick (~16kHz ISR)
void View()                        // render 64x64 display region
void OnEncoderMove(int direction)  // +1 or -1
uint64_t OnDataRequest()           // serialize state for save
void OnDataReceive(uint64_t data)  // deserialize state
// SetHelp() in protected:         // I/O label strings
```

Key optional overrides: `OnButtonPress()`, `AuxButton()`, `Reset()`

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
int Proportion(int value, int max_value, int max_out) // scale value
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

## MarkoV Applet
**File:** `software/src/applets/MarkoV.h`
**Branch:** `MarkoV`
**Registry ID:** 93, category `0x02` (Sequencer)

### Concept
First-order Markov chain melodic generator. 8 states represent scale degrees. On each clock, the next state is chosen by weighted random selection from the current row of a transition matrix. Four profiles define the matrix weights.

### I/O
| Jack | Function |
|------|---------|
| Dig 1 | Clock — advances the chain |
| Dig 2 | Reset — short press = replay from seed (same RNG sequence), long press (5000 ticks) = new random seed |
| CV 1 | Chaos offset — adds to encoder-set chaos_base; higher V = flatter distribution |
| CV 2 | Transpose — raw V/Oct offset added after quantization |
| Out A | Quantized pitch |
| Out B | Trigger pulse — fires only when quantized pitch actually changes |

### Parameters (encoder-navigated, 4 cursors)
| Cursor | Param | Range | Notes |
|--------|-------|-------|-------|
| 0 | Matrix | S / T / J / G | Stability, Tension, Jazz, Glacial |
| 1 | Scale | Q1–Q4 | Absolute quantizer channel; Aux opens editor |
| 2 | Chaos | 0–100% | Baseline; CV 1 adds on top |
| 3 | Seed | — | Dice icon; encoder re-rolls, Aux re-rolls; Dig 2 short resets to seed |

### Profiles
- **S** Pentatonic Stability — root/fifth attractors, 20:1 weight ratios
- **T** Chromatic Tension — stepwise motion dominates, snake-like lines
- **J** Jazz — strong pull to 7th from everywhere, root resolution from 7th
- **G** Glacial — heavy self-loops (~50%), only ±1 movement, near-zero leaps

### Chaos Implementation
Integer fixed-point only (no floats in hot path):
```
chaos_256 = (chaos_pct * 256) / 100
w[j] = (profile_weight[j] * (256 - chaos_256) + 8 * chaos_256) >> 8
```
At chaos=0: pure profile weights. At chaos=100: all weights = 8 (uniform).

### Trigger Logic
Output B compares the **quantized CV value** (`prev_cv`) not the raw Markov state. Two different states that quantize to the same pitch will not fire a trigger.

### Seed & Reset
- `seed` = start state; `rng_seed` = RNG seed for deterministic sequence
- Short press Dig 2 → `randomSeed(rng_seed); state = seed` — identical replay
- Long press Dig 2 (≥5000 ticks) → new `rng_seed` + `seed`, locks new loop
- Encoder on Seed cursor → re-rolls immediately, flashes dice icon ~500ms
- Aux on Seed cursor → same as long press
- reset_flash (~500ms invert of param row) fires on short Dig 2

### Display Layout
```
                [Seed]   ← hint text right-justified at y=2, EditMode only
[S] [Q1] [42%] [dice]   ← param line y=15: profile@1, scale@10, chaos@25, dice@54
──────────────────────   ← separator y=25
█  ██ █  ██ █  ██  █    ← scrolling bar graph, 8 bars, oldest→newest left→right
──────────────────────   ← baseline y=63
```

### Registry Position
Placed between MidiLoop and hMIDIIn in hemisphere_config.h (alphabetical M section).

### Data Persistence (OnDataRequest bit layout)
| Bits | Field | Width |
|------|-------|-------|
| 0–1 | profile | 2 |
| 2–4 | state | 3 |
| 5–6 | qselect | 2 |
| 7–13 | chaos_base | 7 |
| 14–16 | seed | 3 |
| 17–48 | rng_seed | 32 |

## MarkovPerc Applet
**File:** `software/src/applets/MarkovPerc.h`
**Branch:** `MarkovPerc`
**Registry ID:** 94, category `0x02` (Sequencer)

### Concept
Rhythmic sibling to MarkoV. States are hit types (REST, HIT, ACC_HIT, FLAM, ACC_FLAM, RATCHET_2/3/4). A Markov chain chooses the next hit pattern, creating a drummer with evolving style and internal memory. Four profiles define the matrix weights.

### I/O
| Jack | Function |
|------|---------|
| Dig 1 | Clock — advances the chain |
| Dig 2 | Reset — short press = replay from seed (same RNG sequence), long press (5000 ticks) = new seed |
| CV 1 | Chaos — flattens transition distribution (more erratic fills) |
| CV 2 | Density — biases toward hits vs rests (positive V = more hits) |
| Out A | Trigger — fires sub-triggers for ratchets/flams within the beat |
| Out B | Accent CV — 0–5V held for full clock period; level = beat accent of current state |

### Parameters (encoder-navigated, 3 cursors)
| Cursor | Param | Range | Notes |
|--------|-------|-------|-------|
| 0 | Matrix | S / T / J / P | Steady, Syncopated, Jazz, Sparse |
| 1 | Chaos | 0–100% | Baseline; CV 1 adds on top |
| 2 | Seed | — | Dice icon; encoder re-rolls, Aux re-rolls; Dig 2 short resets to seed |

### Profiles
- **S** Steady (Rock/Pop) — gravitates to hits/accented hits, rests brief, ratchets rare
- **T** Syncopated (Funk/Latin) — rests structurally meaningful, flams very common, ratchet_2 frequent
- **J** Jazz/Free — ratchets common fills, accented flams signature, extended rests then dense bursts
- **P** Sparse — heavy REST self-loops, single hits only, ratchets/flams essentially absent

### Hit States & Accent Levels
| State | Triggers | Out B |
|-------|----------|-------|
| REST | none | 0V |
| HIT | 1 | ~2V (ACCENT_SOFT) |
| ACC_HIT | 1 | ~5V (ACCENT_FULL) |
| FLAM | 2 (grace + main) | ~3V (ACCENT_MED) |
| ACC_FLAM | 2 (grace + accented main) | ~5V (ACCENT_FULL) |
| RATCHET_2 | 2 evenly spaced | ~4V (ACCENT_HARD) |
| RATCHET_3 | 3 evenly spaced | ~3V (ACCENT_MED) |
| RATCHET_4 | 4 evenly spaced | ~2V (ACCENT_SOFT) |

Out B is set once per beat at the beat's accent level and held for the full clock period.

### Chaos & Density Implementation
Integer fixed-point, no floats:
```
chaos: blends profile weights toward flat (8) — same formula as MarkoV
density: scales REST weight inversely, hit weights directly
  REST:  w = (w * (256 - density)) >> 7   // density=128 → neutral
  hits:  w = (w * (128 + density/2)) >> 7
```

### Display Layout
```
                 [Seed]  ← hint text right-justified y=2, EditMode only
[S]    [42%]   [dice]   ← param line y=15: profile@1, chaos@22, dice@54
──────────────────────   ← separator y=25
[bar graph — height = accent level, horizontal bands for ratchets/flams]
──────────────────────   ← baseline
```
Bar graph: height proportional to accent level (0–5), horizontal bands subdivide bar for multi-trigger states (2 bands for flam/ratchet_2, 3 for ratchet_3, 4 for ratchet_4).

### Seed & Reset
Same mechanism as MarkoV — `seed` + `rng_seed` pair. Dig 2 short press triggers reset_flash (~500ms invert of param row).

### Registry Position
Placed after MarkoV in hemisphere_config.h (alphabetical M section).

### Data Persistence (OnDataRequest bit layout)
| Bits | Field | Width |
|------|-------|-------|
| 0–1 | profile | 2 |
| 2–4 | hit_state | 3 |
| 5–11 | chaos_base | 7 |
| 12–14 | seed | 3 |
| 15–46 | rng_seed | 32 |

## UgliApp Applet
**File:** `software/src/applets/UgliApp.h`
**Branch:** `UgliApp`
**Registry ID:** 92, category `0x10` (Utility)

Minimal CV passthrough diagnostic. Reads both CV inputs, passes them to matching outputs, displays live voltage on screen. No parameters, no persistence.
