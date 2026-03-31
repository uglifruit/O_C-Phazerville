// Copyright (c) 2026, Andy Jenkinson (uglifruit)
// MIT License
//
// UgliStringApplet — Karplus-Strong stereo resonant string synthesizer.
// Two independently-pitched voices (L/R detuned) through a ladder filter
// with a software VCA envelope and dry/wet mix.
//
// CV1: V/Oct pitch   Trig: assignable trigger   Dec/Brt/Mix: CV-able
// Body + Detune: encoder + CV via button-press InputMap editor

#include "synth_karplusstrong.h"

class UgliStringApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "UgliStr"; }

    void Start() override {
        env_stream.Acquire();
        env_stream.Method(INTERPOLATION_LINEAR);

        // synthL/R → filterL/R → vcaL/R → mixerL/R[1]
        // input_stream[L/R]             → mixerL/R[0]
        // mixerL/R                      → output
        PatchCable(synthL, 0, filterL, 0);
        PatchCable(synthR, 0, filterR, 0);
        PatchCable(filterL, 0, vcaL, 0);
        PatchCable(filterR, 0, vcaR, 0);
        PatchCable(env_stream, 0, vcaL, 1);
        PatchCable(env_stream, 0, vcaR, 1);
        PatchCable(input_stream, 0, mixerL, 0);   // dry
        PatchCable(input_stream, 1, mixerR, 0);
        PatchCable(vcaL, 0, mixerL, 1);           // wet
        PatchCable(vcaR, 0, mixerR, 1);
        PatchCable(mixerL, 0, output, 0);
        PatchCable(mixerR, 0, output, 1);

        vcaL.level(1.0f);  vcaL.bias(0.0f);  vcaL.rectify(true);
        vcaR.level(1.0f);  vcaR.bias(0.0f);  vcaR.rectify(true);

        trig_cv.Reset();
        UpdateFilter();
        UpdateMix();
        AllowRestart();
    }

    void Unload() override {
        env_stream.Release();
        AllowRestart();
    }

    void Controller() override {
        // Envelope decay — base param + CV (unipolar, adds to decay floor)
        float eff_decay = constrain(decay * 0.01f + decay_cv.InF(), 0.0f, 1.0f);
        float ticks = 500.0f + eff_decay * 79500.0f;
        env_level *= powf(0.001f, 1.0f / ticks);
        if (env_level < 0.0001f) env_level = 0.0f;
        env_stream.Push(float_to_q15(env_level));

        // Filter — update every tick so CV modulation tracks in real time
        float eff_brightness = constrain(brightness * 0.01f + brightness_cv.InF(), 0.0f, 1.0f);
        float hz = 200.0f * powf(100.0f, eff_brightness);
        float eff_body = constrain(body * 0.01f + body_cv.InF(), 0.0f, 1.0f);
        float res = eff_body * 1.7f;
        filterL.frequency(hz);  filterL.resonance(res);
        filterR.frequency(hz);  filterR.resonance(res);

        // Mix — dry/wet blend (0=full dry, 100=full wet)
        float eff_mix = constrain(mix * 0.01f + mix_cv.InF(), 0.0f, 1.0f);
        mixerL.gain(0, 1.0f - eff_mix);  mixerL.gain(1, eff_mix);
        mixerR.gain(0, 1.0f - eff_mix);  mixerR.gain(1, eff_mix);

        // Pluck on trigger rising edge
        if (trig_cv.Clock()) Pluck();
    }

    void View() override {
        // Row 1: pitch + V/Oct CV source
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

        // Row 3: decay + CV
        gfxPrint(1, 35, "Dec:");
        gfxStartCursor(25, 35);
        graphics.printf("%3d%%", decay);
        gfxEndCursor(cursor == DECAY);
        gfxStartCursor();
        gfxPrint(decay_cv);
        gfxEndCursor(cursor == DECAY_CV, false, decay_cv.InputName());

        // Row 4: brightness + CV
        gfxPrint(1, 45, "Brt:");
        gfxStartCursor(25, 45);
        PrintBrightnessHz();
        gfxEndCursor(cursor == BRIGHTNESS);
        gfxStartCursor();
        gfxPrint(brightness_cv);
        gfxEndCursor(cursor == BRIGHTNESS_CV, false, brightness_cv.InputName());

        // Row 5: body (spicy — button press opens CV editor), detune (spicy),
        //        mix value + CV
        gfxPrint(1, 55, "B:");
        gfxStartCursor(13, 55);
        graphics.printf("%3d", body);
        gfxEndCursor(cursor == BODY, true);  // spicy: button opens body_cv editor

        gfxPrint(" D:");
        gfxStartCursor();
        graphics.printf("%2d", detune);
        gfxEndCursor(cursor == DETUNE, true);  // spicy: button opens detune_cv editor

        gfxPrint(" M:");
        gfxStartCursor();
        graphics.printf("%3d", mix);
        gfxEndCursor(cursor == MIX);
        gfxStartCursor();
        gfxPrint(mix_cv);
        gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());

        gfxDisplayInputMapEditor();
    }

    void OnButtonPress() override {
        // Body and Detune have hidden CVs — button press on those cursors opens
        // the InputMap editor rather than toggling EditMode.
        if (CheckEditInputMapPress(cursor,
              IndexedInput(PITCH_CV,     pitch_cv),
              IndexedInput(TRIG_CV,      trig_cv),
              IndexedInput(DECAY_CV,     decay_cv),
              IndexedInput(BRIGHTNESS_CV, brightness_cv),
              IndexedInput(BODY,         body_cv),    // spicy: button on BODY
              IndexedInput(DETUNE,       detune_cv),  // spicy: button on DETUNE
              IndexedInput(MIX_CV,       mix_cv)))
            return;
        CursorToggle();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            MoveCursor(cursor, direction, MIX_CV);
            return;
        }
        if (EditSelectedInputMap(direction)) return;

        constexpr int MAX_PITCH =  5 * 12 * 128;
        constexpr int MIN_PITCH = -2 * 12 * 128;
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
            case DECAY_CV:
                decay_cv.ChangeSource(direction);
                break;
            case BRIGHTNESS:
                brightness = constrain(brightness + direction, 0, 100);
                UpdateFilter();
                break;
            case BRIGHTNESS_CV:
                brightness_cv.ChangeSource(direction);
                break;
            case BODY:
                body = constrain(body + direction, 0, 100);
                UpdateFilter();
                break;
            case DETUNE:
                detune = constrain(detune + direction, 0, 50);
                break;
            case MIX:
                mix = constrain(mix + direction, 0, 100);
                UpdateMix();
                break;
            case MIX_CV:
                mix_cv.ChangeSource(direction);
                break;
        }
    }

    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(pitch, decay, brightness, body, detune, mix);
        data[1] = PackPackables(pitch_cv, trig_cv, decay_cv);
        data[2] = PackPackables(brightness_cv, body_cv, detune_cv, mix_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], pitch, decay, brightness, body, detune, mix);
        UnpackPackables(data[1], pitch_cv, trig_cv, decay_cv);
        UnpackPackables(data[2], brightness_cv, body_cv, detune_cv, mix_cv);
        pitch      = constrain(pitch,      -2*12*128, 5*12*128);
        decay      = constrain(decay,      0, 100);
        brightness = constrain(brightness, 0, 100);
        body       = constrain(body,       0, 100);
        detune     = constrain(detune,     0, 50);
        mix        = constrain(mix,        0, 100);
        UpdateFilter();
        UpdateMix();
    }

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output; }

protected:
    void SetHelp() override {}

private:
    enum Cursor : int8_t {
        PITCH, PITCH_CV, TRIG_CV,
        DECAY, DECAY_CV,
        BRIGHTNESS, BRIGHTNESS_CV,
        BODY,       // spicy — button opens body_cv InputMap editor
        DETUNE,     // spicy — button opens detune_cv InputMap editor
        MIX, MIX_CV
    };

    int8_t  cursor     = PITCH;
    int16_t pitch      = 0;    // semitones * 128, C3 default
    int8_t  decay      = 50;   // 0–100 → ~30ms–5s envelope
    int8_t  brightness = 70;   // 0–100 → 200Hz–20kHz (log) filter cutoff
    int8_t  body       = 0;    // 0–100 → 0.0–1.7 filter resonance
    int8_t  detune     = 10;   // 0–50 cents L/R spread
    int8_t  mix        = 100;  // 0=dry (passthru), 100=full wet (string only)

    CVInputMap     pitch_cv;
    DigitalInputMap trig_cv;
    CVInputMap     decay_cv;
    CVInputMap     brightness_cv;
    CVInputMap     body_cv;
    CVInputMap     detune_cv;
    CVInputMap     mix_cv;

    float env_level = 0.0f;

    AudioPassthrough<STEREO>  input_stream;
    AudioSynthKarplusStrong   synthL, synthR;
    AudioFilterLadder         filterL, filterR;
    AudioVCA                  vcaL, vcaR;
    InterpolatingStream<>     env_stream;
    AudioMixer<2>             mixerL, mixerR;
    AudioPassthrough<STEREO>  output;

    void Pluck() {
        float eff_detune = constrain(detune + detune_cv.InF() * 50.0f, 0.0f, 50.0f);
        float base  = PitchToRatio(pitch + pitch_cv.In()) * C3;
        float ratio = powf(2.0f, eff_detune / 1200.0f);
        synthL.noteOn(base / ratio, 1.0f);
        synthR.noteOn(base * ratio, 1.0f);
        env_level = 1.0f;
    }

    void UpdateFilter() {
        float hz  = 200.0f * powf(100.0f, brightness * 0.01f);
        float res = body * 0.017f;
        filterL.frequency(hz);  filterL.resonance(res);
        filterR.frequency(hz);  filterR.resonance(res);
    }

    void UpdateMix() {
        float w = mix * 0.01f;
        mixerL.gain(0, 1.0f - w);  mixerL.gain(1, w);
        mixerR.gain(0, 1.0f - w);  mixerR.gain(1, w);
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
