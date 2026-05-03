#pragma once
#include <AudioStream.h>

// Lightweight wavetable oscillator — no BandLimitedWaveform dependency.
// Plays a 256-sample int16_t wavetable with linear interpolation between
// adjacent samples.  Table pointer is updated lock-free from Controller();
// the audio ISR reads it each block.
//
// Usage:
//   AudioSynthTableOsc osc;
//   osc.setTable(my_table_256);   // point at int16_t[256]
//   osc.frequency(440.0f);
//   osc.amplitude(1.0f);

class AudioSynthTableOsc : public AudioStream {
public:
    AudioSynthTableOsc() : AudioStream(0, nullptr),
        phase_acc_(0), phase_inc_(0),
        amplitude_(0x7fff), table_(nullptr) {}

    void frequency(float f) {
        if (f < 0.0f) f = 0.0f;
        if (f > AUDIO_SAMPLE_RATE_EXACT * 0.5f) f = AUDIO_SAMPLE_RATE_EXACT * 0.5f;
        // phase_inc in Q32 units: full cycle = 2^32 per (sample_rate/f) samples
        phase_inc_ = (uint32_t)(f * (4294967296.0f / AUDIO_SAMPLE_RATE_EXACT));
    }

    void amplitude(float a) {
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        amplitude_ = (int16_t)(a * 32767.0f);
    }

    // Set the 256-sample wavetable.  Safe to call from Controller()
    // between audio blocks.  The table must remain valid while the
    // oscillator is running.
    void setTable(const int16_t* table) {
        table_ = table;
    }

    void update() override {
        audio_block_t* out = allocate();
        if (!out) return;

        const int16_t* tbl = table_;
        if (!tbl || amplitude_ == 0) {
            memset(out->data, 0, sizeof(out->data));
            transmit(out);
            release(out);
            return;
        }

        const int16_t amp = amplitude_;
        uint32_t ph  = phase_acc_;
        uint32_t inc = phase_inc_;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            // Top 8 bits = table index (0–255); next 8 bits = interpolation fraction
            uint8_t  idx  = (uint8_t)(ph >> 24);
            uint8_t  frac = (uint8_t)(ph >> 16);
            int16_t  s0   = tbl[idx];
            int16_t  s1   = tbl[(uint8_t)(idx + 1)];
            // Linear interpolation: s0 + frac/256 * (s1 - s0)
            int32_t  s    = (int32_t)s0 + (((int32_t)(s1 - s0) * frac) >> 8);
            // Apply amplitude
            out->data[i] = (int16_t)(((int32_t)s * amp) >> 15);
            ph += inc;
        }
        phase_acc_ = ph;

        transmit(out);
        release(out);
    }

private:
    uint32_t       phase_acc_;
    uint32_t       phase_inc_;
    int16_t        amplitude_;
    const int16_t* table_;
};
