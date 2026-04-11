#pragma once

// AudioSpectralFreeze — Phase Vocoder Spectral Freeze
//
// Implements a real-time spectral freeze via Overlap-Add (OLA) phase vocoder.
// Uses arm_rfft_fast_f32 (1024-point real FFT) from CMSIS-DSP for efficient
// forward and inverse transforms on the Cortex-M7.
//
// Signal flow:
//   audio in → input FIFO (ring buffer)
//              every 256 samples: analysis FFT → spectrum processing → IFFT → OLA
//   audio out ← OLA ring buffer (drained 128 samples per update() call)
//
// Parameters (written by Controller() in the audio ISR — no race condition):
//   smear  0.0–1.0  Phase randomisation: 0 = accurate, 1 = fully random
//   frozen bool     When true, spectral magnitudes are held; synthesis uses
//                   smear-weighted phase (accurate ↔ random each frame)
//
// MIT License — (c) 2026 Andy Jenkinson (uglifruit) with ClaudeCode

#pragma once

#include <AudioStream.h>
#include <arm_math.h>

class AudioSpectralFreeze : public AudioStream {
public:
    AudioSpectralFreeze();

    void update() override;

    // --- Parameters: set from FreezeApplet::Controller() (audio ISR context) ---
    float smear  = 0.0f;   // 0 = accurate frozen phase, 1 = fully randomised
    bool  frozen = false;  // true = spectral freeze active

    // --- Constants ---
    static constexpr int FFT_SIZE  = 1024;
    static constexpr int HOP_SIZE  = 256;   // 4× overlap
    static constexpr int NUM_BINS  = FFT_SIZE / 2 + 1; // 513: DC + 511 complex + Nyquist
    static constexpr int OLA_SIZE  = 2048;  // power-of-2 ring; holds up to 4 live windows

private:
    // Required by AudioStream: array of input queue pointers
    audio_connection_t *input_queue_array[1];

    // -------------------------------------------------------------------------
    // ARM CMSIS-DSP real FFT instance.
    // arm_rfft_fast_init_f32() sets up pointers to twiddle tables that live in
    // ROM — no additional RAM is needed for them.
    // -------------------------------------------------------------------------
    arm_rfft_fast_instance_f32 rfft_inst;
    bool rfft_ready = false;

    // -------------------------------------------------------------------------
    // Input circular ring buffer — always contains the newest FFT_SIZE samples.
    // input_write advances by AUDIO_BLOCK_SAMPLES each update() call.
    // -------------------------------------------------------------------------
    float input_buf[FFT_SIZE]  = {};
    int   input_write          = 0;

    // Counts incoming samples since the last FFT frame.
    // Fires processFrame() every HOP_SIZE (256) samples = every 2 update() calls.
    int   hop_counter          = 0;

    // -------------------------------------------------------------------------
    // FFT workspace — used in-place for both forward and inverse transforms.
    // arm_rfft_fast_f32 output format (packed real):
    //   buf[0]         = DC bin (real only)
    //   buf[1]         = Nyquist bin (real only)
    //   buf[2k], buf[2k+1] = real, imag of complex bin k  (k = 1 … 511)
    // -------------------------------------------------------------------------
    float fft_buf[FFT_SIZE]    = {};

    // -------------------------------------------------------------------------
    // Frozen spectrum storage — captured on the false→true edge of frozen.
    // Stores the raw complex FFT output rather than polar form, so smear=0
    // synthesis needs no trig at all (just a direct copy).
    //   frozen_re[0]   = DC bin (real only)
    //   frozen_re[512] = Nyquist bin (real only)
    //   frozen_re[k], frozen_im[k]  = complex bin k (k = 1…511)
    // -------------------------------------------------------------------------
    float frozen_re[NUM_BINS] = {};  // real part at freeze moment
    float frozen_im[NUM_BINS] = {};  // imaginary part at freeze moment
    bool  was_frozen          = false;

    // -------------------------------------------------------------------------
    // OLA (Overlap-Add) output ring buffer.
    //   ola_write : next position to accumulate synthesised IFFT output into.
    //               Initialised FFT_SIZE samples ahead of ola_read so there is
    //               always one full window of latency before first readout.
    //   ola_read  : position from which update() drains AUDIO_BLOCK_SAMPLES
    //               samples per call.
    // -------------------------------------------------------------------------
    float ola_buf[OLA_SIZE]    = {};
    int   ola_write            = FFT_SIZE; // start one window ahead
    int   ola_read             = 0;

    // -------------------------------------------------------------------------
    // XORshift32 PRNG — used for fast random phase generation when smear > 0.
    // Significantly cheaper than calling random() or invoking trig inside the ISR.
    // -------------------------------------------------------------------------
    uint32_t prng_state = 0xDEADBEEF;

    // --- Private helpers ---
    void  processFrame();   // Full analysis → process → synthesis pipeline
    float fastRand();       // XORshift → uniform float in [-π, π]
};
