---
layout: default
---
# A-Mish

**A-Mish** is an interval-based melodic sequencer inspired by the interval-navigation concept pioneered by Eventide's Misha hardware. Rather than playing absolute pitches, incoming MIDI notes are interpreted as *relative jumps* through the active quantizer scale. The keyboard becomes a navigation device: which key you press determines how far to step up or down from the current position, not what pitch to play.

---

## The Core Idea

On a normal keyboard-to-CV setup, pressing C plays C. On A-Mish, pressing C (the reference note) *repeats* the current pitch. Pressing D moves one scale degree up. Pressing B moves one scale degree down. The actual pitch that comes out depends entirely on where you are in the scale at that moment — the keyboard controls *direction and distance*, not destination.

This separates two things that are normally coupled: *what interval to move* (determined by the key you press) and *what scale to move through* (set on the module). You can play the same keyboard gesture through a major scale, a pentatonic, a Phrygian, or a custom Scala scale and get a completely different melodic result each time.

---

## Interval Mapping

A-Mish uses a **diatonic (white-key) mapping** relative to the configured reference note:

| Incoming key | Degree delta | Example (ref = C4, 7-note scale) |
|---|---|---|
| Same white key as ref | 0 — repeat | C4 → stays |
| Next white key up | +1 | D4 → +1 scale degree |
| Two white keys up | +2 | E4 → +2 scale degrees |
| Three white keys up | +3 | F4 → +3 |
| Four white keys up | +4 | G4 → +4 |
| Five white keys up | +5 | A4 → +5 |
| Six white keys up | +6 | B4 → +6 |
| One octave up | +7 | C5 → +7 (full scale wrap) |
| One white key down | −1 | B3 → −1 |

Each additional keyboard octave adds ±7 degrees. The delta is calculated in white-key steps, not semitones, so the mapping is independent of which scale is loaded.

**Black keys** apply the same degree delta as the white key immediately below them, plus a +1 semitone chromatic nudge added *after* quantization.

| Black key (ref = C4) | Behaviour |
|---|---|
| C#4 / Db4 | Repeat (delta 0) + nudge up 1 semitone |
| D#4 / Eb4 | +1 degree + nudge |
| F#4 / Gb4 | +3 degrees + nudge |
| G#4 / Ab4 | +4 degrees + nudge |
| A#4 / Bb4 | +5 degrees + nudge |

---

## I/O

|  | 1 (left) | 2 (right) |
|---|:---:|:---:|
| **Digital** | Unused | Reset — snap position to root |
| **CV IN** | Semitone transpose (Transp) | Q-select offset (Qsel) |
| **OUT** | Quantized pitch | Gate or Trigger |

**Out A** outputs the current scale position looked up through the selected quantizer channel. The CV 1 transpose offset is added after quantization.

**Out B** is user-configurable as either:
- **GATE** — held high for as long as any MIDI note is held
- **TRIG** — a short pulse fired on each new note-on event

**Digital 2 (Reset):** A rising edge snaps the position back to degree 0 (root of the current scale). Useful for phrase boundaries or as a performance reset.

**CV 1 (Transp):** A raw semitone offset added to the output CV. Patch a sequencer here to transpose the entire A-Mish output in real time.

**CV 2 (Qsel):** A positive CV offset added to the base quantizer channel selection. At 0V the **Q** setting governs; higher voltages shift to higher-numbered channels.

---

## Controls

Four parameters, navigated by the encoder (rotate to move cursor, press to enter edit, press again to exit).

| Cursor | Parameter | Range | Notes |
|---|---|---|---|
| **Q** | Quantizer channel | Q1–Q8 | Selects the global quantizer scale and root |
| **Ch** | MIDI input channel | 1–16 | Which MIDI channel to listen on |
| **Ref** | Reference note | 0–127 | The "unison" key; displayed as note name + octave |
| **O2** | Out B mode | GATE / TRIG | Gate held vs. trigger pulse per note |

**Q** uses a dotted underline to indicate that **Aux** (the button below the right encoder) opens the full scale editor for the selected quantizer channel.

**Ref** defaults to MIDI note 60 (C4). Set it to whichever key on your MIDI keyboard you want to act as "repeat / unison."

---

## MIDI Output

A-Mish also sends MIDI note-on and note-off messages on the configured channel, mirroring the CV output. This allows A-Mish to drive software instruments or other hardware in parallel with the CV output.

Note-off is sent when all notes on the input channel are released.

---

## Display

```
Q1  Ch 1   C4          ← quantizer / MIDI channel / reference note
   [cursor underlines]
+2                      ← most recent degree delta (= / +N / -N)
● ○ ○ ○ ● ○ ○ ○        ← scale degree dots; filled = current position
A C4        B  GATE     ← output A with current note / output B with mode
```

The degree dot row shows one dot per scale degree (up to 16). The filled dot marks the current position modulo the scale size.

The delta readout shows `=` for a repeat, `+N` for an upward jump, `-N` for downward.

---

## Setup

1. Connect a MIDI keyboard to O_C via USB MIDI or a MIDI-to-CV interface.
2. Set **Ch** to match the keyboard's output channel.
3. Set **Q** to a quantizer channel and configure your scale (Aux to open the editor).
4. Set **Ref** to whichever note on your keyboard you want to be "repeat / unison." Middle C is the natural choice.
5. Patch **Out A** to a VCO V/Oct input.
6. Patch **Out B** to a VCA envelope or gate input.

From this point, playing your keyboard navigates through the scale. The white-key row from the reference note outward is your diatonic interval palette.

---

## Tips

- **Scale choice is the performance variable:** The same keyboard gesture through a pentatonic produces wide leaps; through a chromatic scale it produces half-steps. Change the quantizer scale mid-performance for tonal shifts without changing how you play.
- **Qsel CV:** Patch a slow LFO or step sequencer into CV 2 to shift through quantizer channels automatically while you play.
- **Ref note placement:** Setting Ref to the lowest note of your working range means all white keys above it move upward. Setting it to a middle key gives bidirectional reach without moving your hand.
- **Reset as downbeat:** Patch a clock division or footswitch into Digital 2 to snap back to root at bar boundaries.
- **GATE mode + slow envelope:** Hold a key to sustain the current pitch. Release to close the gate. A-Mish becomes a slow, deliberate voice.
- **TRIG mode + envelope:** Each new note fires a short trigger regardless of how long you hold it. More predictable envelope behaviour when playing quickly.
- **CV 1 transpose:** Patch a step sequencer or LFO into CV 1 to transpose the entire A-Mish output between keys — A-Mish handles intervallic navigation while the sequencer handles key centre.
- **Black keys for colour:** In a diatonic scale, black keys produce pitches one semitone above the nearest in-scale note. Use them sparingly as expressive ornaments.
- **Pair with MarkoV:** Use A-Mish for intentional real-time melodic navigation and MarkoV for autonomous generative lines, both driven from the same quantizer channel so they stay in key.

---

## Credits

A-Mish by uglifruit. Inspired by the interval-navigation concept of the Eventide Misha.
