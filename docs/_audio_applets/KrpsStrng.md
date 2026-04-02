---
layout: default
---
# KrpsStrng

**KrpsStrng** is a mono Karplus-Strong resonant string synthesizer. A trigger plucks a string voice; the sound is shaped by a Moog-style ladder filter and a software decay envelope. It is a **mono source applet** — each side runs an independent instance, so left and right channels can be pitched and triggered separately.

---

## What is Karplus-Strong?

Karplus-Strong synthesis models a plucked string by filling a short delay buffer with noise, then recirculating it through a smoothing filter. The result decays naturally as the high frequencies damp out — producing a bright attack that settles into a warm resonant tone. The fundamental frequency is determined by the buffer length, set from the pitch CV at the moment of each trigger.

The minimum pitch is approximately **E2 (~82 Hz)**, set by the maximum buffer size. There is no upper limit other than the Nyquist frequency.

---

## Getting Started

KrpsStrng is a **mono audio source applet**. To use it:

1. Navigate to the Audio Applets page.
2. Move to slot 0 (the source slot).
3. Press an encoder to enter applet selection, then scroll to **KrpsStrng**.
4. Confirm. The applet is now active on that side (left or right independently).

Patch a trigger or gate into a physical input, then assign it to the **Trg** cursor. Assign a V/Oct source to the **Pitch CV** cursor for pitched playback.

---

## I/O

| | Function |
|---|---|
| **Pitch CV** | V/Oct pitch source — assignable to any CV input |
| **Trg** | Trigger source — assignable to any digital input, gate, or clock |
| **Audio In** | Dry signal (mixed with the string via Mix) |
| **Audio Out** | String voice mixed with dry input |

There are no fixed hardware jack assignments. All inputs are routed through the assignable input maps — set the source for each via its cursor.

---

## Parameters

| Cursor | Parameter | Range | Notes |
|--------|-----------|-------|-------|
| **Pitch** (note) | Base pitch | C1–C8 | Steps by semitone; tuning indicator shown |
| **Pitch** (Hz) | Fine pitch | — | Steps by ~3 cents; Hz readout |
| **Pitch CV** | Pitch CV source | assignable | V/Oct; added to base pitch at pluck time |
| **Trg** | Trigger source | assignable | Rising edge plucks the string and resets the envelope |
| **Dec** | Decay | 0–100 | Envelope decay: ~30ms (0) to ~5s (100); CV-able |
| **Br** | Brightness | 0–100 | Ladder filter cutoff: 200Hz–20kHz (log); CV-able |
| **Bdy** | Body | 0–100 | Ladder filter resonance 0.0–1.7; CV-able |
| **Mix** | Dry/Wet | 0–100 | 0 = full dry (passthrough), 100 = string only; CV-able |

All CV-able parameters show their assigned source to the right of the value. Button press on a CV cursor opens the attenuverter editor.

---

## Pitch Editing

The pitch row has two active cursor positions:

- **Note name** (left): encoder steps by **1 semitone** — use this to set the base key
- **Hz readout** (right): encoder steps by **~3 cents** — use this for fine tuning between semitones

---

## Signal Flow

```
Trigger → StartADCLag → Pluck() after ADC settles
          (pitch CV read after lag, so new pitch is used immediately)

synth → filter (ladder) → vca ──┐
audio in ───────────────────────┤ mixer → output
exponential envelope ───────────→ vca CV port
```

Pitch is sampled at pluck time, after the ADC lag timer expires. This means you can change the pitch CV on every trigger step and the correct pitch will always be used.

---

## Controls in Detail

### Pitch
The note name steps by semitone; the Hz display steps by ~3 cents for fine adjustment. Assign a V/Oct source via the adjacent Pitch CV cursor to track a keyboard or sequencer.

### Trg (Trigger)
Assign any source using the encoder — physical digital inputs, internal clock, gate CV, MIDI, etc. A rising edge plucks the string and resets the decay envelope to full.

Patch a slow clock for arpeggiated plucks, or a gate sequencer for rhythmic phrases. Pitch CV is sampled after a short ADC settling delay so the new pitch is always used, even when pitch and trigger change simultaneously.

### Dec (Decay)
Controls how quickly the envelope fades after a pluck. Short values (0–20) give a percussive snap; long values (80–100) let the string ring for several seconds. The Karplus-Strong algorithm has its own internal high-frequency damping on top of this — at very long decay settings the string will naturally lose brightness before the envelope fully fades.

### Br (Brightness)
Sets the cutoff frequency of the 4-pole Moog-style ladder filter. At low values the output is muffled and body-heavy. At high values the filter is effectively open. The scale is logarithmic so the useful range feels even across the full sweep.

### Bdy (Body)
Sets the resonance of the ladder filter (0–100, mapping to 0.0–1.7 internally). Low values give a neutral filter character. Higher values emphasise the cutoff frequency and add warmth and sustain. The maximum is kept just below self-oscillation.

### Mix
Blends the dry audio input with the string voice. At 0 the output is the unprocessed input (passthrough). At 100 the output is the string only. Intermediate values layer the string over the input — useful for blending with a carrier or adding string resonance to an existing signal.

---

## Tips

- **Pitch CV timing:** Pitch is sampled at each trigger after a short ADC settling delay. Changing the CV between triggers lets you play melodies — pair with a step sequencer on the pitch input and a clock on Trg.
- **Body + Brightness together:** High Body with low Brightness gives a warm, woody resonance. High Body with high Brightness approaches a singing, almost bowed quality.
- **Slow decay + reverb:** Stack KrpsStrng in slot 0 with a reverb in slot 1. Long decay times blur into the reverb tail naturally.
- **Two independent sides:** Each hemisphere runs its own KrpsStrng instance. Pan them apart for stereo, or set different pitches for two-voice polyphony.
- **Trigger from MarkovPerc:** Route MarkovPerc Out A (trigger) to a physical input and assign it as the Trg source. MarkovPerc drives rhythm; KrpsStrng provides pitched timbre.

---

## Credits

KrpsStrng by uglifruit.
