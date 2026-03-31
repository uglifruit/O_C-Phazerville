---
layout: default
---
# UgliString

**UgliString** is a stereo Karplus-Strong resonant string synthesizer. A trigger plucks two detuned string voices in parallel; the sound is shaped by a Moog-style ladder filter and a software decay envelope. It lives in the Audio Applets system and must be selected as a **stereo source** (slot 0 in stereo mode).

---

## What is Karplus-Strong?

Karplus-Strong synthesis models a plucked string by filling a short delay buffer with noise, then recirculating it through a smoothing filter. The result decays naturally as the high frequencies damp out — producing a bright attack that settles into a warm resonant tone. The fundamental frequency is determined by the buffer length, which is set from the pitch CV on each trigger.

The minimum pitch is approximately **E2 (~82 Hz)**, set by the maximum buffer size. There is no upper limit other than the Nyquist frequency.

---

## Getting Started

UgliString is a **stereo audio applet**. To use it:

1. Navigate to the Audio Applets page.
2. Move to slot 0 (the source slot).
3. Press both encoder buttons simultaneously to switch the slot to **stereo mode**.
4. Press an encoder to enter applet selection, then scroll to **UgliStr**.
5. Confirm. The applet is now active.

Patch a trigger or gate into a physical input, then assign it to the **Trg** cursor.

---

## I/O

| | Function |
|---|---|
| **CV 1** | V/Oct pitch (assignable via Pitch cursor) |
| **Trg** | Trigger source — assignable to any digital input, gate, or clock |
| **Out L** | Left string voice |
| **Out R** | Right string voice (detuned by Detune amount) |

There is no fixed hardware jack assignment. Both CV inputs and trigger are routed through the assignable input maps — set the source for each via their respective cursors.

---

## Parameters

| Cursor | Parameter | Range | Notes |
|--------|-----------|-------|-------|
| **Pitch** | Base pitch | C1–C8 | Tuning indicator + Hz readout; encoder steps by semitone |
| **Pitch CV** | Pitch CV source | assignable | V/Oct; added to base pitch before pluck |
| **Trg** | Trigger source | assignable | Rising edge plucks both strings and resets envelope |
| **Dec** | Decay | 0–100% | Envelope decay time: ~30ms (0) to ~5s (100) |
| **Brt** | Brightness | 0–100% | Ladder filter cutoff: 200Hz–20kHz (logarithmic) |
| **B** | Body | 0–100 | Ladder filter resonance; higher values add warmth and sustain |
| **D** | Detune | 0–50ct | L/R pitch spread in cents; 0 = mono, 50 = wide chorus |

---

## Signal Flow

```
Trigger → synthL.noteOn(pitch / detune_ratio)
        → synthR.noteOn(pitch × detune_ratio)
        → env_level = 1.0 (envelope reset)

synthL → filterL (ladder) → vcaL ─┐
synthR → filterR (ladder) → vcaR ─┴─ stereo output
         exponential envelope ──────→ both VCA CV ports
```

Both filters share the same Brightness and Body settings. The envelope is a pure exponential decay applied in software via a VCA — the Karplus-Strong synth's own internal damping is separate and always present.

---

## Controls in Detail

### Pitch
Rotates by semitone. The display shows a tuning indicator (note name with cent offset) and the frequency in Hz. Assign a V/Oct source via the adjacent Pitch CV cursor to track a keyboard or sequencer.

### Trg (Trigger)
Assign any source using the encoder — physical digital inputs, internal clock divisions, gate CV, MIDI, etc. A rising edge (gate low→high) plucks both strings simultaneously at the current pitch and resets the decay envelope to full.

Patch a slow clock for arpeggiated plucks, or a gate sequencer for rhythmic phrases. Because pitch is sampled at the moment of the trigger, you can update the pitch CV between triggers freely.

### Dec (Decay)
Controls how quickly the envelope fades after a pluck. Short values (0–20%) give a percussive snap; long values (80–100%) let the string ring for several seconds. The Karplus-Strong algorithm has its own internal high-frequency damping on top of this — at very long decay settings the string will naturally lose brightness before the envelope fully fades.

### Brt (Brightness)
Sets the cutoff frequency of the 4-pole Moog-style ladder filter that follows each synth voice. At low values (dark, 200Hz) the output is muffled and body-heavy. At high values (bright, 20kHz) the filter is effectively open. The scale is logarithmic so the useful range feels even across the full sweep.

### B (Body)
Sets the resonance of the ladder filter (0–100, mapping to 0.0–1.7 internally). Low values give a neutral filter character. Higher values emphasise the cutoff frequency and add warmth and sustain. The maximum is kept just below self-oscillation.

### D (Detune)
Spreads the left and right string voices apart in pitch. At 0 cents the two voices are identical (mono). At 50 cents they are a quarter-tone apart, producing a wide chorus/unison effect. Values of 5–15 cents give a natural ensemble width without audible beating.

---

## Tips

- **Pitch CV timing:** Pitch is sampled at the moment of each trigger. Changing the CV between triggers lets you play melodies — pair with a step sequencer on the pitch input and a clock on Trg.
- **Body + Brightness together:** High Body with low Brightness gives a warm, woody resonance. High Body with high Brightness approaches a singing, almost bowed quality.
- **Slow decay + reverb processor:** Stack UgliString in slot 0 with a reverb in slot 1. Long decay times blur into the reverb tail naturally.
- **Detune at 0 for bass:** Set Detune to 0, pitch low, Brightness dark, Body moderate — gives a clean bass pluck without stereo spread.
- **Detune wide for pads:** High Detune (30–50ct), long Decay, moderate Brightness — the beating between L and R creates movement without any modulation source.
- **Trigger from MarkovPerc:** Route MarkovPerc Out A (trigger) to a physical input and assign it as the Trg source. MarkovPerc drives rhythm and accent; UgliString provides pitched timbre.

---

## RAM Usage

UgliString uses approximately **28KB of RAM2** (DMAMEM) for the two Karplus-Strong buffers (536 samples × 2 channels × 2 bytes), two ladder filter state blocks, and VCA objects. This is allocated from the pool along with all other audio applets and does not affect the hemisphere applet RAM.

---

## Credits

UgliString by uglifruit.
