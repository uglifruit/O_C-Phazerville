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
First-order Markov chain melodic generator. 8 states represent scale degrees. On each clock, the next state is chosen by weighted random selection from the current row of a transition matrix. Three profiles define the matrix weights.

### I/O
| Jack | Function |
|------|---------|
| Dig 1 | Clock — advances the chain |
| Dig 2 | Reset — short press = root (state 0), long press (5000 ticks) = random state |
| CV 1 | Chaos offset — adds to encoder-set chaos_base; higher V = flatter distribution |
| CV 2 | Transpose — raw V/Oct offset added after quantization |
| Out A | Quantized pitch |
| Out B | Trigger pulse — fires only when quantized pitch actually changes |

### Parameters (encoder-navigated, 3 cursors)
| Cursor | Param | Range | Notes |
|--------|-------|-------|-------|
| 0 | Matrix | S / T / J | Pentatonic Stability, Chromatic Tension, Jazz |
| 1 | Scale | Q1–Q4 | Absolute quantizer channel; Aux opens editor |
| 2 | Chaos | 0–100% | Baseline; CV 1 adds on top |

### Chaos Implementation
Integer fixed-point only (no floats in hot path):
```
chaos_256 = (chaos_pct * 256) / 100
w[j] = (profile_weight[j] * (256 - chaos_256) + 8 * chaos_256) >> 8
```
At chaos=0: pure profile weights. At chaos=100: all weights = 8 (uniform).
Profile weights use 20:1 ratios (max 22, min 1) so profiles are clearly audible.

### Trigger Logic
Output B compares the **quantized CV value** (`prev_cv`) not the raw Markov state. Two different states that quantize to the same pitch will not fire a trigger.

### Reset & Seed
- Short press Dig 2 → return to `seed` state (repeatable reset point)
- Long press Dig 2 (≥5000 ticks) → pick new `random(NUM_STATES)`, store as `seed`
- Seed is persisted in `OnDataRequest`

### Display Layout
```
Matrix                ← cursor label at top (y=6): "Matrix", "Scale", or "Chaos"
[S]  [Q1]  [42%]     ← param line (y=15), cursor underlines below (y=23)
─────────────────     ← separator (y=25)
█  ██ █  ██ █  ██    ← scrolling bar graph, 8 bars, oldest→newest left→right
─────────────────     ← baseline (y=63)
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

## TruthCat4 Applet
**File:** `software/src/applets/TruthCat4.h`
**Branch:** `UGLIALL-on-uglimods`
**Registry ID:** 84, category `0x46` (Logic + Sequencer + Utility)
**Doc:** `docs/_applets/TruthCat4.md`

### Concept
Truth-table rhythm generator. A 5-bit counter drives five Boolean inputs A–E. The expression `A op₀ B op₁ C op₂ D op₃ E` is evaluated using one of 16 NOR/NAND operator patterns and one of 14 Catalan parse trees (C₄ = 14). OUT1 = main Boolean result as trigger. OUT2 = related rhythm derived from the adjacent tree comparison or other modes. Fully deterministic — same settings always produce the same rhythm. 32-step default cycle.

### Mathematics
- **5 inputs:** A = bit4, B = bit3, C = bit2, D = bit1, E = bit0 of counter
- **4 operator slots:** op₀–op₃, each independently NOR or NAND
- **16 operator patterns:** ordered by NAND density (0 NANDs → 4 NANDs); `O` = NOR, `A` = NAND in display
- **14 parse trees:** the 14 Catalan C₄ bracketings; internal 0-indexed (display 1-indexed)
- **OUT2 "adjacent tree":** same step evaluated with `(tree_mod + 1) % 14` — NOT next step
- **Sentinel values:** `OPS_CV_CTRL = 16`, `TREE_CV_CTRL = 14`
- **224 distinct OUT1 rhythms** (16 patterns × 14 trees)

### I/O
| Jack | Function |
|------|---------|
| TR1 | Clock — advance counter |
| TR2 | Mode-dependent (RST/HLD/TrAdv/OpAdv) |
| CV1 | Operator pattern (when ops_base == 16) |
| CV2 | Parse tree (when tree_base == 14) |
| OUT1 | Main Boolean result — trigger pulse |
| OUT2 | Related rhythm — trigger pulse |

Outputs are **ClockOut triggers**, not gates — adjacent true steps both fire.

### Parameters (5 cursors, ordered top-to-bottom left-to-right)
| Cursor | Param | Range | Notes |
|--------|-------|-------|-------|
| OPS | Operator pattern | 0–15, or CV (16) | Arrow icons ↑=NOR ↓=NAND |
| STEPS | Sequence length | index 0–12 into {3,4,5,6,7,8,10,12,15,16,20,24,32} | Default idx 12 = 32 steps |
| TREE | Parse tree | 0–13, or CV (14) | Display 1-indexed |
| TR2_MODE | TR2 function | RST/HLD/TrAdv/OpAdv | |
| OUT2_MODE | OUT2 relation | NXT/NOT/AND/OR/XOR/FLP | Default XOR |

### evalTree — 14 Catalan C₄ switch cases (0-indexed)
```cpp
case  0: applyOp(ops[0], A, applyOp(ops[1], B, applyOp(ops[2], C, applyOp(ops[3], D, E))))  // right-spine
case  1: applyOp(ops[0], A, applyOp(ops[1], B, applyOp(ops[3], applyOp(ops[2], C, D), E)))
case  2: applyOp(ops[0], A, applyOp(ops[2], applyOp(ops[1], B, C), applyOp(ops[3], D, E)))
case  3: applyOp(ops[0], A, applyOp(ops[3], applyOp(ops[1], B, applyOp(ops[2], C, D)), E))
case  4: applyOp(ops[0], A, applyOp(ops[3], applyOp(ops[2], applyOp(ops[1], B, C), D), E))
case  5: applyOp(ops[1], applyOp(ops[0], A, B), applyOp(ops[2], C, applyOp(ops[3], D, E)))
case  6: applyOp(ops[1], applyOp(ops[0], A, B), applyOp(ops[3], applyOp(ops[2], C, D), E))
case  7: applyOp(ops[2], applyOp(ops[0], A, applyOp(ops[1], B, C)), applyOp(ops[3], D, E))
case  8: applyOp(ops[2], applyOp(ops[1], applyOp(ops[0], A, B), C), applyOp(ops[3], D, E))
case  9: applyOp(ops[3], applyOp(ops[0], A, applyOp(ops[1], B, applyOp(ops[2], C, D))), E)
case 10: applyOp(ops[3], applyOp(ops[0], A, applyOp(ops[2], applyOp(ops[1], B, C), D)), E)
case 11: applyOp(ops[3], applyOp(ops[1], applyOp(ops[0], A, B), applyOp(ops[2], C, D)), E)
case 12: applyOp(ops[3], applyOp(ops[2], applyOp(ops[0], A, applyOp(ops[1], B, C)), D), E)
case 13: applyOp(ops[3], applyOp(ops[2], applyOp(ops[1], applyOp(ops[0], A, B), C), D), E)  // left-spine
```

### Display Layout
```
y=0–9:   Context help area — cleared + redrawn for OPS/TR2_MODE/OUT2_MODE cursors
         OPS cursor:     ↑NOR  ↓NAND  (UP_ICON x=0, "NOR" x=9, DOWN_ICON x=31, "NAND" x=39)
         TR2_MODE:       "Trg input"
         OUT2_MODE:      "Output 2"
y=15:    "Op:" + 4 arrow icons at x=19,28,37,46 (UP=NOR, DOWN=NAND) or "CV"
y=25:    pad-right counter+1 / length  "T:" tree+1 (tree_x=37)
y=34:    tr2_names[tr2_mode]   OutputLabel(1) ":" out2_names[out2_mode]  (out2 starts x=30)
y=45:    OutputLabel(0) + 16 history dots (x = 8 + i*3, gfxRect 2×5 if high)
y=55:    OutputLabel(1) + 16 history dots
```

**Cursor underlines (gfxCursor):**
```
OPS:      y=23, x=0,  w=62 (w=30 if CV mode)
STEPS:    y=33, x=19, w=13 (w=7 if single-digit length)
TREE:     y=33, x=37, w=23
TR2_MODE: y=42, x=0,  w=29
OUT2_MODE:y=42, x=30, w=33
```

### Header clearing
The framework draws the applet name at y=0–9 as white pixels. To replace with context help:
```cpp
graphics.clearRect(gfx_offset, 0, 64, 10);  // zero the pixel buffer for this hemisphere's header strip
// gfx_offset = (hemisphere & 1) * 64
```
Do NOT use `gfxInvert` — it XORs pixels, so calling it twice restores the original. Do NOT use `gfxRect` — it draws white pixels. `graphics.clearRect` is the only way to black out the header.

### OutputLabel
`OutputLabel(ch)` returns the correct capital letter for the hemisphere channel — A/B for top-left, C/D for top-right, E/F for bottom-left, G/H for bottom-right. Always use this instead of hardcoding A/B.

### Data packing
| Bits | Field | Width |
|------|-------|-------|
| 0–3  | steps_idx | 4 |
| 4–8  | ops_base  | 5 (0–16) |
| 9–12 | tree_base | 4 (0–14) |
| 13–14| tr2_mode  | 2 |
| 15–17| out2_mode | 3 |

---

## TruthCat3 Applet
**File:** `software/src/applets/TruthCat3.h`
**Branch:** `UGLIALL-on-uglimods`
**Registry ID:** 85, category `0x46` (Logic + Sequencer + Utility)
**Doc:** `docs/_applets/TruthCat3.md`

### Concept
Compact sibling to TruthCat4. Uses a 4-bit counter (inputs A–D) and 3 operator slots. C₃ = 5 parse trees. 8 operator patterns × 5 trees = 40 distinct rhythms. Default 16-step cycle (full 4-bit range). UI identical to TruthCat4.

### Mathematics
- **4 inputs:** A = bit3, B = bit2, C = bit1, D = bit0 of counter
- **3 operator slots:** op₀ (A–B), op₁ (B–C), op₂ (C–D)
- **8 operator patterns:** ordered by NAND density (0 → 3 NANDs)
- **5 parse trees:** the 5 Catalan C₃ bracketings of `A op B op C op D`
- **OUT2 "adjacent tree":** same step evaluated with `(tree_mod + 1) % 5`
- **Sentinel values:** `OPS_CV_CTRL = 8`, `TREE_CV_CTRL = 5`

### Operator Patterns (8 total)
```
0: OOO  1: AOO  2: OAO  3: OOA  4: AAO  5: AOA  6: OAA  7: AAA
```

### evalTree — 5 Catalan C₃ switch cases (0-indexed)
```cpp
case 0: applyOp(ops[0], A, applyOp(ops[1], B, applyOp(ops[2], C, D)))   // A op (B op (C op D))
case 1: applyOp(ops[0], A, applyOp(ops[2], applyOp(ops[1], B, C), D))   // A op ((B op C) op D)
case 2: applyOp(ops[1], applyOp(ops[0], A, B), applyOp(ops[2], C, D))   // (A op B) op (C op D)
case 3: applyOp(ops[2], applyOp(ops[0], A, applyOp(ops[1], B, C)), D)   // (A op (B op C)) op D
case 4: applyOp(ops[2], applyOp(ops[1], applyOp(ops[0], A, B), C), D)   // ((A op B) op C) op D
```

### Key differences from TruthCat4
- `opPatterns[8][3]` not `[16][4]`
- `evalTree` takes 4 bool args (A,B,C,D) not 5
- `computeAltTree` / `computeOut2` pass 4 args; `O2_FLIP` flips 3 operators
- `opPatternStr` returns 3-char string (buf[4])
- Display: 3 arrow icons at x=19,28,37 (not 4)
- OPS cursor underline: w=55 (not 62) in non-CV mode
- Default `steps_idx = 9` (16 steps, not 12/32)

### Data packing
| Bits | Field | Width |
|------|-------|-------|
| 0–3  | steps_idx | 4 |
| 4–7  | ops_base  | 4 (0–8) |
| 8–10 | tree_base | 3 (0–5) |
| 11–12| tr2_mode  | 2 |
| 13–15| out2_mode | 3 |

---

## TruthCat Family — Shared Gotchas

1. **ClockOut not GateOut:** Outputs use `if (state) ClockOut(ch)` so adjacent true steps both fire as distinct triggers. `GateOut` would hold level and miss re-triggers on consecutive highs.
2. **Header clearing:** Use `graphics.clearRect(gfx_offset, 0, 64, 10)` — not `gfxInvert` (XOR restores), not `gfxRect` (draws white).
3. **opPatterns is `static constexpr` in-class:** Requires C++17. If a future build fails on this, move to file scope with a `TruthCatN_` prefix.
4. **CV is unipolar:** `In(ch)` can return negative values; `Proportion` result must be clamped to [0, N-1]. Negative CV has no effect (manual setting governs floor).
5. **TR2_HOLD uses `Gate(1)` not `Clock(1)`** — live level, not rising edge.
6. **TrAdv/OpAdv wrap:** Advance wraps at `CV_CTRL - 1` (not CV_CTRL), so the CV slot is never visited by TR2 advance. Manual encoder can reach CV sentinel.
7. **OUT2 "adjacent tree" is same step, next tree** — NOT next step in same tree. `computeAltTree` calls `evalTree(tree+1 % N, same_ABCD, same_ops)`.
8. **1-indexed display, 0-indexed internal:** Both applets display tree and step as 1-indexed but store 0-indexed. Always add +1 for display, use raw value for logic.

---

## UgliApp Applet
**File:** `software/src/applets/UgliApp.h`
**Branch:** `UgliApp`
**Registry ID:** 92, category `0x10` (Utility)

Minimal CV passthrough diagnostic. Reads both CV inputs, passes them to matching outputs, displays live voltage on screen. No parameters, no persistence.
