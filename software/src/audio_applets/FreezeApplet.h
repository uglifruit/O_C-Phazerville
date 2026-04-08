#pragma once

// FreezeApplet — Real-Time Spectral Freezer
//
// HemisphereAudioApplet wrapper around AudioSpectralFreeze.
// Audio graph:
//
//   input ──► freeze_effect ──► mixer ch0 (wet) ──► output
//   input ─────────────────────► mixer ch1 (dry) ──►
//
// Parameters (accessible via the Hemisphere modulation matrix):
//   Mix   0–100 %   Dry/wet blend (equal-power crossfade)
//   Smear 0–100 %   Phase randomisation: 0 = static/metallic, 100 = smooth drone
//   Frz   gate/latch Spectral freeze toggle
//
// MIT License — (c) 2026 Andy Jenkinson (uglifruit) with ClaudeCode

#include "../Audio/AudioSpectralFreeze.h"
#include "../Audio/AudioMixer.h"
#include "../Audio/AudioPassthrough.h"
#include "dsputils.h"

template <AudioChannels Channels>
class FreezeApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "Freeze"; }

    // -------------------------------------------------------------------------
    // Start() — wire the audio graph and apply initial parameter values.
    // PatchCable() creates AudioConnection objects managed by the base class.
    // -------------------------------------------------------------------------
    void Start() override {
        for (int ch = 0; ch < Channels; ++ch) {
            // Input → freeze DSP engine
            PatchCable(input, ch, freeze[ch], 0);
            // Freeze output → mixer wet channel
            PatchCable(freeze[ch], 0, mixer[ch], 0);
            // Input (dry) → mixer dry channel
            PatchCable(input, ch, mixer[ch], 1);
            // Mixer → output
            PatchCable(mixer[ch], 0, output, ch);
        }
        applyMix();
    }

    void Unload() override {
        AllowRestart();
    }

    // -------------------------------------------------------------------------
    // Controller() — runs in the audio ISR (~16 kHz).
    // Reads CV, updates AudioSpectralFreeze parameters and mixer gains.
    // -------------------------------------------------------------------------
    void Controller() override {
        // Mix: base knob 0–100 plus CV offset, clamped to [0, 1]
        const float m = constrain(mix * 0.01f + mix_cv.InF(), 0.0f, 1.0f);

        // Smear: same structure
        const float s = constrain(smear * 0.01f + smear_cv.InF(), 0.0f, 1.0f);

        // Freeze: latched by parameter OR gated by CV (threshold ~5 V equivalent)
        const bool f = frozen || (freeze_cv.In() > 3000);

        for (int ch = 0; ch < Channels; ++ch) {
            freeze[ch].smear  = s;
            freeze[ch].frozen = f;
        }

        // Equal-power crossfade between dry and wet
        float dry_gain, wet_gain;
        EqualPowerFade(dry_gain, wet_gain, m);
        for (int ch = 0; ch < Channels; ++ch) {
            mixer[ch].gain(0, wet_gain);
            mixer[ch].gain(1, dry_gain);
        }
    }

    // -------------------------------------------------------------------------
    // View() — renders the three parameters with their CV slots.
    //          The Frz row label inverts when frozen (matches Mist convention).
    // -------------------------------------------------------------------------
    void View() override {
        // Row 1: Mix
        gfxPrint(1, 15, "Mix:");
        gfxStartCursor(MIX_X, 15);
        graphics.printf("%3d%%", mix);
        gfxEndCursor(cursor == MIX);

        gfxStartCursor();
        gfxPrint(mix_cv);
        gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());

        // Row 2: Smear
        gfxPrint(1, 25, "Smr:");
        gfxStartCursor(MIX_X, 25);
        graphics.printf("%3d%%", smear);
        gfxEndCursor(cursor == SMEAR);

        gfxStartCursor();
        gfxPrint(smear_cv);
        gfxEndCursor(cursor == SMEAR_CV, false, smear_cv.InputName());

        // Row 3: Freeze toggle — label inverts when active
        gfxStartCursor(1, 35);
        gfxPrint("Frz:");
        gfxEndCursor(false);  // not a cursor target itself
        if (frozen) gfxInvert(1, 35, 4 * 6, 8);

        gfxStartCursor(MIX_X, 35);
        gfxPrint(frozen ? " ON" : "OFF");
        gfxEndCursor(cursor == FREEZE);

        gfxStartCursor();
        gfxPrint(freeze_cv);
        gfxEndCursor(cursor == FREEZE_CV, false, freeze_cv.InputName());

        gfxDisplayInputMapEditor();
    }

    // -------------------------------------------------------------------------
    // Button / encoder
    // -------------------------------------------------------------------------
    void OnButtonPress() override {
        if (CheckEditInputMapPress(
                cursor,
                IndexedInput(MIX_CV,    mix_cv),
                IndexedInput(SMEAR_CV,  smear_cv),
                IndexedInput(FREEZE_CV, freeze_cv)
            )) return;
        CursorToggle();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            MoveCursor(cursor, direction, NUM_PARAMS - 1);
            return;
        }
        if (EditSelectedInputMap(direction)) return;
        switch (cursor) {
            case MIX:
                mix = constrain(mix + direction, 0, 100);
                applyMix();
                break;
            case MIX_CV:
                mix_cv.ChangeSource(direction);
                break;
            case SMEAR:
                smear = constrain(smear + direction, 0, 100);
                break;
            case SMEAR_CV:
                smear_cv.ChangeSource(direction);
                break;
            case FREEZE:
                frozen = !frozen;
                break;
            case FREEZE_CV:
                freeze_cv.ChangeSource(direction);
                break;
        }
    }

    // -------------------------------------------------------------------------
    // Preset serialisation — pack 3 parameters + 3 CVInputMaps into 2 × uint64_t
    // -------------------------------------------------------------------------
    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        // data[0]: mix (7 bit), smear (7 bit), frozen (1 bit) = 15 bits
        data[0] = PackPackables(pack<7>(mix), pack<7>(smear), frozen);
        // data[1]: three CVInputMap structs (each 16 bits = 48 bits total)
        data[1] = PackPackables(mix_cv, smear_cv, freeze_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], mix, smear, frozen);
        UnpackPackables(data[1], mix_cv, smear_cv, freeze_cv);
        mix   = constrain((int)mix,   0, 100);
        smear = constrain((int)smear, 0, 100);
        applyMix();
    }

    // -------------------------------------------------------------------------
    // AudioStream plumbing
    // -------------------------------------------------------------------------
    AudioStream* InputStream()  override { return &input;  }
    AudioStream* OutputStream() override { return &output; }

protected:
    void SetHelp() override {}

private:
    // Cursor positions (enum-style constants)
    static constexpr int MIX       = 0;
    static constexpr int MIX_CV    = 1;
    static constexpr int SMEAR     = 2;
    static constexpr int SMEAR_CV  = 3;
    static constexpr int FREEZE    = 4;
    static constexpr int FREEZE_CV = 5;
    static constexpr int NUM_PARAMS = 6;

    // X position for value columns (right-aligned to match other applets)
    static constexpr int MIX_X = 63 - 5 * 6;

    // --- Parameters ---
    int8_t  mix   = 50;     // 0–100 %
    int8_t  smear = 0;      // 0–100 %
    bool    frozen = false;
    int8_t  cursor = 0;

    CVInputMap mix_cv;
    CVInputMap smear_cv;
    CVInputMap freeze_cv;

    // --- Audio objects ---
    AudioPassthrough<Channels>      input;
    std::array<AudioSpectralFreeze, Channels> freeze;
    std::array<AudioMixer<2>,       Channels> mixer;
    AudioPassthrough<Channels>      output;

    // Apply stored mix value directly to mixer gains (used from Start() and
    // OnDataReceive() where Controller() hasn't run yet)
    void applyMix() {
        float dry, wet;
        EqualPowerFade(dry, wet, mix * 0.01f);
        for (int ch = 0; ch < Channels; ++ch) {
            mixer[ch].gain(0, wet);
            mixer[ch].gain(1, dry);
        }
    }
};
