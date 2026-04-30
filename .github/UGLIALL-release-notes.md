# UGLIALL Latest Build

Custom Phazerville firmware by Andy Jenkinson (uglifruit), branched from [djphazer/O_C-Phazerville](https://github.com/djphazer/O_C-Phazerville).

---

## Custom Applets

### CV / Hemisphere Applets

| Applet | Description |
|--------|-------------|
| **TruthCat4** | Catalan Boolean Gate — truth-table rhythm generator using all 14 parse trees of A NOR/NAND B NOR/NAND C NOR/NAND D NOR/NAND E. 16 operator patterns × 14 bracketings = 224 distinct rhythms. |
| **MarkoV** | Markov chain melodic generator — 5 weighted tendency profiles (Stability, Tension, Jazz, Glacial, Drone), CV chaos control, seed/reset system, V/Oct transpose. |
| **MarkovPerc** | Markov chain rhythm generator — 4 hit types (rest, hit, flam, ratchet), 4 style profiles (Steady, Syncopated, Jazz, Sparse), accent CV output, density CV input. |

### Audio Applets (Teensy 4.1 only)

| Applet | Description |
|--------|-------------|
| **FMDrum** | 4-mode FM + noise percussion synthesiser. Kick, snare, hat, and tom modes with per-preset parameter storage. |
| **Glitch** | Live-input stutter/glitch processor. Captures audio into a buffer and plays it back with variable slice offset, bit crush, sample rate reduction, and reverse/freeze modes. |
| **AdvKrpsStrng** | Advanced Karplus-Strong string synthesiser with custom DSP engine. V/Oct pitch, decay, tone, and mix controls. |
| **Misty** | Clouds-inspired live granular processor. Position, size, pitch, density, and texture controls; feedback and stereo spread. |
| **ModalResonator** | Physical modelling modal resonator filter. Gate-triggered resonance with pitch CV, brightness, structure, and damping parameters. |
| **WavRecorder** | Transparent SD card WAV recorder. Uses 1 MB PSRAM ring buffer (T41 with PSRAM) or 64 KB internal RAM fallback. Records 48 kHz stereo to SD. |
| **GritApplet** | 4-mode distortion/lo-fi processor: Clip, Fold, Crush, Decimate. |

---

## Build Variants

Three `.hex` files are produced per build:

| File | Description |
|------|-------------|
| `T41.hex` | Standard Teensy 4.1 build — all CV applets, all audio applets |
| `T41_audio.hex` | Teensy 4.1 with dedicated audio board |
| `T41_MTP.hex` | Teensy 4.1 with MTP (USB file transfer for SD card access) |

---

## Notes

- All applets are on the `UGLIALL-on-uglimods` branch.
- Audio applets require Teensy 4.1. They are excluded from T32/T40 builds via `#ifndef ARDUINO_TEENSY41` guards.
- WavRecorder is excluded from the `T41_audio` build (audio board occupies the same SD pins).
