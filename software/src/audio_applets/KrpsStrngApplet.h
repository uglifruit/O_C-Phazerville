// Copyright (c) 2026, Andy Jenkinson (uglifruit)
// MIT License
//
// KrpsStrngApplet — Karplus-Strong resonant string synthesizer (mono).
// One voice through a ladder filter with a software VCA envelope and
// dry/wet mix.  Registered MONO so each side runs an independent instance
// with properly-scoped CV inputs.
//
// CV1: V/Oct pitch   Trig: assignable trigger   Dec/Brt/Body/Mix: CV-able

#include "synth_karplusstrong.h"

class KrpsStrngApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "KrpsStrng"; }

    void Start() override {
        env_stream.Acquire();
        env_stream.Method(INTERPOLATION_LINEAR);

        // synth → filter → vca[0]
        // env_stream          → vca[1]
        // input_stream[0]     → mixer[0]  (dry)
        // vca[0]              → mixer[1]  (wet)
        // mixer               → output
        PatchCable(synth, 0, filter, 0);
        PatchCable(filter, 0, vca, 0);
        PatchCable(env_stream, 0, vca, 1);
        PatchCable(input_stream, 0, mixer, 0);  // dry
        PatchCable(vca, 0, mixer, 1);            // wet
        PatchCable(mixer, 0, output, 0);

        vca.level(1.0f);  vca.bias(0.0f);  vca.rectify(true);

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
        filter.frequency(hz);
        filter.resonance(res);

        // Mix — dry/wet blend (0=full dry, 100=full wet)
        float eff_mix = constrain(mix * 0.01f + mix_cv.InF(), 0.0f, 1.0f);
        mixer.gain(0, 1.0f - eff_mix);
        mixer.gain(1, eff_mix);

        // Pluck on trigger rising edge — StartADCLag defers Pluck() until
        // the pitch CV has settled (avoids triggering at the previous pitch)
        if (trig_cv.Clock()) StartADCLag();
        if (EndOfADCLag()) Pluck();
    }

    void View() override {
        // Row 1: pitch (note = semitone steps, Hz = fine steps) + V/Oct CV source
        gfxStartCursor(1, 15);
        gfxPrintTuningIndicator(pitch);
        gfxEndCursor(cursor == PITCH);
        gfxStartCursor(11, 15);
        gfxPrintPitchHz(pitch);
        gfxEndCursor(cursor == PITCH_FINE);
        gfxStartCursor(46, 15);
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
        graphics.printf("%3d", decay);
        gfxEndCursor(cursor == DECAY);
        gfxStartCursor(46, 35);
        gfxPrint(decay_cv);
        gfxEndCursor(cursor == DECAY_CV, false, decay_cv.InputName());

        // Row 4: brightness + CV
        gfxPrint(1, 45, "Br:");
        gfxStartCursor(19, 45);
        PrintBrightnessHz();
        gfxEndCursor(cursor == BRIGHTNESS);
        gfxStartCursor(46, 45);
        gfxPrint(brightness_cv);
        gfxEndCursor(cursor == BRIGHTNESS_CV, false, brightness_cv.InputName());

        // Row 5: context-sensitive — Body+CV | Mix+CV depending on cursor
        if (cursor <= BODY_CV) {
            gfxPrint(1, 55, "Bdy:");
            gfxStartCursor(25, 55);
            graphics.printf("%3d", body);
            gfxEndCursor(cursor == BODY);
            gfxStartCursor(46, 55);
            gfxPrint(body_cv);
            gfxEndCursor(cursor == BODY_CV, false, body_cv.InputName());
        } else {
            gfxPrint(1, 55, "Mix:");
            gfxStartCursor(25, 55);
            graphics.printf("%3d", mix);
            gfxEndCursor(cursor == MIX);
            gfxStartCursor(46, 55);
            gfxPrint(mix_cv);
            gfxEndCursor(cursor == MIX_CV, false, mix_cv.InputName());
        }

        gfxDisplayInputMapEditor();
    }

    void OnButtonPress() override {
        if (CheckEditInputMapPress(cursor,
              IndexedInput(PITCH_CV,      pitch_cv),
              IndexedInput(TRIG_CV,       trig_cv),
              IndexedInput(DECAY_CV,      decay_cv),
              IndexedInput(BRIGHTNESS_CV, brightness_cv),
              IndexedInput(BODY_CV,       body_cv),
              IndexedInput(MIX_CV,        mix_cv)))
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
            case PITCH_FINE:
                pitch = constrain(pitch + direction * 4, MIN_PITCH, MAX_PITCH);
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
            case BODY_CV:
                body_cv.ChangeSource(direction);
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
        data[0] = PackPackables(pitch, decay, brightness, body, mix);
        data[1] = PackPackables(pitch_cv, trig_cv, decay_cv);
        data[2] = PackPackables(brightness_cv, body_cv, mix_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], pitch, decay, brightness, body, mix);
        UnpackPackables(data[1], pitch_cv, trig_cv, decay_cv);
        UnpackPackables(data[2], brightness_cv, body_cv, mix_cv);
        pitch      = constrain(pitch,      -2*12*128, 5*12*128);
        decay      = constrain(decay,      0, 100);
        brightness = constrain(brightness, 0, 100);
        body       = constrain(body,       0, 100);
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
        PITCH, PITCH_FINE, PITCH_CV, TRIG_CV,
        DECAY, DECAY_CV,
        BRIGHTNESS, BRIGHTNESS_CV,
        BODY, BODY_CV,
        MIX, MIX_CV
    };

    int8_t  cursor     = PITCH;
    int16_t pitch      = 0;    // semitones * 128, C3 default
    int8_t  decay      = 50;   // 0–100 → ~30ms–5s envelope
    int8_t  brightness = 70;   // 0–100 → 200Hz–20kHz (log) filter cutoff
    int8_t  body       = 0;    // 0–100 → 0.0–1.7 filter resonance
    int8_t  mix        = 100;  // 0=dry (passthru), 100=full wet (string only)

    CVInputMap      pitch_cv;
    DigitalInputMap trig_cv;
    CVInputMap      decay_cv;
    CVInputMap      brightness_cv;
    CVInputMap      body_cv;
    CVInputMap      mix_cv;

    float env_level = 0.0f;

    AudioPassthrough<MONO>    input_stream;
    AudioSynthKarplusStrong   synth;
    AudioFilterLadder         filter;
    AudioVCA                  vca;
    InterpolatingStream<>     env_stream;
    AudioMixer<2>             mixer;
    AudioPassthrough<MONO>    output;

    void Pluck() {
        float base = PitchToRatio(pitch + pitch_cv.In()) * C3;
        synth.noteOn(base, 1.0f);
        env_level = 1.0f;
    }

    void UpdateFilter() {
        float hz  = 200.0f * powf(100.0f, brightness * 0.01f);
        float res = body * 0.017f;
        filter.frequency(hz);
        filter.resonance(res);
    }

    void UpdateMix() {
        float w = mix * 0.01f;
        mixer.gain(0, 1.0f - w);
        mixer.gain(1, w);
    }

    void PrintBrightnessHz() {
        int hz = (int)(200.0f * powf(100.0f, brightness * 0.01f));
        if (hz < 1000)
            graphics.printf("%3dH", hz);           // e.g. "200H"  (4 chars)
        else if (hz < 10000)
            graphics.printf("%d.%dk", hz / 1000, (hz % 1000) / 100); // e.g. "3.1k"
        else
            graphics.printf("%3dk", hz / 1000);    // e.g. " 20k"  (4 chars)
    }
};
