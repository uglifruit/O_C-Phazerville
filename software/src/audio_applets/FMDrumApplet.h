#pragma once

#include "synth_waveform.h"
#include "synth_whitenoise.h"
#include "../src/Audio/filter_variable2.h"

class FMDrumApplet : public HemisphereAudioApplet {
public:
    const char* applet_name() override { return "FMDrum"; }

    AudioStream* InputStream()  override { return &input_stream; }
    AudioStream* OutputStream() override { return &output_mixer; }

    void Start() override {
        // Acquire interpolating streams
        fm_idx_stream.Acquire();
        fm_idx_stream.Method(INTERPOLATION_LINEAR);
        amp_env_stream.Acquire();
        amp_env_stream.Method(INTERPOLATION_LINEAR);
        noise_env_stream.Acquire();
        noise_env_stream.Method(INTERPOLATION_LINEAR);

        // Modulator: simple sine, phase-resettable
        modulator.begin(WAVEFORM_SINE);
        modulator.amplitude(1.0f);

        // Carrier: PM synthesis, max depth 1800 degrees = 5π rad
        carrier.begin(WAVEFORM_SINE);
        carrier.amplitude(1.0f);
        carrier.phaseModulation(1800.0f);

        // mod_vca: scales modulator by FM index envelope
        mod_vca.bias(0.0f);
        mod_vca.level(1.0f);
        mod_vca.rectify(true);

        // amp_vca: scales carrier by amplitude envelope
        amp_vca.bias(0.0f);
        amp_vca.level(1.0f);
        amp_vca.rectify(true);

        // Noise: white → fixed ~1kHz HPF → VCA
        noise_gen.amplitude(1.0f);
        noise_hpf.frequency(1000.0f);
        noise_hpf.resonance(0.707f);
        noise_hpf.octaveControl(0.0f);

        noise_vca.bias(0.0f);
        noise_vca.level(1.0f);
        noise_vca.rectify(true);

        // Output mixer: gain[0] fixed, gains[1..2] updated in Controller
        output_mixer.gain(0, 1.0f);
        output_mixer.gain(1, 0.0f);
        output_mixer.gain(2, 0.0f);

        // --- Cable routing ---
        // FM chain
        PatchCable(modulator,       0, mod_vca,      0);
        PatchCable(fm_idx_stream,   0, mod_vca,      1);
        PatchCable(mod_vca,         0, carrier,      0);  // PM mod input

        // Carrier → amp envelope → mixer ch0
        PatchCable(carrier,         0, amp_vca,      0);
        PatchCable(amp_env_stream,  0, amp_vca,      1);
        PatchCable(amp_vca,         0, output_mixer, 0);

        // Noise chain: noise → HPF → noise VCA → mixer ch1
        PatchCable(noise_gen,       0, noise_hpf,    0);
        PatchCable(noise_hpf,       2, noise_vca,    0);  // output 2 = HP
        PatchCable(noise_env_stream,0, noise_vca,    1);
        PatchCable(noise_vca,       0, output_mixer, 1);

        // Passthrough: audio in → mixer ch2 (level via mixer gain)
        PatchCable(input_stream,    0, output_mixer, 2);

        // Prime streams to silence
        fm_idx_stream.Push(float_to_q15(0.0f));
        amp_env_stream.Push(float_to_q15(0.0f));
        noise_env_stream.Push(float_to_q15(0.0f));
    }

    void Unload() override {
        fm_idx_stream.Release();
        amp_env_stream.Release();
        noise_env_stream.Release();
        AllowRestart();
    }

    void Controller() override {
        // --- CV application (bipolar via Proportion) ---
        float eff_pitch = constrain(
            (float)pitch_hz + Proportion(pitch_cv.In(), HEMISPHERE_MAX_INPUT_CV, 1000),
            10.f, 8000.f);
        float eff_dec = constrain(
            (float)dec + Proportion(dec_cv.In(), HEMISPHERE_MAX_INPUT_CV, 1000),
            1.f, 5000.f);
        float eff_swp = constrain(
            (float)swp + Proportion(swp_cv.In(), HEMISPHERE_MAX_INPUT_CV, 100),
            0.f, 200.f);
        float eff_rto = constrain(
            (float)rto + Proportion(rto_cv.In(), HEMISPHERE_MAX_INPUT_CV, 50),
            0.1f, 200.f);
        float eff_fmi = constrain(
            (float)fmi + Proportion(fmi_cv.In(), HEMISPHERE_MAX_INPUT_CV, 100),
            0.f, 200.f);
        float eff_fmd = constrain(
            (float)fmd_s * 10.f + Proportion(fmd_cv.In(), HEMISPHERE_MAX_INPUT_CV, 500),
            1.f, 5000.f);
        float eff_noi = constrain(
            (float)noi + Proportion(noi_cv.In(), HEMISPHERE_MAX_INPUT_CV, 100),
            0.f, 200.f);
        float eff_ndc = constrain(
            (float)ndc + Proportion(ndc_cv.In(), HEMISPHERE_MAX_INPUT_CV, 500),
            1.f, 5000.f);
        float eff_mix = constrain(
            (float)mix + Proportion(mix_cv.In(), HEMISPHERE_MAX_INPUT_CV, 100),
            0.f, 100.f);

        // --- Per-block decay coefficients ---
        // coeff = exp(-AUDIO_BLOCK_SAMPLES / (decay_s * AUDIO_SAMPLE_RATE_EXACT))
        const float sr = AUDIO_SAMPLE_RATE_EXACT;
        float amp_coeff   = expf(-128.f / (eff_dec * 0.001f * sr));
        float fm_coeff    = expf(-128.f / (eff_fmd * 0.001f * sr));
        float noise_coeff = expf(-128.f / (eff_ndc * 0.001f * sr));

        // --- Trigger detection ---
        if (trg.Clock()) {
            amp_env   = 1.0f;
            fm_env    = 1.0f;
            noise_env = 1.0f;
            modulator.phase(0.0f);  // phase reset for consistent transient
            trigger_flash = 8;
        }

        // --- Advance envelopes ---
        amp_env   *= amp_coeff;
        fm_env    *= fm_coeff;
        noise_env *= noise_coeff;
        if (trigger_flash > 0) --trigger_flash;

        // --- Oscillator frequencies ---
        // Pitch sweep: adds (swp% * pitch) Hz at envelope peak, decays with amp_env
        float sweep_hz   = (eff_swp * 0.01f) * eff_pitch * amp_env;
        float carrier_hz = eff_pitch + sweep_hz;
        carrier.frequency(constrain(carrier_hz, 1.f, 20000.f));
        modulator.frequency(constrain(carrier_hz * eff_rto * 0.1f, 1.f, 20000.f));

        // --- Push envelope values to audio streams ---
        fm_idx_stream.Push(
            float_to_q15(constrain(fm_env * eff_fmi * 0.01f, 0.f, 1.f)));
        amp_env_stream.Push(
            float_to_q15(constrain(amp_env, 0.f, 1.f)));
        noise_env_stream.Push(
            float_to_q15(constrain(noise_env, 0.f, 1.f)));

        // --- Mixer gains for noise and passthrough ---
        output_mixer.gain(1, constrain(eff_noi * 0.01f, 0.f, 2.f));
        output_mixer.gain(2, constrain(eff_mix * 0.01f, 0.f, 1.f));
    }

    void View() override {
        // Header
        gfxPrint(1, 2, "FMDrum");
        if (preset_idx < NUM_PRESETS)
            gfxPrint(40, 2, PRESETS[preset_idx].name);
        else
            gfxPrint(40, 2, "Rnd");
        if (trigger_flash)
            gfxIcon(56, 2, ZAP_ICON);

        // Draw 6 visible rows from scroll_top
        for (int row = 0; row < 6; ++row) {
            int param = scroll_top + row;
            if (param >= NUM_CURSORS) break;
            int y = 15 + row * 8;
            DrawRow(param, y);
        }

        // Scroll arrows
        if (scroll_top > 0)
            gfxIcon(57, 14, UP_ICON);
        if (scroll_top + 6 < NUM_CURSORS)
            gfxIcon(57, 56, DOWN_ICON);

        gfxDisplayInputMapEditor();
    }

    void OnEncoderMove(int direction) override {
        if (!EditMode()) {
            MoveCursor(cursor, direction, NUM_CURSORS - 1);
            // Keep cursor visible
            if (cursor < scroll_top)
                scroll_top = cursor;
            else if (cursor >= scroll_top + 6)
                scroll_top = cursor - 5;
            scroll_top = constrain(scroll_top, 0, NUM_CURSORS - 6);
            return;
        }

        if (EditSelectedInputMap(direction)) return;

        switch (cursor) {
            case TRG:    trg.ChangeSource(direction); break;
            case PIT:    pitch_hz = constrain(pitch_hz + direction * 5, 10, 2000); break;
            case DCY:    dec      = constrain(dec + direction * 5, 10, 2000); break;
            case SWP:    swp      = constrain(swp + direction, 0, 100); break;
            case RTO:    rto      = constrain(rto + direction, 1, 100); break;
            case FMI:    fmi      = constrain(fmi + direction, 0, 100); break;
            case FMD:    fmd_s    = constrain(fmd_s + direction, 1, 100); break;
            case NOI:    noi      = constrain(noi + direction, 0, 100); break;
            case NDC:    ndc      = constrain(ndc + direction * 5, 5, 1000); break;
            case MIX:    mix      = constrain(mix + direction, 0, 100); break;
            case CV_PIT: pitch_cv.ChangeSource(direction); break;
            case CV_DCY: dec_cv.ChangeSource(direction);   break;
            case CV_SWP: swp_cv.ChangeSource(direction);   break;
            case CV_RTO: rto_cv.ChangeSource(direction);   break;
            case CV_FMI: fmi_cv.ChangeSource(direction);   break;
            case CV_FMD: fmd_cv.ChangeSource(direction);   break;
            case CV_NOI: noi_cv.ChangeSource(direction);   break;
            case CV_NDC: ndc_cv.ChangeSource(direction);   break;
            case CV_MIX: mix_cv.ChangeSource(direction);   break;
            default: break;
        }
    }

    void OnButtonPress() override {
        // TRG row opens DigitalInputMap editor via CursorToggle (edit mode changes source)
        if (cursor == TRG) {
            CursorToggle();
            return;
        }
        if (CheckEditInputMapPress(cursor,
              IndexedInput(CV_PIT, pitch_cv),
              IndexedInput(CV_DCY, dec_cv),
              IndexedInput(CV_SWP, swp_cv),
              IndexedInput(CV_RTO, rto_cv),
              IndexedInput(CV_FMI, fmi_cv),
              IndexedInput(CV_FMD, fmd_cv),
              IndexedInput(CV_NOI, noi_cv),
              IndexedInput(CV_NDC, ndc_cv),
              IndexedInput(CV_MIX, mix_cv)))
            return;
        CursorToggle();
    }

    void AuxButton() override {
        preset_idx = (preset_idx + 1) % (NUM_PRESETS + 1);
        LoadPreset(preset_idx);
    }

    void OnDataRequest(std::array<uint64_t, CONFIG_SIZE>& data) override {
        data[0] = PackPackables(pitch_hz, dec, swp, rto, fmi, fmd_s);
        data[1] = PackPackables(noi, ndc, mix, trg, mix_cv);
        data[2] = PackPackables(pitch_cv, dec_cv, swp_cv, rto_cv);
        data[3] = PackPackables(fmi_cv, fmd_cv, noi_cv, ndc_cv);
    }

    void OnDataReceive(const std::array<uint64_t, CONFIG_SIZE>& data) override {
        UnpackPackables(data[0], pitch_hz, dec, swp, rto, fmi, fmd_s);
        UnpackPackables(data[1], noi, ndc, mix, trg, mix_cv);
        UnpackPackables(data[2], pitch_cv, dec_cv, swp_cv, rto_cv);
        UnpackPackables(data[3], fmi_cv, fmd_cv, noi_cv, ndc_cv);
    }

protected:
    void SetHelp() override {}

private:
    enum Cursor : int8_t {
        TRG,
        PIT, DCY, SWP, RTO, FMI, FMD, NOI, NDC, MIX,
        CV_PIT, CV_DCY, CV_SWP, CV_RTO, CV_FMI, CV_FMD, CV_NOI, CV_NDC, CV_MIX,
        NUM_CURSORS
    };

    // --- Parameters ---
    int16_t pitch_hz = 100;  // 10..2000 Hz
    int16_t dec      = 400;  // 10..2000 ms
    int8_t  swp      = 60;   // 0..100 %
    int8_t  rto      = 10;   // 1..100 (÷10 = 0.1..10.0×)
    int8_t  fmi      = 80;   // 0..100 %
    int8_t  fmd_s    = 20;   // 1..100 (×10 = 10..1000 ms)
    int8_t  noi      = 20;   // 0..100 %
    int16_t ndc      = 80;   // 5..1000 ms
    int8_t  mix      = 0;    // 0..100 %

    DigitalInputMap trg;
    CVInputMap pitch_cv, dec_cv, swp_cv, rto_cv;
    CVInputMap fmi_cv, fmd_cv, noi_cv, ndc_cv, mix_cv;

    // --- Envelope state (not saved) ---
    float amp_env   = 0.0f;
    float fm_env    = 0.0f;
    float noise_env = 0.0f;

    // --- UI state ---
    int8_t  cursor      = TRG;
    int8_t  scroll_top  = 0;
    uint8_t trigger_flash = 0;
    uint8_t preset_idx  = 0;

    // --- Audio objects ---
    AudioPassthrough<MONO>       input_stream;
    AudioSynthWaveform           modulator;
    InterpolatingStream<>        fm_idx_stream;
    AudioVCA                     mod_vca;
    AudioSynthWaveformModulated  carrier;
    InterpolatingStream<>        amp_env_stream;
    AudioVCA                     amp_vca;
    AudioSynthNoiseWhite         noise_gen;
    AudioFilterStateVariable2    noise_hpf;
    InterpolatingStream<>        noise_env_stream;
    AudioVCA                     noise_vca;
    AudioMixer<3>                output_mixer;

    // --- Preset system ---
    struct FMDrumPreset {
        int16_t pitch_hz;
        int16_t dec;
        int8_t  swp;
        int8_t  rto;
        int8_t  fmi;
        int8_t  fmd_s;
        int8_t  noi;
        int16_t ndc;
        int8_t  mix;
        const char* name;
    };

    static const int NUM_PRESETS = 5;
    static constexpr FMDrumPreset PRESETS[NUM_PRESETS] = {
        //        hz   dec  swp  rto  fmi  fmd noi  ndc  mix  name
        {  60,   500,  80,  10,  90,  20,   5,  30,  0, "Kick"  },
        { 200,   200,  30,  15,  60,  10,  70, 120,  0, "Snare" },
        { 800,    40,   0,  10,  20,   3, 100,  40,  0, "HiHat" },
        { 120,   350,  60,  12,  70,  15,  15,  60,  0, "Tom"   },
        { 300,    80,   5,   8,  40,   5,  90,  80,  0, "Clap"  },
    };

    void LoadPreset(int idx) {
        if (idx < NUM_PRESETS) {
            const auto& p = PRESETS[idx];
            pitch_hz = p.pitch_hz;
            dec      = p.dec;
            swp      = p.swp;
            rto      = p.rto;
            fmi      = p.fmi;
            fmd_s    = p.fmd_s;
            noi      = p.noi;
            ndc      = p.ndc;
            mix      = p.mix;
        } else {
            // Random
            pitch_hz = random(20, 800);
            dec      = random(30, 800);
            swp      = random(0, 100);
            rto      = random(1, 50);
            fmi      = random(20, 100);
            fmd_s    = random(2, 50);
            noi      = random(0, 80);
            ndc      = random(20, 300);
            mix      = 0;
        }
    }

    void DrawRow(int param, int y) {
        switch (param) {
            case TRG:
                gfxPrint(1, y, "TRG:");
                gfxStartCursor(25, y);
                gfxPrint(trg);
                gfxEndCursor(cursor == TRG, false, trg.InputName());
                break;
            case PIT:
                gfxPrint(1, y, "Pit:");
                gfxStartCursor(25, y);
                graphics.printf("%4d", (int)pitch_hz);
                gfxEndCursor(cursor == PIT);
                break;
            case DCY:
                gfxPrint(1, y, "Dec:");
                gfxStartCursor(25, y);
                graphics.printf("%4d", (int)dec);
                gfxEndCursor(cursor == DCY);
                break;
            case SWP:
                gfxPrint(1, y, "Swp:");
                gfxStartCursor(25, y);
                graphics.printf("%3d%%", (int)swp);
                gfxEndCursor(cursor == SWP);
                break;
            case RTO:
                gfxPrint(1, y, "Rto:");
                gfxStartCursor(25, y);
                graphics.printf("%2d.%1d", rto / 10, rto % 10);
                gfxEndCursor(cursor == RTO);
                break;
            case FMI:
                gfxPrint(1, y, "FMi:");
                gfxStartCursor(25, y);
                graphics.printf("%3d%%", (int)fmi);
                gfxEndCursor(cursor == FMI);
                break;
            case FMD:
                gfxPrint(1, y, "FMd:");
                gfxStartCursor(25, y);
                graphics.printf("%4d", (int)fmd_s * 10);
                gfxEndCursor(cursor == FMD);
                break;
            case NOI:
                gfxPrint(1, y, "Noi:");
                gfxStartCursor(25, y);
                graphics.printf("%3d%%", (int)noi);
                gfxEndCursor(cursor == NOI);
                break;
            case NDC:
                gfxPrint(1, y, "Ndc:");
                gfxStartCursor(25, y);
                graphics.printf("%4d", (int)ndc);
                gfxEndCursor(cursor == NDC);
                break;
            case MIX:
                gfxPrint(1, y, "Mix:");
                gfxStartCursor(25, y);
                graphics.printf("%3d%%", (int)mix);
                gfxEndCursor(cursor == MIX);
                break;
            // CV rows
            case CV_PIT:
                gfxPrint(1, y, " Pit>");
                gfxStartCursor(31, y);
                gfxPrint(pitch_cv);
                gfxEndCursor(cursor == CV_PIT, false, pitch_cv.InputName());
                break;
            case CV_DCY:
                gfxPrint(1, y, " Dec>");
                gfxStartCursor(31, y);
                gfxPrint(dec_cv);
                gfxEndCursor(cursor == CV_DCY, false, dec_cv.InputName());
                break;
            case CV_SWP:
                gfxPrint(1, y, " Swp>");
                gfxStartCursor(31, y);
                gfxPrint(swp_cv);
                gfxEndCursor(cursor == CV_SWP, false, swp_cv.InputName());
                break;
            case CV_RTO:
                gfxPrint(1, y, " Rto>");
                gfxStartCursor(31, y);
                gfxPrint(rto_cv);
                gfxEndCursor(cursor == CV_RTO, false, rto_cv.InputName());
                break;
            case CV_FMI:
                gfxPrint(1, y, " FMi>");
                gfxStartCursor(31, y);
                gfxPrint(fmi_cv);
                gfxEndCursor(cursor == CV_FMI, false, fmi_cv.InputName());
                break;
            case CV_FMD:
                gfxPrint(1, y, " FMd>");
                gfxStartCursor(31, y);
                gfxPrint(fmd_cv);
                gfxEndCursor(cursor == CV_FMD, false, fmd_cv.InputName());
                break;
            case CV_NOI:
                gfxPrint(1, y, " Noi>");
                gfxStartCursor(31, y);
                gfxPrint(noi_cv);
                gfxEndCursor(cursor == CV_NOI, false, noi_cv.InputName());
                break;
            case CV_NDC:
                gfxPrint(1, y, " Ndc>");
                gfxStartCursor(31, y);
                gfxPrint(ndc_cv);
                gfxEndCursor(cursor == CV_NDC, false, ndc_cv.InputName());
                break;
            case CV_MIX:
                gfxPrint(1, y, " Mix>");
                gfxStartCursor(31, y);
                gfxPrint(mix_cv);
                gfxEndCursor(cursor == CV_MIX, false, mix_cv.InputName());
                break;
            default:
                break;
        }
    }
};

// Required for constexpr static member with non-trivial destructor
constexpr FMDrumApplet::FMDrumPreset FMDrumApplet::PRESETS[FMDrumApplet::NUM_PRESETS];
