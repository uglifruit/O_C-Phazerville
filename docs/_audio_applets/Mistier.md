---
layout: default
---
# Mistier — Live Granular Audio Processor

(TODO: screenshot)

## License

MIT License  
(c) 2026 Andy Jenkinson (uglifruit) — with ClaudeCode.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## Overview

Mistier is a live granular audio processor for the Phazerville/Hemisphere audio applet framework. It continuously records incoming audio into a 1-second PSRAM circular buffer and plays it back as a cloud of overlapping grains.

Inspired by Mutable Instruments Clouds, Mistier extends the simpler [Mist](Mist.md) applet with:
- Continuous grain window morphing (Texture)
- Density centred at silence, with stochastic or periodic modes
- Grain feedback — the granular output feeds back into the recording buffer
- Post-grain reverb, with a multi-mode Blend control

Available as a **MONO** applet.

---

## Signal Flow

```
Audio In ──► AudioEffectClouds (grain cloud) ──► wet ──────────────────┐
wet      ──► Reverb ──────────────────────────► reverb ────────────────┤──► Output
Audio In ──────────────────────────────────────► dry ──────────────────┘
```

The wet (granular), reverb, and dry signals are mixed by the **Blend** and **Mix** parameters.

---

## Parameters

### Page 1

| Param | Range | Description |
|-------|-------|-------------|
| **Pos** | 0–100% | Read position in the buffer. 0% = current (live), 100% = 1 second ago. |
| **Den** | −20 to +20 Hz | Grain spawn rate, centred at silence. Negative = regular periodic; positive = stochastic (random timing). |
| **Sz** | 10–500 ms | Grain duration. Short = glitchy fragments; long = smooth smear. |
| **Spr** | 0–100% | Position scatter. Randomises each grain's start point around Pos. |

### Page 2

| Param | Range | Description |
|-------|-------|-------------|
| **Pt** | −12 to +12 st | Pitch shift in semitones. Unity = 0. |
| **WD/FB/RV** | 0–100% | Blend parameter — meaning changes with mode (see Blend Modes below). |
| **Tex** | 0–100% | Grain window shape. 0% = rectangular (harsh/clicky), 50% = triangle, 100% = Hann (smooth). |
| **Mix** | 0–100% | Output level / wet amount. |
| **Frz** | gate / latch | Freeze. Stops the write pointer so grains replay a fixed snapshot. |

All parameters except Frz accept CV modulation via assignable CV inputs.

---

## Blend Modes

The **Blend** knob is multi-functional. The Blend row on page 2 has two cursor positions:

- **Mode label cursor** (on "WD"/"FB"/"RV"): encoder cycles between modes. The label is underlined when this cursor is active.
- **Value cursor** (on the % number): encoder adjusts the blend amount.

| Mode | Label | Blend controls |
|------|-------|----------------|
| Wet/Dry | **WD** | Balance between granular (wet) and live (dry) signal. 0% = fully dry, 100% = fully wet. |
| Feedback | **FB** | Amount of grain output fed back into the recording buffer. Creates self-reinforcing granular textures. |
| Reverb | **RV** | Amount of reverb applied to the grain output. |

The current mode is shown as the label before the Blend value on page 2.

---

## Controls

| Control | Action |
|---------|--------|
| Encoder (browse) | Navigate cursor through parameters |
| Encoder (edit) | Adjust parameter value; on the mode label — cycle WD/FB/RV |
| Button | Enter/exit edit mode; assign CV input when cursor is on a CV slot |
| **Aux button** | Latch/unlatch manual freeze — for live performance without a gate cable |

---

## CV / Gate Inputs

Each parameter (except Frz and Blend mode) has an assignable CV input slot. Frz accepts a gate/digital input. Assign inputs using the in-applet CV input editor (press button when cursor is on a CV label).

---

## Density

Density is **centred at silence** (displayed as 0). This differs from Mist's always-positive density:

- **Positive values (+1 to +20 Hz)**: Stochastic — grains are spawned at randomised intervals, creating an irregular, diffuse cloud.
- **Negative values (−1 to −20 Hz)**: Periodic — grains are spawned at a steady, regular rate.
- **Zero**: No grains. Silence on the wet output.

---

## Texture

Texture continuously morphs the grain envelope shape:

| Value | Shape | Character |
|-------|-------|-----------|
| 0% | Rectangular | Hard edges — harsh, clicky |
| 50% | Triangle | Smooth fade in/out |
| 100% | Hann (sin²) | Softest, most musical |

---

## Freeze

Freeze halts the write pointer. Grains continue to spawn and play from the frozen buffer, creating a looping granular texture from the captured moment. Two ways to freeze:

- **Gate input** on the Frz parameter (hardware gate cable)
- **Aux button** latch (no cable needed — great for live use)

When latched, the **Frz** row label is shown inverted (white on black) on screen.

---

## Feedback

In **FB** mode, the Blend knob feeds a portion of the grain output back into the audio recording buffer. At low amounts this adds warmth and density. At high amounts it creates self-reinforcing feedback textures. With Freeze active, the feedback circulates endlessly within the frozen buffer.

---

## Activity Display

A row of pixels at the top of the display (y=7) shows active grain count in real time — one pixel per active grain, up to 12 maximum.

---

## Technical Notes

- Buffer: ~1 second stored in PSRAM (EXTMEM). Falls back to half-size in OCRAM if no PSRAM. Displays "No PSRAM" if unavailable.
- Grains: up to 12 simultaneous grains. Output scaled by 0.25 for headroom.
- Interpolation: Hermite cubic interpolation on grain read pointer.
- Window morph: implemented via a 256-entry Q15 Hann LUT in flash — no trigonometric functions in the audio hot loop.
- Reverb: Schroeder/Moorer architecture (4 comb + 2 allpass), shared with BungverbApplet.

---

## Files

| File | Purpose |
|------|---------|
| `audio_applets/MistierApplet.h` | UI and control layer (HemisphereAudioApplet) |
| `Audio/AudioEffectClouds.h` | DSP engine (AudioStream subclass) |
| `audio_applets/Mistier_CLAUDE.md` | Internal notes for Claude Code |
