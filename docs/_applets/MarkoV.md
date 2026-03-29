---
layout: default
---
# MarkoV

A first-order **Markov chain** melodic generator. On each clock, the next scale degree is chosen by weighted random selection from a transition matrix, where the weights depend on the current state. Three **Tendency Profiles** shape the character of the melody. A **Chaos** control continuously blends between the profile's structured weights and a flat uniform distribution, giving you a spectrum from composed to erratic.

---

### I/O

|        | 1/3 | 2/4 |
| ------ | :-: | :-: |
| TRIG   | Clock — advance to next state | Reset — short press returns to seed; long press sets a new random seed |
| CV INs | Chaos offset — adds to the encoder-set Chaos baseline | Transpose — V/Oct pitch offset applied after quantization |
| OUTs   | Quantized pitch CV | Trigger — pulses only when the quantized pitch actually changes |

_Output B will **not** fire if two consecutive Markov states happen to land on the same pitch in the chosen scale. This makes rhythmic variation a natural consequence of scale choice._

---

### UI Parameters

Turn the encoder to move between parameters. Click to enter edit mode, click again to lock.

| Display | Parameter | Range | Notes |
|---------|-----------|-------|-------|
| **S / T / J** | Matrix (Tendency Profile) | S, T, J | See profiles below |
| **Q1–Q4** | Scale | Q1–Q4 | Selects quantizer channel; dotted cursor = Aux opens full scale editor |
| **0–100%** | Chaos | 0–100% | Baseline chaos level; CV In 1 adds on top |

The top line of the display shows the name of the currently-selected parameter ("Matrix", "Scale", or "Chaos") as a reminder.

---

### Tendency Profiles

The heart of MarkoV. Each profile is an 8×8 transition matrix where rows = current state and columns = next state. Higher values = more likely. Weights range from 1 to 22, giving strong 20:1 ratios at 0% chaos so the character is clearly audible.

**S — Pentatonic Stability**
Root (degree 1) and fifth (degree 5) are overwhelmingly preferred arrivals from any state. The melody gravitates toward these anchor points with occasional passing tones. Ideal for drones, ostinatos, or melodic backgrounds that stay in one place.

**T — Chromatic Tension**
Strongly prefers stepwise motion (±1 scale degree). The melody snakes up and down, rarely leaping. Repetition of the same note is common. Creates a restless, chromatic feel — especially interesting with chromatic or microtonal scales.

**J — Jazz Tendencies**
The 7th scale degree is the dominant target from almost any state, creating the characteristic "reach for the 7th" gesture of jazz lines. The 3rd is a common secondary arrival. Root resolution from the 7th is strong. Tritone substitution is implied from the 4th.

---

### Chaos

At **0% chaos** the profile weights dominate — melodies are strongly shaped by the chosen tendency profile.

At **100% chaos** all transition weights are equalised — any next note is equally likely regardless of the current note or the profile. The profile has no effect.

**CV In 1** adds to the encoder-set Chaos baseline, so you can set a starting point with the encoder and modulate upward with voltage. Patching an LFO here creates continuously evolving melodic density.

---

### Reset & Seed

**Short press** on Digital In 2 (or as a trigger): returns to the **seed state** — the same starting scale degree every time. Use this to repeat a melodic phrase from a known starting point.

**Long press** (hold Digital In 2 for ~1 second): picks a **new random seed** and jumps to it. That new state becomes the new short-press reset point going forward. The seed is saved with presets.

---

### What is a Markov Chain?

A Markov chain is a mathematical model where the probability of the next event depends only on the current state — not on the history of how you got there. In MarkoV, each **state** is a scale degree (1–8), and the **transition matrix** defines how likely you are to jump to any other degree from your current position.

The key musical insight is that different melodic styles have characteristic interval patterns:
- Stable tonal music gravitates toward root and fifth
- Chromatic music moves in small steps
- Jazz leaps to the leading tone and resolves dramatically

By encoding these tendencies as probability weights, MarkoV can generate melodies that feel stylistically consistent without being deterministic or repetitive.

---

### Tips

- **Pair with a rhythmic gate**: MarkoV outputs a trigger on Output B only when the pitch changes, so it naturally generates rhythmic patterns when paired with a VCA or envelope. Dense scales produce more frequent triggers; sparse pentatonic scales create rests.
- **Transpose with a sequence**: Patching a sequencer into CV In 2 transposes the entire Markov melody, effectively key-modulating in real time.
- **Chaos as an arc**: Start at 0% chaos and slowly sweep toward 100% over a long period to create a gradual transition from composed to generative.
- **S profile with a minor pentatonic scale**: The pull to root and fifth combined with pentatonic pitch selection produces a reliable, melodic output suitable for leads.
- **J profile with a Dorian or mixolydian scale**: The 7th-degree bias takes on very different character depending on whether the 7th is major or minor.
