#pragma once

#include "../Audio/AudioEffectClouds.h"

extern "C" uint8_t external_psram_size;

// MistierApplet — Clouds-inspired live granular audio processor.
//
// Records audio into a 1-second PSRAM circular buffer and plays it back as a
// cloud of overlapping grains. Differences from MistApplet:
//   • Texture — continuous window morph: rect → triangle → Hann
//   • Density — centred at 0 (silence). CCW → regular periodic. CW → stochastic.
//   • Feedback/reverb — via Blend multi-mode (encoder cycles WD/FB/RV on BLEND_MODE cursor)
//   • No fixed grain shapes — shape driven by Texture
//
// I/O:
//   Input → [AudioEffectClouds] → wet ──────────────────────────────┐
//   wet   → [AudioEffectReverbSchroeder] → reverb_out ──────────────┤
//   Input →                              → dry ─────────────────────┤ AudioMixer<3> → Output
//
// Blend multi-mode (encoder on BLEND_MODE cursor, or AuxButton latches freeze):
//   WD  — Blend controls wet/dry ratio (equal-power crossfade)
//   FB  — Blend controls grain feedback amount
//   RV  — Blend controls reverb send
//
// Freeze:
//   • AuxButton: latches/unlatches manual freeze (performance use, no cable needed)
//   • Frz input: assign a hardware gate via input map editor (cursor on Frz row → press button)
//   Both OR together. Frz row label inverts while latched.
//
// Parameters:
//   Page 1: Pos, Den, Sz, Spr
//   Page 2: Pitch, Blend+Mode, Tex, Mix, Frz
//
template <AudioChannels Channels>
class MistierApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "Mistier"; }

    void Start() override {
        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].Start(this, ch, input_stream, output_stream);
        }
    }

    void Unload() override {
        for (auto& ch : channels) ch.Stop(this);
        AllowRestart();
    }

    void Controller() override {
        // ── CV-modulated effective parameters ──────────────────────────────────
        float eff_pos     = constrain(0.01f * pos     + pos_cv.InF(),              0.0f, 1.0f);
        // density UI 0–100: map to −20..+20 Hz (50=silence, >50=stochastic, <50=periodic)
        float eff_density = constrain(0.4f * (density - 50) + density_cv.InF() * 20.0f, -20.0f, 20.0f);
        float eff_size    = constrain(0.01f * size    + size_cv.InF() * 0.49f,     0.01f, 0.5f);
        float eff_texture = constrain(0.01f * texture + texture_cv.InF(),           0.0f, 1.0f);
        float eff_spray   = constrain(0.01f * spray   + spray_cv.InF(),             0.0f, 1.0f);

        // Pitch: semitones + V/Oct CV.
        float eff_semis = (float)pitch + (float)pitch_cv.In() / 128.0f;
        float eff_pitch = SemitonesToRatio(eff_semis);

        // Pitch spread: quadratic curve for fine control at low values.
        float eff_psprd_raw   = constrain(0.01f * psprd + psprd_cv.InF(), 0.0f, 1.0f);
        float eff_psprd_semis = 12.0f * eff_psprd_raw * eff_psprd_raw;

        // Blend param 0..1 from UI + CV.
        float eff_blend = constrain(0.01f * blend + blend_cv.InF(), 0.0f, 1.0f);

        // Freeze: hardware gate OR manual latch.
        bool frozen = freeze_input.Gate() || manual_freeze_;

        // Wet/dry/reverb gains depend on blend mode.
        float dry_gain = 1.0f, wet_gain = 1.0f, reverb_gain = 0.0f;
        float eff_feedback = 0.0f;
        float eff_mix = constrain(0.01f * mix + mix_cv.InF(), 0.0f, 1.0f);

        switch (blend_mode_) {
            case BLEND_WD:
                // Blend controls wet/dry balance; mix scales overall output.
                EqualPowerFade(dry_gain, wet_gain, eff_blend);
                dry_gain *= eff_mix;
                wet_gain *= eff_mix;
                break;
            case BLEND_FB:
                // Blend drives feedback; mix controls wet/dry as normal.
                eff_feedback = eff_blend;
                EqualPowerFade(dry_gain, wet_gain, eff_mix);
                break;
            case BLEND_RV:
                // Blend drives reverb send; mix scales wet output level.
                reverb_gain = eff_blend * eff_mix;
                wet_gain    = eff_mix;
                dry_gain    = 0.0f;
                break;
        }

        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].grain_stream.setPosition(eff_pos);
            channels[ch].grain_stream.setDensity(eff_density);
            channels[ch].grain_stream.setSize(eff_size);
            channels[ch].grain_stream.setSpray(eff_spray);
            channels[ch].grain_stream.setPitch(eff_pitch);
            channels[ch].grain_stream.setPitchSpread(eff_psprd_semis);
            channels[ch].grain_stream.setTexture(eff_texture);
            channels[ch].grain_stream.setFeedback(eff_feedback);
            channels[ch].grain_stream.setFreeze(frozen);
            channels[ch].mixer.gain(MistierChannel::DRY_CH,  dry_gain);
            channels[ch].mixer.gain(MistierChannel::WET_CH,  wet_gain);
            channels[ch].mixer.gain(MistierChannel::VERB_CH, reverb_gain);
        }
    }

    void View() override {
        if (!channels[0].grain_stream.IsReady()) {
            gfxPrint(1, 15, "No PSRAM");
            return;
        }

        // ── Grain activity bar (y=7) ──────────────────────────────────────────
        uint8_t active = channels[0].grain_stream.ActiveGrainCount();
        for (uint8_t i = 0; i < AudioEffectClouds::MAX_GRAINS; i++) {
            if (i < active) gfxPixel(1 + i, 7);
        }

        // ── Two pages ────────────────────────────────────────────────────────
        const bool pg2 = (cursor >= PITCH);

        if (!pg2) {
            // ── Page 1: Pos, Den, Sz, Spr (y=15/25/35/45) ──────────────────
            gfxPrint(1, 15, "Pos:");
            gfxStartCursor(); graphics.printf("%3d%%", pos); gfxEndCursor(cursor == POS);
            gfxStartCursor(); gfxPrint(pos_cv); gfxEndCursor(cursor == POS_CV, false, pos_cv.InputName());

            gfxPrint(1, 25, "Den:");
            gfxStartCursor();
            int8_t d_hz = (int8_t)(0.4f * (density - 50));
            if (d_hz >= 0) graphics.printf("+%2d", d_hz);
            else           graphics.printf("%3d", d_hz);
            gfxEndCursor(cursor == DENSITY);
            gfxStartCursor(); gfxPrint(density_cv); gfxEndCursor(cursor == DENSITY_CV, false, density_cv.InputName());

            gfxPrint(1, 35, "Sz:");
            gfxStartCursor(); graphics.printf("%3dms", size * 10); gfxEndCursor(cursor == SIZE);
            gfxStartCursor(); gfxPrint(size_cv); gfxEndCursor(cursor == SIZE_CV, false, size_cv.InputName());

            gfxPrint(1, 45, "Spr:");
            gfxStartCursor(); graphics.printf("%3d%%", spray); gfxEndCursor(cursor == SPRAY);
            gfxStartCursor(); gfxPrint(spray_cv); gfxEndCursor(cursor == SPRAY_CV, false, spray_cv.InputName());
        } else {
            // ── Page 2: Pitch, Blend+Mode, Tex, Mix, Frz (y=15/25/35/45/55) ─

            // Pitch
            gfxPrint(1, 15, "Pt:");
            gfxStartCursor();
            if (pitch >= 0) graphics.printf("+%2d", pitch);
            else            graphics.printf("%3d", pitch);
            gfxEndCursor(cursor == PITCH);
            gfxStartCursor(); gfxPrint(pitch_cv); gfxEndCursor(cursor == PITCH_CV, false, pitch_cv.InputName());

            // Blend: mode label and value share one row. Two separate cursors.
            // BLEND cursor underlines the value; BLEND_MODE cursor underlines the label.
            static const char* BLEND_LABELS[] = { "WD", "FB", "RV" };
            gfxStartCursor(1, 25); gfxPrint(BLEND_LABELS[blend_mode_]); gfxPrint(":"); gfxEndCursor(cursor == BLEND_MODE);
            gfxStartCursor(); graphics.printf("%3d%%", blend); gfxEndCursor(cursor == BLEND);
            gfxStartCursor(); gfxPrint(blend_cv); gfxEndCursor(cursor == BLEND_CV, false, blend_cv.InputName());

            // Texture
            gfxPrint(1, 35, "Tex:");
            gfxStartCursor(); graphics.printf("%3d%%", texture); gfxEndCursor(cursor == TEXTURE);
            gfxStartCursor(); gfxPrint(texture_cv); gfxEndCursor(cursor == TEXTURE_CV, false, texture_cv.InputName());

            // Mix
            gfxPrint(1, 45, "Mix:");
            gfxStartCursor(); graphics.printf("%3d%%", mix); gfxEndCursor(cursor == MIX);
            gfxStartCursor(); gfxPrint(mix_cv); gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());

            // Freeze — label inverts while manual latch is active (print first, then invert)
            gfxPrint(1, 55, "Frz:");
            if (manual_freeze_) gfxInvert(1, 55, 20, 8);
            gfxStartCursor(); gfxPrint(freeze_input); gfxEndCursor(cursor == FREEZE, true, freeze_input.InputName());
        }

        // Page indicator
        gfxPrint(58, 56, pg2 ? "<" : ">");

        gfxDisplayInputMapEditor();
    }

    // AuxButton: latch/unlatch manual freeze (live performance, no cable needed).
    void AuxButton() override {
        manual_freeze_ ^= 1;
        CancelEdit();
    }

    void OnButtonPress() override {
        if (CheckEditInputMapPress(
                cursor,
                IndexedInput(POS_CV,      pos_cv),
                IndexedInput(DENSITY_CV,  density_cv),
                IndexedInput(SIZE_CV,     size_cv),
                IndexedInput(SPRAY_CV,    spray_cv),
                IndexedInput(PITCH_CV,    pitch_cv),
                IndexedInput(BLEND_CV,    blend_cv),
                IndexedInput(TEXTURE_CV,  texture_cv),
                IndexedInput(MIX_CV,      mix_cv),
                IndexedInput(FREEZE,      freeze_input)
            ))
            return;
        CursorToggle();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            MoveCursor(cursor, direction, CURSOR_LENGTH - 1);
            return;
        }
        if (EditSelectedInputMap(direction)) return;

        switch (cursor) {
            case POS:        pos      = constrain(pos      + direction,   0, 100); break;
            case POS_CV:     pos_cv.ChangeSource(direction);                        break;
            case DENSITY:    density  = constrain(density  + direction,   0, 100); break;
            case DENSITY_CV: density_cv.ChangeSource(direction);                   break;
            case SIZE:       size     = constrain(size     + direction,   1,  50); break;
            case SIZE_CV:    size_cv.ChangeSource(direction);                       break;
            case SPRAY:      spray    = constrain(spray    + direction,   0, 100); break;
            case SPRAY_CV:   spray_cv.ChangeSource(direction);                     break;
            case PITCH:      pitch    = constrain(pitch    + direction, -12,  12); break;
            case PITCH_CV:   pitch_cv.ChangeSource(direction);                     break;
            case BLEND:      blend    = constrain(blend    + direction,   0, 100); break;
            case BLEND_CV:   blend_cv.ChangeSource(direction);                     break;
            case BLEND_MODE:
                // Cycle WD→FB→RV→WD in either direction.
                blend_mode_ = (BlendMode)((blend_mode_ + 3 + direction) % 3);
                break;
            case TEXTURE:    texture  = constrain(texture  + direction,   0, 100); break;
            case TEXTURE_CV: texture_cv.ChangeSource(direction);                   break;
            case MIX:        mix      = constrain(mix      + direction,   0, 100); break;
            case MIX_CV:     mix_cv.ChangeSource(direction);                       break;
            case FREEZE:     freeze_input.ChangeSource(direction);                 break;
            default: break;
        }
    }

#define MISTIER_PARAMS  pos, density, size, texture, pitch, psprd, blend, mix
    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(MISTIER_PARAMS);
        data[1] = PackPackables(pos_cv, density_cv, size_cv, spray_cv);
        data[2] = PackPackables(pitch_cv, blend_cv, texture_cv, mix_cv);
        data[3] = PackPackables(freeze_input, (uint8_t)blend_mode_, spray);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], MISTIER_PARAMS);
        UnpackPackables(data[1], pos_cv, density_cv, size_cv, spray_cv);
        UnpackPackables(data[2], pitch_cv, blend_cv, texture_cv, mix_cv);
        uint8_t bm = 0;
        UnpackPackables(data[3], freeze_input, bm, spray);
        blend_mode_ = (BlendMode)constrain(bm, 0, 2);
    }
#undef MISTIER_PARAMS

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output_stream; }

protected:
    void SetHelp() override {}

private:
    enum BlendMode : uint8_t {
        BLEND_WD = 0,  // blend = wet/dry balance
        BLEND_FB = 1,  // blend = feedback amount
        BLEND_RV = 2,  // blend = reverb send
    };

    enum Cursor : int8_t {
        // Page 1
        POS = 0, POS_CV,
        DENSITY, DENSITY_CV,
        SIZE, SIZE_CV,
        SPRAY, SPRAY_CV,
        // Page 2
        PITCH, PITCH_CV,
        BLEND, BLEND_CV,
        BLEND_MODE,           // encoder cycles WD→FB→RV; no CV slot
        TEXTURE, TEXTURE_CV,
        MIX, MIX_CV,
        FREEZE,               // DigitalInputMap; button opens input map editor
        CURSOR_LENGTH,
    };

    int8_t cursor = POS;

    // Parameters
    int8_t  pos      = 50;  // 0–100%
    CVInputMap pos_cv;
    int8_t  density  = 75;  // 0–100 (50=silence; 0.4*(val-50) Hz, >50=stochastic)
    CVInputMap density_cv;
    int8_t  size     = 15;  // 1–50 (×10ms = 10–500ms)
    CVInputMap size_cv;
    int8_t  spray    = 20;  // 0–100% position scatter
    CVInputMap spray_cv;
    int8_t  pitch    = 0;   // −12 to +12 semitones
    CVInputMap pitch_cv;
    int8_t  psprd    = 0;   // 0–100% pitch spread (hidden, persists)
    CVInputMap psprd_cv;
    int8_t  blend    = 80;  // 0–100% (meaning depends on blend_mode_)
    CVInputMap blend_cv;
    int8_t  texture  = 50;  // 0–100% (0=rect, 50=tri, 100=Hann)
    CVInputMap texture_cv;
    int8_t  mix      = 80;  // 0–100% output level
    CVInputMap mix_cv;
    DigitalInputMap freeze_input;

    BlendMode blend_mode_    = BLEND_WD;
    bool      manual_freeze_ = false;  // latched by AuxButton

    // Per-channel DSP struct.
    struct MistierChannel {
        static const uint8_t DRY_CH  = 0;
        static const uint8_t WET_CH  = 1;
        static const uint8_t VERB_CH = 2;

        AudioEffectClouds           grain_stream;
        AudioEffectReverbSchroeder* reverb = nullptr;
        AudioMixer<3>               mixer;

        MistierChannel()
            : grain_stream(
                external_psram_size
                    ? AudioEffectClouds::CLOUDS_BUFFER_SAMPLES
                    : AudioEffectClouds::CLOUDS_BUFFER_SAMPLES / 2)
        {}

        void Start(HemisphereAudioApplet* owner, int ch,
                   AudioStream& input, AudioStream& output) {
            grain_stream.Acquire();
            reverb = owner->GetBungverb();

            owner->PatchCable(input,        ch, grain_stream, 0);
            owner->PatchCable(input,        ch, mixer,        DRY_CH);
            owner->PatchCable(grain_stream,  0, mixer,        WET_CH);
            if (reverb) {
                owner->PatchCable(grain_stream, 0, *reverb,  0);
                owner->PatchCable(*reverb,       0, mixer,   VERB_CH);
                reverb->setDecayTime(2.5f);
                reverb->setDamping(0.5f);
            }
            owner->PatchCable(mixer, 0, output, ch);
        }

        void Stop(HemisphereAudioApplet* owner) {
            grain_stream.Release();
            if (reverb) {
                owner->ReleaseBungverb(reverb);
                reverb = nullptr;
            }
        }
    } channels[Channels];

    AudioPassthrough<Channels> input_stream;
    AudioPassthrough<Channels> output_stream;
};
