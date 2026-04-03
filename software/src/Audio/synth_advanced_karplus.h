#pragma once

// AudioSynthAdvancedKarplus — custom Karplus-Strong physical model
//
// Motivation:
//   The PJRC AudioSynthKarplusStrong uses an integer delay line (pitch
//   quantization at higher frequencies) and a hardcoded 2-point average
//   filter (no independent brightness/decay/body control). This object
//   addresses both limitations.
//
// Architecture:
//   - 4096-sample float32 circular delay line (PSRAM preferred, heap fallback)
//   - Linear fractional interpolation on read: eliminates pitch quantization
//   - Tunable 1st-order IIR in feedback loop: independent Brightness control
//   - Pitch-compensated loop gain: perceptual Decay time independent of pitch
//   - Resonant bandpass "Body" filter applied to excitation noise burst
//   - One-pole smoothing on delay length: silent pitch transitions (no zipper)
//
// Thread safety:
//   setFrequency / setDecay / setBrightness / setBody: call anytime; atomic enough
//   noteOn():  sets a volatile flag; trigger handled safely in update()
//   Acquire() / Release(): call from Start() / Unload() in main loop only

#include <Audio.h>
#include <smalloc.h>
#include "../dsputils.h"

extern "C" uint8_t external_psram_size;

class AudioSynthAdvancedKarplus : public AudioStream {
public:
  // Delay line size must be a power of 2 for fast index masking
  static constexpr uint32_t BUFFER_SIZE = 4096;
  static constexpr uint32_t BUFFER_MASK = BUFFER_SIZE - 1;

  // Practical frequency limits for KS synthesis at 44.1 kHz:
  //   Low end: 20 Hz → delay = 2205 samples (fine, under BUFFER_SIZE)
  //   High end: 4000 Hz → delay = ~11 samples (below 11 is unstable)
  static constexpr float MIN_HZ = 20.0f;
  static constexpr float MAX_HZ = 4000.0f;

  AudioSynthAdvancedKarplus() : AudioStream(0, nullptr) {}

  // --- Lifecycle ---------------------------------------------------------

  // Allocate the delay line. Prefers PSRAM (16 KB there vs heap).
  // Called from the applet's Start().
  void Acquire() {
    if (delay_line_) return;
    if (external_psram_size > 0) {
      delay_line_ = static_cast<float*>(
        extmem_calloc(BUFFER_SIZE, sizeof(float))
      );
      use_extmem_ = (delay_line_ != nullptr);
    }
    if (!delay_line_) {
      delay_line_ = static_cast<float*>(calloc(BUFFER_SIZE, sizeof(float)));
      use_extmem_ = false;
    }
    // Reset internal state for a clean start
    if (delay_line_) {
      write_idx_   = 0;
      iir_state_   = 0.0f;
      smooth_delay_ = target_delay_;
      trigger_pending_ = false;
    }
  }

  void Release() {
    if (!delay_line_) return;
    if (use_extmem_) extmem_free(delay_line_);
    else             free(delay_line_);
    delay_line_ = nullptr;
  }

  // --- Parameter setters (call every Controller() tick) -----------------

  // Target fundamental frequency in Hz.
  // One-pole smoothing is applied in update() to avoid zipper noise.
  void setFrequency(float hz) {
    hz = constrain(hz, MIN_HZ, MAX_HZ);
    float d = AUDIO_SAMPLE_RATE_EXACT / hz;
    // Guard: leave at least 2 samples headroom below buffer wraparound
    target_delay_ = (d < BUFFER_SIZE - 2) ? d : BUFFER_SIZE - 2;
  }

  // Decay: 0.0 (shortest) → 1.0 (longest).
  // Internally maps to a target decay time on a log scale (0.05 s → 15 s).
  // The loop gain ρ is then pitch-compensated so that perceptual decay time
  // is independent of pitch (high notes decay as slowly as low notes).
  void setDecay(float d) {
    decay_param_ = constrain(d, 0.0f, 1.0f);
  }

  // Brightness: 0.0 (dark, heavy LPF) → 1.0 (bright, minimal LPF).
  // Controls α in the 1st-order IIR feedback filter:
  //   y[n] = α * x[n] + (1 - α) * y[n-1]
  // Large α passes high frequencies (bright); small α smooths heavily (dark).
  void setBrightness(float b) {
    brightness_param_ = constrain(b, 0.0f, 1.0f);
    // Map: 0 → α = 0.05 (very dark), 1 → α = 1.0 (full bright / no filter)
    iir_alpha_ = 0.05f + brightness_param_ * 0.95f;
  }

  // Body: 0.0 (flat noise excitation) → 1.0 (narrow resonant bandpass).
  // Controls the Q of a biquad bandpass applied to the noise burst on noteOn.
  // Higher values simulate an acoustic body cavity that emphasises the
  // fundamental before the string recirculates the excitation.
  void setBody(float body) {
    body_param_ = constrain(body, 0.0f, 1.0f);
  }

  // --- Trigger -----------------------------------------------------------

  // Fire the string. velocity: 0.0 = silent, 1.0 = full amplitude.
  // Safe to call from Controller() (flag read inside audio interrupt update()).
  void noteOn(float velocity = 1.0f) {
    trigger_velocity_  = constrain(velocity, 0.0f, 1.0f);
    trigger_pending_   = true;
  }

  // --- AudioStream update (runs in audio interrupt) ----------------------

  void update() override {
    if (!delay_line_) return;  // not yet acquired

    audio_block_t* out = allocate();
    if (!out) return;

    // --- One-pole pitch smoothing (~10 ms time constant per block) -------
    // ONE_POLE macro: out += coeff * (in - out)
    // coeff ≈ 1 - exp(-128 / (0.010 * 44100)) ≈ 0.25
    ONE_POLE(smooth_delay_, target_delay_, 0.25f);

    // --- Pitch-compensated loop gain (computed once per block) -----------
    // Map decay param to target time: T = 0.05 * e^(d * ln300)  [0.05 s…15 s]
    // fastexp from dsputils fastapprox library
    float T_s = 0.05f * fastexp(decay_param_ * 5.7038f);  // ln(300) ≈ 5.7038
    // ρ = exp(-L / (T_s * fs))  — pitch-compensated: smaller L → larger ρ
    float loop_gain = expf(-smooth_delay_ / (T_s * AUDIO_SAMPLE_RATE_EXACT));
    // Hard cap: system must stay stable regardless of parameter extremes
    if (loop_gain > 0.99999f) loop_gain = 0.99999f;

    // --- Handle pending trigger (fills delay line with excited noise) -----
    if (trigger_pending_) {
      trigger_pending_ = false;
      exciteString(trigger_velocity_);
    }

    // --- Per-sample KS feedback loop -------------------------------------
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {

      // Fractional read position: L samples behind the write head
      float read_pos = static_cast<float>(write_idx_) - smooth_delay_;
      if (read_pos < 0.0f) read_pos += static_cast<float>(BUFFER_SIZE);

      uint32_t r0  = static_cast<uint32_t>(read_pos) & BUFFER_MASK;
      uint32_t r1  = (r0 + 1) & BUFFER_MASK;
      float    frac = read_pos - static_cast<float>(r0);

      // Linear interpolation for sub-sample accuracy (resolves pitch quantization)
      float raw = delay_line_[r0] + frac * (delay_line_[r1] - delay_line_[r0]);

      // 1st-order IIR low-pass (Brightness)
      //   y[n] = α * x[n] + (1 - α) * y[n-1]
      float filtered  = iir_alpha_ * raw + (1.0f - iir_alpha_) * iir_state_;
      iir_state_      = filtered;

      // Pitch-compensated loop gain (Decay)
      float new_sample = filtered * loop_gain;

      // Write back: delay line values live in q15 float units (±32767)
      delay_line_[write_idx_ & BUFFER_MASK] = new_sample;
      write_idx_++;

      out->data[i] = Clip16(new_sample);
    }

    transmit(out);
    release(out);
  }

private:
  // -----------------------------------------------------------------------
  // Excitation: fill the delay line with body-filtered noise.
  // This implements "commuted synthesis": rather than injecting plain noise,
  // the noise is pre-filtered through a resonant bandpass that simulates the
  // acoustic body impedance before the signal enters the string model.
  // -----------------------------------------------------------------------
  void exciteString(float velocity) {
    // Number of samples to fill = one full period (≈ delay_length)
    int n = static_cast<int>(smooth_delay_);
    if (n < 2)                        n = 2;
    if (n >= static_cast<int>(BUFFER_SIZE)) n = BUFFER_SIZE - 1;

    // Wipe the delay line and IIR state for a clean, click-free attack
    memset(delay_line_, 0, BUFFER_SIZE * sizeof(float));
    iir_state_ = 0.0f;

    // --- Body biquad bandpass coefficients --------------------------------
    // Center frequency = string fundamental; Q scales with body_param.
    // Q range: 0.5 (almost flat / broadband) → 10.0 (narrow / resonant).
    float body_freq_hz = AUDIO_SAMPLE_RATE_EXACT / smooth_delay_;
    float q            = 0.5f + body_param_ * 9.5f;

    static constexpr float KS_TWO_PI = 6.28318530718f;
    float omega    = KS_TWO_PI * body_freq_hz / AUDIO_SAMPLE_RATE_EXACT;
    float sin_o    = sinf(omega);
    float cos_o    = cosf(omega);
    float alpha_bq = sin_o / (2.0f * q);
    float a0_inv   = 1.0f / (1.0f + alpha_bq);

    // Standard biquad bandpass (b1 = 0, constant 0 dB peak at centre)
    float b0 =  alpha_bq * a0_inv;
    float b2 = -alpha_bq * a0_inv;
    float a1 = -2.0f * cos_o * a0_inv;
    float a2 = (1.0f - alpha_bq) * a0_inv;

    // Biquad state (direct form I)
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    // Scale excitation to q15 float units; compensate bandpass gain drop at
    // high Q by multiplying through by Q (peak gain ≈ Q for this design)
    float excite_scale = velocity * 32767.0f;

    for (int i = 0; i < n; i++) {
      // LCG white noise: good spectral flatness, zero allocation overhead
      noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
      float noise = static_cast<float>(static_cast<int32_t>(noise_seed_))
                    * (1.0f / 2147483648.0f);  // [-1.0, 1.0]

      float sample;
      if (body_param_ < 0.01f) {
        // body = 0: flat white noise (no body colouration)
        sample = noise * excite_scale;
      } else {
        // Biquad direct form I: y[n] = b0*x[n] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
        float y = b0 * noise + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;  x1 = noise;
        y2 = y1;  y1 = y;
        // Multiply by Q to normalise the resonant amplitude reduction
        sample = y * q * excite_scale;
      }

      // Fill positions write_idx_ - n … write_idx_ - 1 (the "past" of the
      // string). Unsigned subtraction + BUFFER_MASK handles all wraparound.
      uint32_t idx = (write_idx_ - static_cast<uint32_t>(n - i)) & BUFFER_MASK;
      delay_line_[idx] = sample;
    }
  }

  // --- Delay line ---------------------------------------------------------
  float*   delay_line_ = nullptr;
  bool     use_extmem_ = false;
  uint32_t write_idx_  = 0;

  // --- Pitch smoothing ----------------------------------------------------
  float target_delay_ = 100.0f;   // samples (≈ 441 Hz default)
  float smooth_delay_ = 100.0f;   // one-pole smoothed version

  // --- Feedback filter (Brightness) ---------------------------------------
  float iir_alpha_ = 0.5f;        // computed from brightness_param
  float iir_state_ = 0.0f;

  // --- Parameter state ----------------------------------------------------
  float decay_param_      = 0.5f;
  float brightness_param_ = 0.5f;
  float body_param_       = 0.3f;

  // --- Trigger (volatile: written in main loop, read in audio interrupt) --
  volatile bool  trigger_pending_  = false;
  volatile float trigger_velocity_ = 1.0f;

  // --- Noise generation ---------------------------------------------------
  uint32_t noise_seed_ = 0xDEADBEEF;
};
