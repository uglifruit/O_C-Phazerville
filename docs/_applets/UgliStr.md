---
layout: default
---
# UgliStr

UgliStr is a stereo Karplus-Strong string synthesizer. It generates two slightly detuned string voices (left and right) through a resonant ladder filter and a VCA envelope, with a dry/wet mix so it can process incoming audio or run as a standalone voice.

Pluck the string with any assignable trigger source. The pitch tracks V/Oct from any CV input. Body and Detune each have their own CV inputs for live timbral modulation.

### I/O

|        | Assignable | Assignable |
| ------ | :--------: | :--------: |
| TRIG   | Pluck trigger | — |
| CV INs | V/Oct pitch | Assignable (Decay / Brightness / Body / Detune / Mix) |
| OUTs   | Left string | Right string |

All CV input assignments are configurable via the InputMap editor (button press on any CV cursor).

### UI Parameters
* **Pitch** — base pitch in V/Oct (displays as note + Hz); CV source assignable
* **Trigger** — pluck trigger source (digital inputs or CV)
* **Decay** — envelope decay time (0–100 → ~30ms to 5s); CV-able
* **Brightness** — ladder filter cutoff (0–100 → 200Hz to 20kHz, log); CV-able
* **Body** — filter resonance (0–100 → 0.0 to 1.7); CV-able
* **Detune** — L/R spread in cents (0–50); CV-able
* **Mix** — dry/wet blend (0 = full dry passthrough, 100 = string only); CV-able

### Credits
Authored by uglifruit (Andy Jenkinson).
