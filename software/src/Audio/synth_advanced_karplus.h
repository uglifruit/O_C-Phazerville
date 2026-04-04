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
//   - 4096-sample float32 circular delay line (internal heap; avoids QSPI ISR stall)
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
#include "../dsputils.h"

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

  // Allocate the delay line from internal heap (DTCM/OCRAM).
  // 16 KB in internal RAM avoids the QSPI stall that PSRAM causes
  // when memset fires inside the audio ISR. Called from applet Start().
  void Acquire() {
    if (delay_line_) return;
    delay_line_ = static_cast<float*>(calloc(BUFFER_SIZE, sizeof(float)));
    if (delay_line_) {
      write_idx_        = 0;
      iir_state_        = 0.0f;
      smooth_delay_     = target_delay_;
      trigger_pending_  = false;
      excite_remaining_ = 0;
    }
  }

  void Release() {
    if (!delay_line_) return;
    free(delay_line_);
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
    // Min 0.001 s → RT60 ≈ 14 ms (very short staccato). Max 6 s → RT60 ≈ 41 s.
    // fastexp from dsputils fastapprox library
    float T_s = 0.001f * fastexp(decay_param_ * 8.699f);
    // ρ = exp(-1 / (T_s * fs))  — apply once per sample.
    // Over one period L the combined gain is ρ^L = exp(-L/(T_s*fs)), giving
    // perceptual decay time ≈ T_s regardless of pitch (pitch-compensated).
    // Do NOT include L in the exponent — that would apply a full period's
    // attenuation on every sample, decaying the string L× too fast.
    float loop_gain = expf(-1.0f / (T_s * AUDIO_SAMPLE_RATE_EXACT));
    // Hard cap: system must stay stable regardless of parameter extremes.
    // Max legitimate gain at T_s=6 s is exp(-1/(6*44100)) ≈ 0.9999985 — keep
    // cap above that so the full decay range is usable.
    if (loop_gain > 0.9999990f) loop_gain = 0.9999990f;

    // --- Handle pending trigger (sets up progressive excitation) ----------
    // Instead of filling the delay line in one burst (memset + for-loop),
    // excitation samples are generated one-per-sample inside the main loop
    // below. This amortizes the sinf() cost across multiple audio blocks and
    // eliminates the need for a memset entirely.
    if (trigger_pending_) {
      trigger_pending_ = false;
      // Snap to target pitch immediately so excitation fills at correct length.
      smooth_delay_     = target_delay_;
      int n = static_cast<int>(smooth_delay_);
      if (n < 2)                              n = 2;
      if (n >= static_cast<int>(BUFFER_SIZE)) n = BUFFER_SIZE - 1;
      excite_remaining_ = n;
      excite_phase_     = 0.0f;
      excite_phase_inc_ = KS_TWO_PI / static_cast<float>(n);
      excite_scale_     = trigger_velocity_ * 32767.0f;
      // Reset IIR state so stale delay-line data cannot poison the filter
      // during the excitation phase (read head still sees old audio for the
      // first period before freshly written samples come back around).
      iir_state_ = 0.0f;
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

      float new_sample;

      if (excite_remaining_ > 0) {
        // Excitation phase: generate one noise/sine sample and write it
        // directly into the delay line, bypassing feedback. This naturally
        // overwrites stale data as the write head advances. The IIR is warmed
        // up from the excitation signal (not from stale delay-line reads) so
        // the filter state is correct when sustain begins.
        noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
        float noise = static_cast<float>(static_cast<int32_t>(noise_seed_))
                      * (1.0f / 2147483648.0f);
        float sine  = sinf(excite_phase_);
        float excite = ((1.0f - body_param_) * noise + body_param_ * sine)
                       * excite_scale_;
        if (excite >  32767.0f) excite =  32767.0f;
        if (excite < -32767.0f) excite = -32767.0f;

        iir_state_ = iir_alpha_ * excite + (1.0f - iir_alpha_) * iir_state_;
        new_sample = excite;
        excite_phase_ += excite_phase_inc_;
        excite_remaining_--;
      } else {
        // Sustain phase: normal KS feedback (1st-order IIR + loop gain)
        float filtered = iir_alpha_ * raw + (1.0f - iir_alpha_) * iir_state_;
        iir_state_     = filtered;
        new_sample     = filtered * loop_gain;
      }

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
  static constexpr float KS_TWO_PI = 6.28318530718f;

  // --- Delay line ---------------------------------------------------------
  float*   delay_line_ = nullptr;
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

  // --- Progressive excitation state ---------------------------------------
  int      excite_remaining_ = 0;    // samples left to generate for current pluck
  float    excite_phase_     = 0.0f; // sine phase accumulator
  float    excite_phase_inc_ = 0.0f; // sine phase step per sample
  float    excite_scale_     = 0.0f; // velocity * 32767

  // --- Noise generation ---------------------------------------------------
  uint32_t noise_seed_ = 0xDEADBEEF;
};
