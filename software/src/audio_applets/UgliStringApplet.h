// Copyright (c) 2026, Andy Jenkinson (uglifruit)
// MIT License
//
// UgliStringApplet — Karplus-Strong stereo resonant string synthesizer.
// Two independently-pitched voices (L/R detuned apart) run through a
// Moog-style ladder filter with a software VCA envelope.
//
// CV1: V/Oct pitch   CV2 / Trig: assignable trigger source   Decay: encoder only

#include "synth_karplusstrong.h"

class UgliStringApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "UgliStr"; }

    void Start() override {
        env_stream.Acquire();
        env_stream.Method(INTERPOLATION_LINEAR);

        // synthL/R → filterL/R → vcaL/R → stereo output
        PatchCable(synthL, 0, filterL, 0);
        PatchCable(synthR, 0, filterR, 0);
        PatchCable(filterL, 0, vcaL, 0);
        PatchCable(filterR, 0, vcaR, 0);
        PatchCable(env_stream, 0, vcaL, 1);  // same envelope to both channels
        PatchCable(env_stream, 0, vcaR, 1);
        PatchCable(vcaL, 0, output, 0);
        PatchCable(vcaR, 0, output, 1);

        // VCA: CV port 1 controls gain; bias=0 so silence until envelope fires
        vcaL.level(1.0f);  vcaL.bias(0.0f);  vcaL.rectify(true);
        vcaR.level(1.0f);  vcaR.bias(0.0f);  vcaR.rectify(true);

        trig_cv.Reset();
        UpdateFilter();
        AllowRestart();
    }

    void Unload() override {
        env_stream.Release();
        AllowRestart();
    }

    void Controller() override {
        // Envelope: exponential decay shaped by Decay param
        float d = decay * 0.01f;
        float ticks = 500.0f + d * 79500.0f;  // ~30ms (0) to ~5s (100)
        env_level *= powf(0.001f, 1.0f / ticks);
        if (env_level < 0.0001f) env_level = 0.0f;
        env_stream.Push(float_to_q15(env_level));

        // Pluck on rising edge of trigger source (CVInputMap — assign to Dig1,
        // clock, gate, etc. via the Trig cursor)
        if (trig_cv.Clock()) Pluck();
    }

    void View() override {
        // Row 1: pitch (tuning indicator + Hz) and pitch CV source
        gfxStartCursor(1, 15);
        gfxPrintTuningIndicator(pitch);
        gfxEndCursor(cursor == PITCH);
        gfxStartCursor(11, 15);
        gfxPrintPitchHz(pitch);
        gfxEndCursor(cursor == PITCH);
        gfxStartCursor();
        gfxPrint(pitch_cv);
        gfxEndCursor(cursor == PITCH_CV, false, pitch_cv.InputName());

        // Row 2: trigger source
        gfxPrint(1, 25, "Trg:");
        gfxStartCursor(25, 25);
        gfxPrint(trig_cv);
        gfxEndCursor(cursor == TRIG_CV, false, trig_cv.InputName());

        // Row 3: decay
        gfxPrint(1, 35, "Dec:");
        gfxStartCursor(25, 35);
        graphics.printf("%3d%%", decay);
        gfxEndCursor(cursor == DECAY);

        // Row 4: filter brightness
        gfxPrint(1, 45, "Brt:");
        gfxStartCursor(25, 45);
        PrintBrightnessHz();
        gfxEndCursor(cursor == BRIGHTNESS);

        // Row 5: body and detune packed together
        gfxPrint(1, 55, "B:");
        gfxStartCursor(13, 55);
        graphics.printf("%3d", body);
        gfxEndCursor(cursor == BODY);
        gfxPrint(" D:");
        gfxStartCursor();
        graphics.printf("%2d", detune);
        gfxEndCursor(cursor == DETUNE);

        gfxDisplayInputMapEditor();
    }

    void OnButtonPress() override {
        if (CheckEditInputMapPress(cursor,
              IndexedInput(PITCH_CV, pitch_cv),
              IndexedInput(TRIG_CV,  trig_cv)))
            return;
        CursorToggle();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            MoveCursor(cursor, direction, DETUNE);
            return;
        }
        if (EditSelectedInputMap(direction)) return;

        constexpr int MAX_PITCH =  5 * 12 * 128;  // C8
        constexpr int MIN_PITCH = -2 * 12 * 128;  // C1
        switch (cursor) {
            case PITCH:
                pitch = constrain(pitch + direction * 128, MIN_PITCH, MAX_PITCH);
                break;
            case PITCH_CV:
                pitch_cv.ChangeSource(direction);
                break;
            case TRIG_CV:
                trig_cv.ChangeSource(direction);
                break;
            case DECAY:
                decay = constrain(decay + direction, 0, 100);
                break;
            case BRIGHTNESS:
                brightness = constrain(brightness + direction, 0, 100);
                UpdateFilter();
                break;
            case BODY:
                body = constrain(body + direction, 0, 100);
                UpdateFilter();
                break;
            case DETUNE:
                detune = constrain(detune + direction, 0, 50);
                break;
        }
    }

    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(pitch, decay, brightness, body, detune);
        data[1] = PackPackables(pitch_cv, trig_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], pitch, decay, brightness, body, detune);
        UnpackPackables(data[1], pitch_cv, trig_cv);
        pitch      = constrain(pitch,      -2*12*128, 5*12*128);
        decay      = constrain(decay,      0, 100);
        brightness = constrain(brightness, 0, 100);
        body       = constrain(body,       0, 100);
        detune     = constrain(detune,     0, 50);
        UpdateFilter();
    }

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output; }

protected:
    void SetHelp() override {}

private:
    enum Cursor : int8_t {
        PITCH, PITCH_CV, TRIG_CV, DECAY, BRIGHTNESS, BODY, DETUNE
    };

    int8_t  cursor     = PITCH;
    int16_t pitch      = 0;    // semitones * 128, C3 default
    int8_t  decay      = 50;   // 0–100: short (~30ms) to long (~5s) decay
    int8_t  brightness = 70;   // 0–100: logarithmic 200Hz–20kHz filter cutoff
    int8_t  body       = 0;    // 0–100: ladder resonance (0.0–1.7)
    int8_t  detune     = 10;   // 0–50 cents: L/R pitch spread

    CVInputMap pitch_cv;
    DigitalInputMap trig_cv;

    float env_level = 0.0f;

    AudioPassthrough<STEREO>     input_stream;  // unused; source applet
    AudioSynthKarplusStrong      synthL, synthR;
    AudioFilterLadder            filterL, filterR;
    AudioVCA                     vcaL, vcaR;
    InterpolatingStream<>        env_stream;
    AudioPassthrough<STEREO>     output;

    void Pluck() {
        float base  = PitchToRatio(pitch + pitch_cv.In()) * C3;
        float ratio = powf(2.0f, detune / 1200.0f);  // cents → frequency ratio
        synthL.noteOn(base / ratio, 1.0f);
        synthR.noteOn(base * ratio, 1.0f);
        env_level = 1.0f;
    }

    void UpdateFilter() {
        float hz  = 200.0f * powf(100.0f, brightness * 0.01f);  // 200Hz–20kHz log
        float res = body * 0.017f;  // 0–100 → 0.0–1.7 (below self-oscillation)
        filterL.frequency(hz);  filterL.resonance(res);
        filterR.frequency(hz);  filterR.resonance(res);
    }

    void PrintBrightnessHz() {
        int hz = (int)(200.0f * powf(100.0f, brightness * 0.01f));
        if (hz < 1000)
            graphics.printf("%dHz", hz);
        else if (hz < 10000)
            graphics.printf("%d.%dkHz", hz / 1000, (hz % 1000) / 100);
        else
            graphics.printf("%dkHz", hz / 1000);
    }
};
