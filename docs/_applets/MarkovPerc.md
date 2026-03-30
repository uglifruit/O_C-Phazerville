---
layout: default
---
# MarkovPerc

**MarkovPerc** is a rhythmic Markov chain generator — the percussive sibling to [MarkoV](MarkoV). Instead of scale degrees, the states are **hit types**: rests, plain hits, accented hits, flams, and ratchets. On each clock the chain selects the next hit type based on the current one, producing drum patterns that develop their own momentum and style without ever locking into a fixed loop.

---

## What is a Markov Chain?

A Markov chain is a system where the *next* state is chosen by weighted random probability based entirely on the *current* state. Each state has its own set of transition weights pointing toward every other state.

This puts it in a distinct category from the other rhythm generators you may know:

| Generator | How it works | Character |
|---|---|---|
| **Pure random** | No memory — each step is fully independent | Unpredictable, no groove tendency |
| **Euclidean** | Deterministic, fixed repeating pattern | Mathematical, predictable, loops exactly |
| **Clock divider / gate sequencer** | Fixed binary pattern | Loops with fills only by manual edit |
| **Markov chain** | Weighted probability from the current hit type | Groove tendency without fixed repetition |

The key quality is that drumming styles have characteristic *follow-on* patterns: a flam tends to come out of certain contexts; a rest after a dense ratchet is natural. A Markov chain encodes these tendencies as probabilities. You get the feeling of a drummer working within a style, rather than a pattern running on repeat.

---

## I/O

|        | 1 (left) | 2 (right) |
| ------ | :------: | :-------: |
| **TRIG** | Clock — advance chain | Reset — short = replay from seed; long = new seed |
| **CV IN** | Chaos offset | Density (hits vs. rests) |
| **OUT** | Trigger (Out A) | Accent CV, 0–5V (Out B) |

**Out A** fires the trigger events for each hit. Single hits fire once; flams fire a soft grace note immediately and the main hit slightly after; ratchets fire 2, 3, or 4 evenly-spaced pulses across the clock period.

**Out B** outputs a steady CV voltage for the full clock period representing the accent level of the current hit. 0V = REST, ~1V = grace note, ~2V = plain hit, ~3V = medium accent, ~4V = ratchet lead, ~5V = full accent. Patch this into a VCA, envelope modulation, or a velocity-sensitive drum module to get dynamic shaping of each hit.

**CV 1 (Chaos offset):** Adds to the encoder-set Chaos baseline. Set the CV 1 source to **None** when nothing is patched — a floating ADC input can read high and push Chaos toward 100%.

**CV 2 (Density):** Biases the chain toward hits or rests. Positive voltage makes rests less likely and hit states more likely, pushing toward continuous playing. No voltage = neutral, profile determines the natural balance.

---

## Controls

Three parameters, navigated by the encoder (rotate to move cursor, press to enter edit, press again to exit).

| Cursor | Parameter | Range | Notes |
|--------|-----------|-------|-------|
| **Matrix** | Tendency profile | S / T / J / P | Sets the transition weight table |
| **Chaos** | Randomisation | 0–100% | Blends weights toward flat/uniform |
| **Seed** | Loop anchor | — | Dice icon; controls the deterministic reset point |

**Seed** has no numeric value — the dice icon shows its current state. See Seed & Reset below.

---

## Hit States

| Abbrev | State | Triggers | Out B |
|--------|-------|----------|-------|
| `Rst` | REST | none | 0V |
| `Hit` | HIT | 1 | ~2V |
| `AcH` | ACC_HIT | 1 (wide bar shown) | ~5V |
| `Flm` | FLAM | grace note + main hit | ~3V |
| `AcF` | ACC_FLAM | grace note + accented main | ~5V |
| `Rc2` | RATCHET_2 | 2 evenly spaced | ~4V |
| `Rc3` | RATCHET_3 | 3 evenly spaced | ~3V |
| `Rc4` | RATCHET_4 | 4 evenly spaced | ~2V |

For **flams**, the grace note fires immediately; the main hit fires approximately 1/8 of the clock period later. For **ratchets**, triggers are evenly distributed across the clock period — the spacing adapts automatically to the current clock rate.

Note that ratchet accent levels decrease as the count increases (Rc4 = softest). This reflects how fast ratchets are naturally lighter; the density itself creates the intensity rather than the per-hit accent.

---

## Chaos

Chaos blends the profile's transition weights toward a flat, uniform distribution:

- **0%** — pure profile weights; the chain follows its tendency strongly
- **50%** — character present but movement more varied
- **100%** — all weights equal; any hit type equally likely from any state

CV 1 adds to the encoder-set baseline. Patching an LFO or envelope here creates evolving rhythmic density over time.

---

## Density (CV 2)

Density shifts the REST weight independently of the other weights:

- **No voltage / 0V** — neutral; profile determines the rest-to-hit ratio naturally
- **Positive voltage** — rests become less likely; hit states become more likely; pattern fills in
- **Higher positive voltage** — approaches continuous hitting

Use Density to dynamically control how busy the pattern is. For example, patch a slowly rising envelope into CV 2 during a build-up, or route a sustain pedal gate through an attenuator to manually control density in real time.

---

## The Four Profiles (Transition Matrices)

Each profile is an 8×8 weight table. The **row** is the current hit type (where you are now); the **column** is a possible next hit type (where you might go). Higher numbers mean more likely. At Chaos=0 the weights are followed closely; at Chaos=100 they are ignored entirely.

### S — Steady (Rock/Pop)

Gravitates strongly toward plain hits and accented hits from almost every state. Rests are brief — the chain quickly rebounds into playing. Flams appear as ornaments, mostly from hit and acc_hit states. Ratchets are rare fills following dense passages, with rests very likely in the step after a ratchet.

Produces a reliable, driving groove that stays active without becoming unpredictably complex. Best for kick/snare applications where consistent rhythmic drive is needed.

### T — Syncopated (Funk/Latin)

Rests are structurally meaningful — the chain uses them deliberately rather than rushing back to play. Flams are very common from almost every state, making them a regular feature rather than an ornament. Ratchet-2 is a common syncopation device; longer ratchets still lead back to rests. Higher overall energy than Steady, but with built-in space.

The syncopation emerges naturally from the combination of deliberate rests and frequent flams. This profile works well where rhythmic interest matters more than raw density.

### J — Jazz/Free

Favours complexity: accented flams (AcF) are the signature gesture and appear frequently across the matrix. Ratchets escalate — Ratchet-3 leads to Ratchet-4 which tends to self-reinforce before finally collapsing to rest. Extended rests are followed by bursts of dense activity. The chain can build into cascading complexity and then collapse.

Most unpredictable of the four profiles. Best for free improvisation contexts, or when you want the drummer to make dramatic decisions autonomously.

### P — Sparse

Very heavy REST self-loops — the chain spends most of its time in silence. When it does play, it favours plain hits or accented hits and returns quickly to rest. Ratchets and flams are essentially absent from all rows. Single hits only, with long gaps between them.

Use this for minimal percussion — a clap or accent that arrives rarely and unpredictably, or a hi-hat that drops out for bars at a time. Combine with Density CV to dynamically pull it out of sparse mode when needed.

---

## Seed & Reset

The seed system gives you a deterministic loop anchor. Two values are stored: the **start state** (which hit type to return to) and an **RNG seed** (the seed for the random number generator). Because resetting the RNG seed replays the same random number sequence, a short-press reset reproduces the *exact same sequence of hit types* every time.

The seed is never set to REST — long press and encoder re-roll always land on an active hit state to ensure the pattern starts playing immediately.

### Short press on Digital 2 — Replay loop
Resets the RNG to the stored seed and returns to the start state. The parameter row briefly inverts (~500ms) to confirm. The chain will now play the identical sequence it played after the last seed was set.

### Long press on Digital 2 (~3 seconds) — New seed
Generates a new start state (never REST) and a new RNG seed from the current time. From this moment on, short press replays this new loop.

### Encoder on Seed cursor — Re-roll
Rotating the encoder immediately rolls a new seed. Stay in edit mode to keep rolling — you do not need to click out. The dice icon shifts up one pixel briefly to confirm each roll.

### Aux on Seed cursor — Re-roll
Same as rotating the encoder on Seed: generates a new seed and jumps to it.

**Performance workflow:** let the chain develop its character → short press Digital 2 to lock in that pattern as a repeatable loop → use long press or encoder re-roll to move to a new pattern at the next section.

---

## Display

```
                  [Seed]    ← cursor label, right-justified, Edit mode only
[S]    [42%]   [dice]      ← profile / chaos% / seed icon, y=15
──────────────────────      ← separator
[scrolling bar graph]       ← 8 columns, oldest left → newest right
──────────────────────      ← baseline (screen bottom)
```

The bar graph shows the last 8 hit states:

- **REST:** single dot at the baseline
- **HIT / ACC_HIT:** solid bar whose height represents accent level (taller = louder); ACC_HIT bar is wider
- **FLAM / ACC_FLAM:** bar subdivided into 2 horizontal bands with a 1px gap — the lower band represents the grace note, upper band the main hit
- **RATCHET_2:** 2 horizontal bands
- **RATCHET_3:** 3 horizontal bands
- **RATCHET_4:** 4 horizontal bands

Bar height indicates accent level — a tall bar is a loud, accented hit; a short bar is a soft hit. Multi-band bars (flams, ratchets) divide the total bar height among the sub-triggers.

---

## Tips

- **Out B into a VCA:** Patch Accent CV into the CV input of a VCA or dynamics processor. Each hit type gets its natural dynamic level automatically, without needing a velocity-to-CV converter.
- **Out B into Out A's envelope amount:** Send the trigger (Out A) to an envelope and the accent (Out B) to the envelope's amount input. Loud hits get a longer, harder envelope; soft hits a shorter, quieter one.
- **Density for build-ups:** Patch a slowly rising CV into CV 2 across a four or eight-bar phrase. The pattern fills in gradually, creating a natural build without a fixed arrangement.
- **P profile + Density:** The Sparse profile mostly rests; increasing Density CV opens it up into a real groove. This gives you a wide range of rhythmic density from a single preset.
- **Pair with MarkoV:** Run both from the same clock. MarkovPerc drives a percussion voice; MarkoV drives pitch. The profile labels (S/T/J) are intentionally parallel — matching the mood on both creates coherent ensemble textures.
- **J profile for solos:** The Jazz profile's ratchet escalation creates natural moments of peak complexity. Use the encoder re-roll on Seed to find a new loop when the pattern has run its course.

---

## Credits

MarkovPerc by uglifruit.
