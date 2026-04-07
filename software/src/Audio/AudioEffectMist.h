#pragma once

#include "AudioBuffer.h"
#include "../dsputils.h"
#include "../extern/stmlib_utils_random.h"
#include <Audio.h>
#include <math.h>

// Thin subclass of ExtAudioBuffer exposing write position and raw buffer
// access needed by AudioEffectMist for absolute-position grain reads.
template <typename T = int16_t>
class MistCircBuffer : public ExtAudioBuffer<T> {
public:
    using ExtAudioBuffer<T>::ExtAudioBuffer;
    size_t GetWriteIx() const { return this->write_ix; }
    T*     RawBuffer()  const { return this->buffer; }
    bool   IsReady()    const { return this->buffer != nullptr; }
};

// Live granular processor. Continuously records audio into a circular PSRAM
// buffer. On each Controller() tick, the applet calls setters to configure:
//   Position    — where in the buffer grains are spawned (0=live, 1=oldest)
//   Density     — grain spawn rate in Hz
//   Size        — grain duration in seconds
//   Spray       — position scatter (randomises start point per grain)
//   Pitch       — playback speed ratio (0.5=−1oct, 1.0=unity, 2.0=+1oct)
//   PitchSpread — random pitch offset per grain in semitones (0=none)
//   Freeze      — stops the write pointer; grains replay recorded history only
//
// Output is the summed wet signal only; dry/wet blend is handled by the
// AudioMixer<2> in MistApplet.
class AudioEffectMist : public AudioStream {
public:
    static const size_t MIST_BUFFER_SAMPLES = AUDIO_SAMPLE_RATE; // 1 sec at sample rate
    static const int    MAX_GRAINS = 12;

    enum GrainShape : uint8_t { SHAPE_HANN = 0, SHAPE_TRIANGLE, SHAPE_RAMP_UP, SHAPE_RAMP_DOWN };

    AudioEffectMist(size_t buf_len = MIST_BUFFER_SAMPLES)
        : AudioStream(1, input_queue_array), g_buffer(buf_len) {}

    void Acquire() { g_buffer.Acquire(); }
    void Release() { g_buffer.Release(); }
    bool IsReady() const { return g_buffer.IsReady(); }

    // Setters — called from Controller() (ISR rate ~16.6 kHz).
    // Parameters are volatile; update() snapshots them once per block.
    void setPosition(float p)        { pos_     = p; }
    void setDensity(float hz)        { density_ = hz; }
    void setSize(float secs)         { size_    = secs; }
    void setSpray(float s)           { spray_   = s; }
    void setPitch(float r)           { pitch_   = r; }
    void setPitchSpread(float semis) { psprd_   = semis; }
    void setFreeze(bool f)           { freeze_  = f; }
    void setShape(GrainShape s)      { shape_   = s; }

    uint8_t ActiveGrainCount() const {
        uint8_t n = 0;
        for (const auto& g : grains) if (g.active) n++;
        return n;
    }

    void update() override {
        audio_block_t* in  = receiveReadOnly(0);
        audio_block_t* out = allocate();
        if (!out) { if (in) release(in); return; }

        // Snapshot volatile params once for this block.
        const float   cur_pos     = pos_;
        const float   cur_density = density_;
        const float   cur_size    = size_;
        const float   cur_spray   = spray_;
        const float   cur_pitch   = pitch_;
        const float   cur_psprd   = psprd_;
        const bool       cur_freeze  = freeze_;
        const GrainShape cur_shape   = shape_;
        const size_t     buf_size    = g_buffer.NumSamples;

        // Write incoming audio unless frozen.
        if (in && !cur_freeze) g_buffer.Write(in);

        if (!g_buffer.IsReady()) {
            // No PSRAM — pass through unchanged.
            if (in) {
                memcpy(out->data, in->data, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
            } else {
                memset(out->data, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
            }
            transmit(out, 0);
            release(out);
            if (in) release(in);
            return;
        }

        int16_t* buf = g_buffer.RawBuffer();

        // Process each sample in the block.
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {

            // ── Grain scheduling ───────────────────────────────────────────
            // Phase accumulator advances by density/AUDIO_SAMPLE_RATE per sample.
            spawn_phase_ += cur_density / AUDIO_SAMPLE_RATE_EXACT;
            if (spawn_phase_ >= 1.0f) {
                spawn_phase_ -= 1.0f;
                spawnGrain(cur_pos, cur_size, cur_spray, cur_pitch, cur_psprd, cur_shape, buf_size);
            }

            // ── Sum active grains ──────────────────────────────────────────
            float accum = 0.0f;
            for (auto& g : grains) {
                if (!g.active) continue;

                float t = (float)g.phase / (float)(g.grain_len - 1);
                float w;
                switch (g.shape) {
                    case SHAPE_TRIANGLE:
                        w = (t < 0.5f) ? (2.0f * t) : (2.0f - 2.0f * t); break;
                    case SHAPE_RAMP_UP:
                        w = t; break;
                    case SHAPE_RAMP_DOWN:
                        w = 1.0f - t; break;
                    default: { // SHAPE_HANN: sin²(π × t)
                        float s = sinf(3.14159265358979f * t);
                        w = s * s;
                    } break;
                }

                // Hermite-interpolated read at g.read_ptr.
                size_t idx  = (size_t)g.read_ptr;
                float  frac = g.read_ptr - (float)idx;
                // Four surrounding points (with wrap).
                float s0 = (float)buf[(idx + buf_size - 1) % buf_size];
                float s1 = (float)buf[idx % buf_size];
                float s2 = (float)buf[(idx + 1) % buf_size];
                float s3 = (float)buf[(idx + 2) % buf_size];
                accum += InterpHermite(s0, s1, s2, s3, frac) * w;

                // Advance grain read pointer by pitch ratio; wrap in-place.
                g.read_ptr += g.pitch;
                if (g.read_ptr >= (float)buf_size) g.read_ptr -= (float)buf_size;
                if (g.read_ptr < 0.0f)             g.read_ptr += (float)buf_size;

                g.phase++;
                if (g.phase >= g.grain_len) g.active = false;
            }

            // Scale by equal-power coefficient for MAX_GRAINS simultaneous sources.
            // EQUAL_POWER_EQUAL_MIX has entries up to index 8; clamp to that.
            out->data[i] = Clip16(accum * GRAIN_SCALE);
        }

        transmit(out, 0);
        release(out);
        if (in) release(in);
    }

private:
    // Equal-power normalisation for up to 16 grains.
    // EQUAL_POWER_EQUAL_MIX[] tops out at index 8 (= 0.3536), so use that.
    // For 16 grains equal-power: 1/sqrt(16) = 0.25 — close enough.
    static constexpr float GRAIN_SCALE = 0.25f;

    struct Grain {
        bool       active    = false;
        float      read_ptr  = 0.0f;  // fractional buffer index
        float      pitch     = 1.0f;  // playback ratio
        size_t     grain_len = 0;     // duration in samples
        size_t     phase     = 0;     // position within grain
        GrainShape shape     = SHAPE_HANN;
    } grains[MAX_GRAINS];

    MistCircBuffer<int16_t> g_buffer;
    audio_block_t* input_queue_array[1];

    // Grain scheduling state (audio interrupt only — not volatile).
    float spawn_phase_ = 0.0f;

    // Volatile params: written from Controller() ISR, read from audio interrupt.
    volatile float pos_     = 0.5f;
    volatile float density_ = 4.0f;   // Hz
    volatile float size_    = 0.1f;   // seconds
    volatile float spray_   = 0.0f;   // fraction of buffer
    volatile float pitch_   = 1.0f;   // ratio
    volatile float psprd_   = 0.0f;   // random pitch spread per grain (semitones)
    volatile bool       freeze_  = false;
    volatile GrainShape shape_   = SHAPE_HANN;

    void spawnGrain(float cur_pos, float cur_size, float cur_spray,
                    float cur_pitch, float cur_psprd, GrainShape cur_shape, size_t buf_size) {
        // Find an inactive slot.
        Grain* g = nullptr;
        for (auto& gr : grains) {
            if (!gr.active) { g = &gr; break; }
        }
        if (!g) return; // all slots busy — skip this spawn

        // Randomise position by ±spray within the buffer.
        float scatter = cur_spray * (stmlib::Random::GetFloat() * 2.0f - 1.0f);
        float eff_pos = cur_pos + scatter;
        if (eff_pos < 0.0f) eff_pos = 0.0f;
        if (eff_pos > 1.0f) eff_pos = 1.0f;

        // Compute absolute start position in the circular buffer.
        // pos=0 → write head (most recent), pos=1 → oldest.
        float rptr = (float)g_buffer.GetWriteIx() - (float)buf_size * eff_pos;
        if (rptr < 0.0f) rptr += (float)buf_size;

        size_t glen = (size_t)(cur_size * AUDIO_SAMPLE_RATE_EXACT);
        if (glen < (size_t)AUDIO_BLOCK_SAMPLES) glen = AUDIO_BLOCK_SAMPLES;
        if (glen > buf_size / 2)                glen = buf_size / 2;

        // Apply per-grain pitch spread: random offset in ±cur_psprd semitones.
        float grain_pitch = cur_pitch;
        if (cur_psprd > 0.0f) {
            float rand_semis = cur_psprd * (stmlib::Random::GetFloat() * 2.0f - 1.0f);
            grain_pitch *= powf(2.0f, rand_semis / 12.0f);
        }

        g->read_ptr  = rptr;
        g->pitch     = grain_pitch;
        g->grain_len = glen;
        g->phase     = 0;
        g->shape     = cur_shape;
        g->active    = true;
    }
};
