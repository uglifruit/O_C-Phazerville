#pragma once

#include "../Audio/AudioEffectGlitch.h"

extern "C" uint8_t external_psram_size;

// GlitchApplet — live-input stutter/glitch effect.
//
// Continuously records audio into a 1-second circular buffer. When HOLD is
// gated, the read pointer freezes on a clock-sized slice and loops it in
// Forward, Reverse, Ping-Pong, or Ratchet (MOD) mode. Wet/dry mix blends
// the frozen slice with the live input.
//
// I/O (within Quadrants audio chain):
//   Input  → glitch DSP → wet channel  ┐
//   Input  → dry channel               ┴→ mixer → Output
//
// Parameters:
//   CLOCK_SRC  — clock source for beat tracking (DigitalInputMap)
//   DIV        — slice length as clock division (1/2 … 1/64)
//   HOLD_SRC   — gate input that activates stutter (DigitalInputMap)
//   MODE       — FWD / REV / PNG / MOD (ping-pong / ratchet)
//   MODE_CV    — CV modulates effective mode index
//   RATCHET    — ratchet subdivisions 1–6 (visible only in MOD mode)
//   RATCHET_CV — CV modulates ratchet count
//   MIX        — wet/dry balance 0–100%, CV-modulatable
//
// AuxButton latches manual hold without a patched gate.
template <AudioChannels Channels>
class GlitchApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "Glitch"; }

    void Start() override {
        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].Start(this, ch, input_stream, output_stream);
        }
        clock_source.source = -2; // default to CLK1
    }

    void Unload() override {
        for (auto& ch : channels) ch.Stop();
        AllowRestart();
    }

    void Controller() override {
        // Track beat period from selected clock source (same pattern as DelayApplet).
        clock_count++;
        if (clock_source.Clock()) {
            clock_base_secs = clock_count / 16666.0f;
            clock_count = 0;
        }

        // Slice length in samples from BPM × division ratio.
        size_t slice_samples = static_cast<size_t>(
            clock_base_secs * DIV_BEATS[div] * AUDIO_SAMPLE_RATE);

        // Gate state: hardware gate OR latched manual hold.
        bool held = hold_input.Gate() || manual_hold_;

        // CV-modulated mode and ratchet count.
        int eff_mode = constrain(
            (int)mode + (int)roundf(mode_cv.InF() * (NUM_MODES - 1)),
            0, NUM_MODES - 1);
        uint8_t eff_ratchet = (uint8_t)constrain(
            (int)ratchet + (int)roundf(ratchet_cv.InF() * 5.0f),
            1, 6);

        // Equal-power wet/dry gains (computed once, set on all channels).
        float dry_gain, wet_gain;
        EqualPowerFade(dry_gain, wet_gain,
            constrain(0.01f * mix + mix_cv.InF(), 0.0f, 1.0f));

        for (int ch = 0; ch < Channels; ch++) {
            channels[ch].glitch_stream.setHold(held);
            channels[ch].glitch_stream.setSliceSamples(slice_samples);
            channels[ch].glitch_stream.setMode(eff_mode);
            channels[ch].glitch_stream.setRatchet(eff_ratchet);
            channels[ch].wet_dry_mixer.gain(GlitchChannel::DRY_CH, dry_gain);
            channels[ch].wet_dry_mixer.gain(GlitchChannel::WET_CH, wet_gain);
        }
    }

    void View() override {
        if (!channels[0].glitch_stream.IsReady()) {
            gfxPrint(1, 15, "No PSRAM");
            return;
        }

        // ── Line 1 (y=15): Clock source + Division ──────────────────────
        gfxPos(1, 15);
        gfxStartCursor();
        gfxPrint(clock_source);
        gfxEndCursor(cursor == CLOCK_SRC, false, clock_source.InputName());

        gfxStartCursor();
        gfxPrint(DIV_NAMES[div]);
        gfxEndCursor(cursor == DIV);

        // ── Line 2 (y=25): Hold source ───────────────────────────────────
        gfxPrint(1, 25, "Hld:");
        if (manual_hold_) gfxInvert(1, 25, 24, 8); // indicate latched hold
        gfxStartCursor();
        gfxPrint(hold_input);
        gfxEndCursor(cursor == HOLD_SRC, false, hold_input.InputName());

        // ── Line 3 (y=35): Playback mode + CV ────────────────────────────
        gfxPrint(1, 35, "Mod:");
        gfxStartCursor();
        gfxPrint(MODE_NAMES[mode]);
        gfxEndCursor(cursor == MODE);

        gfxStartCursor();
        gfxPrint(mode_cv);
        gfxEndCursor(cursor == MODE_CV, false, mode_cv.InputName());

        if (mode == MODE_RATCHET) {
            // ── Line 4 (y=45): Ratchet count + CV (MOD mode only) ────────
            gfxPrint(1, 45, "Rch:");
            gfxStartCursor();
            gfxPrint(ratchet);
            gfxEndCursor(cursor == RATCHET);

            gfxStartCursor();
            gfxPrint(ratchet_cv);
            gfxEndCursor(cursor == RATCHET_CV, false, ratchet_cv.InputName());

            // ── Line 5 (y=55): Mix + CV ───────────────────────────────────
            gfxPrint(1, 55, "Mix:");
            gfxStartCursor();
            graphics.printf("%3d%%", mix);
            gfxEndCursor(cursor == MIX);

            gfxStartCursor();
            gfxPrint(mix_cv);
            gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());
        } else {
            // ── Line 4 (y=45): Mix + CV ───────────────────────────────────
            gfxPrint(1, 45, "Mix:");
            gfxStartCursor();
            graphics.printf("%3d%%", mix);
            gfxEndCursor(cursor == MIX);

            gfxStartCursor();
            gfxPrint(mix_cv);
            gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());
        }

        gfxDisplayInputMapEditor();
    }

    // AuxButton latches/unlatches manual hold for performance without a patch.
    void AuxButton() override {
        manual_hold_ ^= 1;
        CancelEdit();
    }

    void OnButtonPress() override {
        if (CheckEditInputMapPress(
                cursor,
                IndexedInput(CLOCK_SRC,  clock_source),
                IndexedInput(MODE_CV,    mode_cv),
                IndexedInput(RATCHET_CV, ratchet_cv),
                IndexedInput(MIX_CV,     mix_cv)
            ))
            return;
        CursorToggle();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            int next = cursor + direction;
            // Skip RATCHET/RATCHET_CV positions when not in MOD mode.
            if (mode != MODE_RATCHET) {
                if (next == RATCHET || next == RATCHET_CV) next += direction;
            }
            cursor = (Cursor)constrain(next, 0, CURSOR_LENGTH - 1);
            return;
        }
        if (EditSelectedInputMap(direction)) return;

        switch (cursor) {
            case CLOCK_SRC:  clock_source.ChangeSource(direction); break;
            case DIV:        div = constrain(div + direction, 0, NUM_DIVS - 1); break;
            case HOLD_SRC:   hold_input.ChangeSource(direction); break;
            case MODE:
                mode = constrain(mode + direction, 0, NUM_MODES - 1);
                // If leaving MOD mode while cursor is on a ratchet row, reset it.
                if (mode != MODE_RATCHET && (cursor == RATCHET || cursor == RATCHET_CV))
                    cursor = MODE;
                break;
            case MODE_CV:    mode_cv.ChangeSource(direction); break;
            case RATCHET:    ratchet = constrain(ratchet + direction, 1, 6); break;
            case RATCHET_CV: ratchet_cv.ChangeSource(direction); break;
            case MIX:        mix = constrain(mix + direction, 0, 100); break;
            case MIX_CV:     mix_cv.ChangeSource(direction); break;
            default: break;
        }
    }

#define GLITCH_PARAMS  pack<3>(div), pack<2>(mode), pack<3>(ratchet), mix
    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(GLITCH_PARAMS);
        data[1] = PackPackables(clock_source, hold_input, mix_cv);
        data[2] = PackPackables(mode_cv, ratchet_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], GLITCH_PARAMS);
        UnpackPackables(data[1], clock_source, hold_input, mix_cv);
        UnpackPackables(data[2], mode_cv, ratchet_cv);
    }
#undef GLITCH_PARAMS

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output_stream; }

protected:
    void SetHelp() override {}

private:
    static const uint8_t NUM_DIVS  = 8;
    static const uint8_t NUM_MODES = 4;
    static const uint8_t MODE_RATCHET = AudioEffectGlitch::MODE_RATCHET;

    static constexpr const char* DIV_NAMES[] = {
        "1/2", "1/3", "1/4", "1/6", "1/8", "1/16", "1/32", "1/64"
    };
    static constexpr float DIV_BEATS[] = {
        2.0f, 4.0f/3.0f, 1.0f, 2.0f/3.0f, 0.5f, 0.25f, 0.125f, 0.0625f
    };
    static constexpr const char* MODE_NAMES[] = { "FWD", "REV", "PNG", "RAT" };

    enum Cursor : int8_t {
        CLOCK_SRC = 0,
        DIV,
        HOLD_SRC,
        MODE,
        MODE_CV,
        RATCHET,
        RATCHET_CV,
        MIX,
        MIX_CV,
        CURSOR_LENGTH,
    };

    Cursor cursor = DIV;

    // Parameters
    DigitalInputMap clock_source;
    uint8_t  div     = 5;   // default 1/16
    DigitalInputMap hold_input;
    uint8_t  mode    = 0;   // 0=FWD, 1=REV, 2=PING, 3=MOD
    CVInputMap mode_cv;
    uint8_t  ratchet = 2;   // 1–6 subdivisions, used in MOD mode
    CVInputMap ratchet_cv;
    int8_t   mix     = 100; // 0–100% wet
    CVInputMap mix_cv;

    bool manual_hold_ = false;

    // Clock period tracking (same approach as DelayApplet)
    uint32_t clock_count     = 0;
    float    clock_base_secs = 0.5f; // 120 BPM default until first clock tick

    // Per-channel DSP struct (mirrors DelayChannel pattern).
    struct GlitchChannel {
        static const uint8_t DRY_CH = 0;
        static const uint8_t WET_CH = 1;

        AudioEffectGlitch glitch_stream;
        AudioMixer<2>     wet_dry_mixer;

        GlitchChannel()
            : glitch_stream(
                external_psram_size
                    ? AudioEffectGlitch::GLITCH_BUFFER_SAMPLES
                    : AudioEffectGlitch::GLITCH_BUFFER_SAMPLES / 16)
        {}

        void Start(HemisphereAudioApplet* owner, int ch,
                   AudioStream& input, AudioStream& output) {
            glitch_stream.Acquire();
            owner->PatchCable(input,        ch,     glitch_stream,   0);
            owner->PatchCable(input,        ch,     wet_dry_mixer,   DRY_CH);
            owner->PatchCable(glitch_stream, 0,     wet_dry_mixer,   WET_CH);
            owner->PatchCable(wet_dry_mixer, 0,     output,          ch);
        }

        void Stop() { glitch_stream.Release(); }
    } channels[Channels];

    AudioPassthrough<Channels> input_stream;
    AudioPassthrough<Channels> output_stream;
};

// Out-of-line definitions for static constexpr members (C++14 ODR requirement).
template <AudioChannels Channels>
constexpr const char* GlitchApplet<Channels>::DIV_NAMES[];
template <AudioChannels Channels>
constexpr float GlitchApplet<Channels>::DIV_BEATS[];
template <AudioChannels Channels>
constexpr const char* GlitchApplet<Channels>::MODE_NAMES[];
