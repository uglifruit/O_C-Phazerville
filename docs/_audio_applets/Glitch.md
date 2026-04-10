---
layout: default
---
# Glitch (mono)

TODO: screenshot

A live-input stutter and ratchet effect. Audio is continuously recorded into a one-second
circular buffer. When the **HLD** gate goes high, the playhead freezes at the most recently
completed clock-length slice and loops it until the gate drops — creating classic buffer-freeze
stutters, reverse loops, ping-pong bounces, or tightly subdivided ratchets.

Wet/dry mix blends the frozen loop back against the live (always-recording) signal.

### Signal flow
```
input ──► [CircularBuffer — always recording]
       ├──► AudioEffectGlitch (wet: frozen slice loop)  ─┐
       └──► dry path                                      ├──► AudioMixer ──► output
                                                         ─┘
```

---

### Parameters

#### CLK — Clock source / DIV — Slice length
The **CLK** selector chooses which clock the slice length is derived from. When set to
**CLK1** or **CLK4**, the internal Phazerville master clock is used. When set to a
digital input (e.g. **TR1**), that jack's rising edges are measured instead.

The **DIV** setting then expresses the slice as a fraction (or multiple) of that beat:

| DIV  | Slice length relative to one beat |
|------|-----------------------------------|
| 1/2  | Half a beat (two-beat pattern loops) |
| 1/3  |             |
| 1/4  | One quarter (one beat) |
| 1/6  |             |
| 1/8  | Eighth note |
| 1/16 | Sixteenth note (default) |
| 1/32 | Thirty-second note |
| 1/64 | Sixty-fourth note — very short stabs |

**Example:** CLK1 at 120 BPM = 0.5 s/beat. DIV 1/16 → slice = 0.031 s (31 ms).
Setting CLK to **TR1** with a tap-tempo cable overrides the internal clock entirely.

> The applet measures the period between consecutive rising edges on the selected
> source, so patching any periodic signal — a divided clock, an LFO, even a gate
> sequencer — will set the reference tempo.

#### HLD — Hold / gate source
**HLD** is a gate input that must be held high for the entire duration of the glitch.
When the gate rises, the playhead locks to the most recently recorded slice. While the
gate stays high, that slice loops continuously. When the gate drops, the effect returns
immediately to live audio (the buffer never stopped recording, so there is no gap).

**AuxButton** latches and unlatches manual hold without a patched cable — useful for
performance. The `Hld:` label inverts to indicate an active manual latch.

> Short trigger pulses will produce very brief stutters. Long gates freeze audio for
> as long as the gate is held — try an envelope follower, a slow LFO gate, or a
> footswitch for hands-free control.

#### MOD — Playback mode / MOD CV
Selects how the frozen slice is looped:

| Mode | Display | Behaviour |
|------|---------|-----------|
| Forward    | `FWD` | Loop plays start → end, repeating |
| Reverse    | `REV` | Loop plays end → start, repeating |
| Ping-pong  | `PNG` | Alternates forward and reverse on each pass |
| Ratchet    | `RAT` | Subdivides the slice (see RCH below) |

**MOD CV** shifts the active mode by a CV voltage, allowing automated or random mode
switching mid-performance. At full CV swing the entire 0–3 range is covered.

#### RCH — Ratchet count / RCH CV *(visible only in RAT mode)*
When **MOD** is set to `RAT`, **RCH** subdivides the frozen slice into *N* equal
sub-loops, so the capture window is repeated *N* times per gate event at a higher rate.

| RCH | Effect |
|-----|--------|
| 1 | Same as FWD — no subdivision |
| 2 | Two repeats of the first half |
| 3 | Three repeats of the first third |
| 4 | Four repeats — classic drum-machine "ratchet" |
| 6 | Six rapid repeats — tremolo-like stutter |

**RCH CV** sweeps the ratchet count ±5 around the base value. Patching a random or
stepped CV source here gives unpredictable subdivision changes mid-loop.

#### MIX — Wet/dry balance / MIX CV
Blends the frozen/looped signal (wet) with the continuously live signal (dry) using
equal-power crossfading. `0%` = fully dry (bypass), `100%` = fully wet (default).

**MIX CV** adds to the base mix; negative attenuation pulls the mix toward dry even
when MIX is at 100%.

---

### Suggested patches

**Classic beat-repeat (TR-style)**
- Send a 1/4-note clock from a sequencer to **TR1**, set CLK = TR1, DIV = 1/16.
- Patch a momentary gate button or a clock divider output to **HLD**.
- Tap HLD on beat 3 or 4 of a bar to freeze and repeat the last sixteenth.

**Reverse ambient texture**
- Set MOD = `REV`, DIV = 1/2, MIX = 60%.
- Patch a slow LFO gate (high for ~1 beat, low for ~3 beats) to HLD.
- The backwards slice rises up underneath the forward signal as a shimmer.

**Ping-pong flutter**
- Set MOD = `PNG`, DIV = 1/8, MIX = 80%.
- Gate HLD for 2–4 beats. The slice bounces forward and backward, creating a
  flutter that sits between a flanger and a chorus.

**Drum ratchet / roll**
- Set MOD = `RAT`, DIV = 1/4, RCH = 4.
- Trigger HLD from a sequencer gate on snare hits.
- The snare transient is captured and fired four times within the 1/4-note window,
  producing a drum-machine roll effect.
- Patch an S&H random CV to RCH CV to randomise the subdivision per hit.

**CV-morphing glitch**
- Set MOD = `FWD`, patch a slow random voltage (e.g. a Turing Machine output) to
  MOD CV and RCH CV simultaneously.
- Enable manual hold (AuxButton). As the CV drifts, the mode and subdivision shift,
  producing continuously evolving glitch textures without any patched gate.

---

### Notes

- A 1-second PSRAM buffer is used when external PSRAM is present. Without PSRAM,
  the buffer shrinks to ~62 ms. The display shows "No PSRAM" and operation is
  limited to very short slices.
- A 64-sample (~1.3 ms) micro-fade is applied at slice loop boundaries to suppress
  clicks. For very short slices (< 128 samples) fading is skipped.
- Both mono and stereo slots are supported. In stereo, left and right channels share
  the same hold state and mode but use independent buffers; stereo image is preserved.
- The circular buffer records continuously regardless of hold state, so releasing
  hold always returns to the *current* live audio with no stale gap.

---

### Credits

Authored by Andy Jenkinson 'uglifruit' - using ClaudeCode.
DSP core (`AudioEffectGlitch`) and applet wrapper (`GlitchApplet`) released under the
MIT License.

```
MIT License
Copyright (c) 2026 Andy Jenkinson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
