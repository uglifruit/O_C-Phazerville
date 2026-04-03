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
    // Map decay param to target time: T = 0.002 * e^(d * ln3000)  [0.002 s…6 s]
    // Min 0.002 s → RT60 ≈ 14 ms (very short staccato). Max 6 s → RT60 ≈ 41 s.
    // fastexp from dsputils fastapprox library
    float T_s = 0.002f * fastexp(decay_param_ * 8.006f);  // ln(3000) ≈ 8.006
    // ρ = exp(-1 / (T_s * fs))  — apply once per sample.
    // Over one period L the combined gain is ρ^L = exp(-L/(T_s*fs)), giving
    // perceptual decay time ≈ T_s regardless of pitch (pitch-compensated).
    // Do NOT include L in the exponent — that would apply a full period's
    // attenuation on every sample, decaying the string L× too fast.
    float loop_gain = expf(-1.0f / (T_s * AUDIO_SAMPLE_RATE_EXACT));
    // Hard cap: system must stay stable regardless of parameter extremes.
    // Max legitimate gain at T_s=15 s is exp(-1/(15*44100)) ≈ 0.9999985 — keep
    // cap above that so the full decay range is usable.
    if (loop_gain > 0.9999990f) loop_gain = 0.9999990f;

    // --- Handle pending trigger (fills delay line with excited noise) -----
    if (trigger_pending_) {
      trigger_pending_ = false;
      // Snap to target pitch immediately so exciteString() fills the delay
      // line at the correct length. Without this, the one-pole smoother is
      // still mid-transition on a large pitch jump, causing the string to
      // slide from the previous pitch rather than start at the new one.
      smooth_delay_ = target_delay_;
      exciteString(trigger_velocity_);
    }

    // --- Per-sample KS feedback loop -------------------------------------
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {

      // Fractional read position: L samples behind the write head
      float read_pos = static_cast<float>(write_idx_) - smooth_delay_;
      if (read_pos < 0.0f) read_pos += static_cast<float>(BUFFER_SIZE);

      // Extract fractional part from the UNMASKED floor of read_pos.
      // After the negative-wrap guard, read_pos can be up to ~8190 (when
      // write_idx_ is near 4095 and smooth_delay_ is near BUFFER_SIZE).
      // r0_raw may therefore exceed BUFFER_MASK. frac MUST be computed from
      // r0_raw; using (r0_raw & BUFFER_MASK) instead would give frac ≈ 4096
      // and corrupt the interpolation.
      uint32_t r0_raw = static_cast<uint32_t>(read_pos);
      float    frac   = read_pos - static_cast<float>(r0_raw);
      uint32_t r0     = r0_raw & BUFFER_MASK;
      uint32_t r1     = (r0 + 1) & BUFFER_MASK;

      // Linear interpolation for sub-sample accuracy (resolves pitch quantization)
      float raw = delay_line_[r0] + frac * (delay_line_[r1] - delay_line_[r0]);

      // 1st-order IIR low-pass (Brightness)
      //   y[n] = α * x[n] + (1 - α) * y[n-1]
      float filtered  = iir_alpha_ * raw + (1.0f - iir_alpha_) * iir_state_;
      iir_state_      = filtered;

      // Pitch-compensated loop gain (Decay)
      float new_sample = filtered * loop_gain;

      // Write back: delay line values live in q15 float units (±32767).
      // Keep write_idx_ masked (0–4095) so float(write_idx_) stays within
      // float32's exact integer range; avoids precision loss after ~6 min.
      delay_line_[write_idx_] = new_sample;
      write_idx_ = (write_idx_ + 1) & BUFFER_MASK;

      out->data[i] = Clip16(new_sample);
    }

    transmit(out);
    release(out);
  }

private:
  // -----------------------------------------------------------------------
  // Excitation: fill the delay line with a noise/sine blend controlled by
  // body_param_. body=0 gives pure white noise (bright, percussive attack).
  // body=1 gives a pure sine at the string fundamental (clean, smooth attack).
  // Middle values crossfade linearly, giving an audible sweep from noisy to
  // pure across the full 0–100 display range.
  //
  // This is distinct from Brightness, which shapes the ongoing feedback IIR
  // (affects sustain/timbre). Body affects the attack transient only.
  //
  // sinf is called once per note-on (not per-block), so even for the lowest
  // pitch (n ≈ 2205) the cost is ~110 µs — well within the 2.9 ms budget.
  // -----------------------------------------------------------------------
  void exciteString(float velocity) {
    // Number of samples to fill = one full period (≈ delay_length)
    int n = static_cast<int>(smooth_delay_);
    if (n < 2)                        n = 2;
    if (n >= static_cast<int>(BUFFER_SIZE)) n = BUFFER_SIZE - 1;

    // Wipe the delay line and IIR state for a clean, click-free attack
    memset(delay_line_, 0, BUFFER_SIZE * sizeof(float));
    iir_state_ = 0.0f;

    float excite_scale = velocity * 32767.0f;

    // Sine step: one full cycle over n samples (= one string period)
    static constexpr float KS_TWO_PI = 6.28318530718f;
    float body_omega = KS_TWO_PI / static_cast<float>(n);

    for (int i = 0; i < n; i++) {
      // LCG white noise: good spectral flatness, zero allocation overhead
      noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
      float noise = static_cast<float>(static_cast<int32_t>(noise_seed_))
                    * (1.0f / 2147483648.0f);  // [-1.0, 1.0]

      // Sine at fundamental — one full period across the delay line
      float sine = sinf(body_omega * static_cast<float>(i));

      // body=0 → pure noise (bright/percussive), body=1 → pure sine (clean/smooth)
      float sample = ((1.0f - body_param_) * noise + body_param_ * sine) * excite_scale;

      // Clamp to q15 float range
      if (sample >  32767.0f) sample =  32767.0f;
      if (sample < -32767.0f) sample = -32767.0f;

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
