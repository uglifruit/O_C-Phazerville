#pragma once

#include "../Audio/AudioEffectMist.h"

extern "C" uint8_t external_psram_size;

// MistApplet — live granular audio processor.
//
// Continuously records audio into a 1-second PSRAM circular buffer and generates
// overlapping grains (up to 16 per channel). Each grain is a windowed slice of
// the buffer played back at a configurable pitch. Wet/dry mix blends the granular
// cloud with the live (always-recording) input.
//
// I/O:
//   Input → [AudioEffectMist] → wet  ─┐
//   Input →                    → dry    ─┴→ AudioMixer<2> → Output
//
// Parameters:
//   POS     — playback position in buffer (0=live, 100=oldest), CV-able
//   DENSITY — grain spawn rate 1–50 Hz, CV-able (bipolar)
//   SIZE    — grain duration 10–500 ms, CV-able (bipolar)
//   SPRAY   — position scatter 0–100%, CV-able (bipolar)
//   PITCH   — playback speed ±12 semitones, CV-able (bipolar)
//   PSPRD   — per-grain pitch spread 0–100% (0=none, 100=±1oct), CV-able (bipolar)
//   FREEZE  — gate input stops write pointer (AuxButton = manual latch)
//   MIX     — wet/dry balance 0–100%, CV-able (bipolar)
//
template <AudioChannels Channels>
class MistApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "Mist"; }

    void Start() override {
        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].Start(this, ch, input_stream, output_stream);
        }
    }

    void Unload() override {
        for (auto& ch : channels) ch.Stop();
        AllowRestart();
    }

    void Controller() override {
        // CV-modulated effective parameter values.
        float eff_pos     = constrain(0.01f * pos     + pos_cv.InF(),             0.0f, 1.0f);
        float eff_density = constrain((float)density  + density_cv.InF() * 49.0f, 1.0f, 50.0f);
        float eff_size    = constrain(0.01f * size     + size_cv.InF()  * 0.49f,   0.01f, 0.5f);
        float eff_spray   = constrain(0.01f * spray    + spray_cv.InF(),           0.0f, 1.0f);

        // Pitch: semitones ±12 → playback ratio.
        float eff_semis   = constrain((float)pitch     + pitch_cv.InF()  * 12.0f, -12.0f, 12.0f);
        float eff_pitch   = SemitonesToRatio(eff_semis);

        // Pitch spread: 0–100% → 0–12 semitones via quadratic curve.
        // Squaring gives log-like feel: small values cover fine spreads (<0.5st),
        // large values reach up to ±1 octave.
        float eff_psprd_raw   = constrain(0.01f * psprd + psprd_cv.InF(), 0.0f, 1.0f);
        float eff_psprd_semis = 12.0f * eff_psprd_raw * eff_psprd_raw;

        // Freeze: hardware gate OR latched manual freeze.
        bool frozen = freeze_input.Gate() || manual_freeze_;

        // Wet/dry gains.
        float eff_mix = constrain(0.01f * mix + mix_cv.InF(), 0.0f, 1.0f);
        float dry_gain, wet_gain;
        EqualPowerFade(dry_gain, wet_gain, eff_mix);

        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].grain_stream.setPosition(eff_pos);
            channels[ch].grain_stream.setDensity(eff_density);
            channels[ch].grain_stream.setSize(eff_size);
            channels[ch].grain_stream.setSpray(eff_spray);
            channels[ch].grain_stream.setPitch(eff_pitch);
            channels[ch].grain_stream.setPitchSpread(eff_psprd_semis);
            channels[ch].grain_stream.setFreeze(frozen);
            channels[ch].wet_dry_mixer.gain(MistChannel::DRY_CH, dry_gain);
            channels[ch].wet_dry_mixer.gain(MistChannel::WET_CH, wet_gain);
        }
    }

    void View() override {
        if (!channels[0].grain_stream.IsReady()) {
            gfxPrint(1, 15, "No PSRAM");
            return;
        }

        // ── Grain activity bar (y=7) ──────────────────────────────────────
        uint8_t active = channels[0].grain_stream.ActiveGrainCount();
        for (uint8_t i = 0; i < 16; i++) {
            if (i < active) gfxPixel(1 + i, 7);
        }

        // ── Scrolling parameter rows ──────────────────────────────────────
        // 8 param rows, 6 visible at a time (y=15..55, 8px per row).
        static const int FIRST_Y = 15;
        static const int ROW_H   = 8;
        static const int VISIBLE = 6;

        // Keep the cursor's row inside the scroll window.
        int cur_row = CursorToRow(cursor);
        if (cur_row < scroll_top_)             scroll_top_ = cur_row;
        if (cur_row >= scroll_top_ + VISIBLE)  scroll_top_ = cur_row - VISIBLE + 1;

        auto y_of = [&](int row) { return FIRST_Y + (row - scroll_top_) * ROW_H; };
        auto vis  = [&](int row) { return row >= scroll_top_ && row < scroll_top_ + VISIBLE; };

        // Row 0: Position
        if (vis(0)) {
            gfxPrint(1, y_of(0), "Pos:");
            gfxStartCursor();
            graphics.printf("%3d%%", pos);
            gfxEndCursor(cursor == POS);
            gfxStartCursor();
            gfxPrint(pos_cv);
            gfxEndCursor(cursor == POS_CV, false, pos_cv.InputName());
        }

        // Row 1: Density
        if (vis(1)) {
            gfxPrint(1, y_of(1), "Den:");
            gfxStartCursor();
            graphics.printf("%2d", density);
            gfxEndCursor(cursor == DENSITY);
            gfxStartCursor();
            gfxPrint(density_cv);
            gfxEndCursor(cursor == DENSITY_CV, false, density_cv.InputName());
        }

        // Row 2: Size
        if (vis(2)) {
            gfxPrint(1, y_of(2), "Sz:");
            gfxStartCursor();
            graphics.printf("%3dms", size * 10);
            gfxEndCursor(cursor == SIZE);
            gfxStartCursor();
            gfxPrint(size_cv);
            gfxEndCursor(cursor == SIZE_CV, false, size_cv.InputName());
        }

        // Row 3: Spray
        if (vis(3)) {
            gfxPrint(1, y_of(3), "Spr:");
            gfxStartCursor();
            graphics.printf("%3d%%", spray);
            gfxEndCursor(cursor == SPRAY);
            gfxStartCursor();
            gfxPrint(spray_cv);
            gfxEndCursor(cursor == SPRAY_CV, false, spray_cv.InputName());
        }

        // Row 4: Pitch
        if (vis(4)) {
            gfxPrint(1, y_of(4), "Pt:");
            gfxStartCursor();
            if (pitch >= 0) graphics.printf("+%2d", pitch);
            else            graphics.printf("%3d", pitch);
            gfxEndCursor(cursor == PITCH);
            gfxStartCursor();
            gfxPrint(pitch_cv);
            gfxEndCursor(cursor == PITCH_CV, false, pitch_cv.InputName());
        }

        // Row 5: Pitch Spread
        if (vis(5)) {
            gfxPrint(1, y_of(5), "PSp:");
            gfxStartCursor();
            graphics.printf("%3d%%", psprd);
            gfxEndCursor(cursor == PSPRD);
            gfxStartCursor();
            gfxPrint(psprd_cv);
            gfxEndCursor(cursor == PSPRD_CV, false, psprd_cv.InputName());
        }

        // Row 6: Freeze
        if (vis(6)) {
            int y = y_of(6);
            gfxPrint(1, y, "Frz:");
            if (manual_freeze_) gfxInvert(1, y, 24, 8);
            gfxStartCursor();
            gfxPrint(freeze_input);
            gfxEndCursor(cursor == FREEZE, true, freeze_input.InputName());
        }

        // Row 7: Mix
        if (vis(7)) {
            gfxPrint(1, y_of(7), "Mix:");
            gfxStartCursor();
            graphics.printf("%3d%%", mix);
            gfxEndCursor(cursor == MIX);
            gfxStartCursor();
            gfxPrint(mix_cv);
            gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());
        }

        gfxDisplayInputMapEditor();
    }

    // AuxButton latches manual freeze for performance use without a patch cable.
    void AuxButton() override {
        manual_freeze_ ^= 1;
        CancelEdit();
    }

    void OnButtonPress() override {
        if (CheckEditInputMapPress(
                cursor,
                IndexedInput(POS_CV,     pos_cv),
                IndexedInput(DENSITY_CV, density_cv),
                IndexedInput(SIZE_CV,    size_cv),
                IndexedInput(SPRAY_CV,   spray_cv),
                IndexedInput(PITCH_CV,   pitch_cv),
                IndexedInput(PSPRD_CV,   psprd_cv),
                IndexedInput(FREEZE,     freeze_input),
                IndexedInput(MIX_CV,     mix_cv)
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
            case POS:        pos     = constrain(pos     + direction, 0, 100);  break;
            case POS_CV:     pos_cv.ChangeSource(direction);                     break;
            case DENSITY:    density = constrain(density + direction, 1, 50);   break;
            case DENSITY_CV: density_cv.ChangeSource(direction);                 break;
            case SIZE:       size    = constrain(size    + direction, 1, 50);   break;
            case SIZE_CV:    size_cv.ChangeSource(direction);                    break;
            case SPRAY:      spray   = constrain(spray   + direction, 0, 100);  break;
            case SPRAY_CV:   spray_cv.ChangeSource(direction);                   break;
            case PITCH:      pitch   = constrain(pitch   + direction, -12, 12); break;
            case PITCH_CV:   pitch_cv.ChangeSource(direction);                   break;
            case PSPRD:      psprd   = constrain(psprd   + direction, 0, 100);  break;
            case PSPRD_CV:   psprd_cv.ChangeSource(direction);                   break;
            case FREEZE:     freeze_input.ChangeSource(direction);               break;
            case MIX:        mix     = constrain(mix     + direction, 0, 100);  break;
            case MIX_CV:     mix_cv.ChangeSource(direction);                     break;
            default: break;
        }
    }

#define MIST_PARAMS  pos, density, size, spray, pitch, psprd, mix
    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(MIST_PARAMS);
        data[1] = PackPackables(pos_cv, density_cv, size_cv);
        data[2] = PackPackables(spray_cv, pitch_cv, psprd_cv, mix_cv);
        data[3] = PackPackables(freeze_input);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], MIST_PARAMS);
        UnpackPackables(data[1], pos_cv, density_cv, size_cv);
        UnpackPackables(data[2], spray_cv, pitch_cv, psprd_cv, mix_cv);
        UnpackPackables(data[3], freeze_input);
    }
#undef MIST_PARAMS

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output_stream; }

protected:
    void SetHelp() override {}

private:
    enum Cursor : int8_t {
        POS = 0,
        POS_CV,
        DENSITY,
        DENSITY_CV,
        SIZE,
        SIZE_CV,
        SPRAY,
        SPRAY_CV,
        PITCH,
        PITCH_CV,
        PSPRD,
        PSPRD_CV,
        FREEZE,
        MIX,
        MIX_CV,
        CURSOR_LENGTH,
    };

    int CursorToRow(int c) const {
        switch (c) {
            case POS:     case POS_CV:     return 0;
            case DENSITY: case DENSITY_CV: return 1;
            case SIZE:    case SIZE_CV:    return 2;
            case SPRAY:   case SPRAY_CV:   return 3;
            case PITCH:   case PITCH_CV:   return 4;
            case PSPRD:   case PSPRD_CV:   return 5;
            case FREEZE:                   return 6;
            case MIX:     case MIX_CV:     return 7;
            default:                       return 0;
        }
    }

    int8_t cursor     = POS;
    int8_t scroll_top_ = 0;

    // Parameters
    int8_t  pos     = 50;  // 0–100% (buffer depth)
    CVInputMap pos_cv;
    int8_t  density = 8;   // 1–50 Hz
    CVInputMap density_cv;
    int8_t  size    = 10;  // 1–50 (×10ms = 10–500ms)
    CVInputMap size_cv;
    int8_t  spray   = 20;  // 0–100% scatter
    CVInputMap spray_cv;
    int8_t  pitch   = 0;   // −12 to +12 semitones
    CVInputMap pitch_cv;
    int8_t  psprd   = 0;   // 0–100% pitch spread (0=none, 100=±1oct)
    CVInputMap psprd_cv;
    DigitalInputMap freeze_input;
    int8_t  mix     = 80;  // 0–100% wet
    CVInputMap mix_cv;

    bool manual_freeze_ = false;

    // Per-channel DSP struct (mirrors GlitchChannel pattern).
    struct MistChannel {
        static const uint8_t DRY_CH = 0;
        static const uint8_t WET_CH = 1;

        AudioEffectMist grain_stream;
        AudioMixer<2>       wet_dry_mixer;

        MistChannel()
            : grain_stream(
                external_psram_size
                    ? AudioEffectMist::MIST_BUFFER_SAMPLES
                    : AudioEffectMist::MIST_BUFFER_SAMPLES / 2)
        {}

        void Start(HemisphereAudioApplet* owner, int ch,
                   AudioStream& input, AudioStream& output) {
            grain_stream.Acquire();
            owner->PatchCable(input,        ch,  grain_stream,   0);
            owner->PatchCable(input,        ch,  wet_dry_mixer,  DRY_CH);
            owner->PatchCable(grain_stream, 0,   wet_dry_mixer,  WET_CH);
            owner->PatchCable(wet_dry_mixer, 0,  output,         ch);
        }

        void Stop() { grain_stream.Release(); }
    } channels[Channels];

    AudioPassthrough<Channels> input_stream;
    AudioPassthrough<Channels> output_stream;
};
