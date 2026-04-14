# UGLIALL Branch

Integration branch combining all custom audio and CV applets into a single flashable firmware for Teensy 4.1.

## Download

Latest build: https://github.com/uglifruit/O_C-Phazerville/releases/latest

Built automatically on every push by GitHub Actions. Grab the `.hex` from the release assets and flash with Teensy Loader.

---

## What's included

### Audio applets

| Applet | Type | Notes |
|---|---|---|
| FMDrumApplet | Input + Processor | FM + noise drum synth |
| GlitchApplet | Processor | Buffer glitch / stutter effect |
| MistApplet | Processor | Granular processor |
| AdvKrpsStrngApplet | Input + Processor | Custom Karplus-Strong physical model string |
| WavRecorderApplet | Processor | SD card WAV recorder — 1 MB PSRAM ring buffer, R001–R999 filenames |
| ReverbApplet | Processor | Freeverb reverb |
| VcaApplet | Processor | |
| LadderApplet | Processor | 4-pole Moog-style ladder filter |
| FilterFolderApplet | Processor | Wave folder + state variable filter |
| DelayApplet | Processor | Multi-tap delay |
| PhazerApplet | Processor | Phaser |
| DynamicsApplet | Processor | Gate / compressor / limiter |
| OscApplet | Input + Processor | FM oscillator |
| HandSawApplet | Input + Processor | 12-voice polysynth |
| PassthruApplet | Input + Processor | Bypass |
| InputApplet | Input + Processor | Hardware input with gain |
| UpsampledApplet | Input + Processor | AC-coupled input upsampler |

**Removed from phazerville base:** WavPlayerApplet (RAM1 pressure), BungverbApplet

### CV applets

| Applet | ID | Category | Notes |
|---|---|---|---|
| MarkoV | 93 | Sequencer | Melodic first-order Markov chain, 10 states, scale-degree quantizer |
| MarkovPerc | 94 | Other | Rhythmic Markov state machine with density and accent |

---

## RAM1 status

As of 2026-04-14 the build passes with ~8KB headroom (was 8704 bytes over before WavPlayerApplet was removed).

If RAM1 overflows again, remove in this order:
1. HandSawApplet (~2.5–3KB) — 12-oscillator polysynth, replaced by FMDrum/AdvKrpsStrng
2. OscApplet (~2KB) — FM oscillator, same reason
3. FilterFolderApplet (~1.5KB) — niche wavefolder

Do not remove FMDrum, Glitch, Mist, or AdvKrpsStrng.

**FLASHMEM note:** FLASHMEM annotations inside `.h` files are silently ignored by LTO in **all** environments (T41, T41_audio, etc.) — both use `-DTEENSY_OPT_SMALLEST_CODE_LTO`. Confirmed empirically: annotating every cold-path method in WAVRecorderApplet produced zero change in RAM1/code size. The only way to get methods into Flash is to define them in a `.cpp` file. Only `synth_advanced_karplus.cpp` does this correctly — don't rely on FLASHMEM in headers.

**WavRecorderApplet is excluded from T41_audio** via `#ifndef USB_AUDIO` guards in `hemisphere_audio_config.h`. The USB audio driver costs ~33 KB RAM1, leaving insufficient headroom for the applet's ~17 KB ITCM code.

---

## Adding new audio DSP files

Any file under `software/src/Audio/` that includes `<Audio.h>` must be wrapped:

```cpp
// In the .h:
#pragma once
#ifdef ARDUINO_TEENSY41
// ... your code ...
#endif // ARDUINO_TEENSY41

// In the .cpp:
#include "your_file.h"
#ifdef ARDUINO_TEENSY41
// ... your code ...
#endif // ARDUINO_TEENSY41
```

Without this, T40/T32/stock CI targets fail to compile.

---

## Merge history

| Source branch | What it added |
|---|---|
| FMDrumGlitch | Base — FMDrumApplet + GlitchApplet |
| GranularDSP | MistApplet |
| feature/advanced-karplus-dsp | AdvKrpsStrngApplet + AudioSynthAdvancedKarplus DSP engine |
| MarkoV | MarkoV CV applet |
| MarkovPerc | MarkovPerc CV applet |

Conflicts on merge are always in `hemisphere_audio_config.h` (audio pools) or `hemisphere_config.h` (CV registry). Resolution pattern: keep UGLIALL's pool contents and add the new applet from the incoming branch.

---

## CI

Workflow: `.github/workflows/firmware.yml`
- Triggers on any push touching `software/**`
- Builds all PlatformIO environments (`pio run`)
- T41 and T41_audio are the target environments — others may fail due to audio-only code (expected)
- Publishes a GitHub Release tagged `UGLIALL-latest` with `.hex` files attached on every push to this branch
