// Control modules for the control matrix: LFO, envelope follower,
// sample & hold, clock and AD envelope. They run once per block (24 samples,
// 0.5 ms). LFO, sample & hold, clock and AD are the modulation boxes the
// player adds; each has a trigger input (Reset, Trigger, Reset, Trigger).

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "granular_looper.h"

namespace gr {

class Lfo {
 public:
  // shape: 0 sine, 1 triangle, 2 square, 3 ramp. Output -1 .. +1.
  float Process(float hz, int shape, size_t n) {
    phase_ += hz * n / kSampleRate;
    phase_ -= std::floor(phase_);
    switch (shape) {
      case 1: out_ = 1.0f - 4.0f * std::fabs(phase_ - 0.5f); break;
      case 2: out_ = phase_ < 0.5f ? 1.0f : -1.0f; break;
      case 3: out_ = 2.0f * phase_ - 1.0f; break;
      default: out_ = std::sin(6.2831853f * phase_); break;
    }
    return out_;
  }
  float out() const { return out_; }
  void Reset() { phase_ = 0.0f; }

 private:
  float phase_ = 0.0f;
  float out_ = 0.0f;
};

class EnvelopeFollower {
 public:
  // Output 0 .. 1. Attack 3 ms, release as given.
  float Process(const float* x, size_t n, float release_ms, float gain) {
    float att = 1.0f - std::exp(-1.0f / (0.003f * kSampleRate));
    float rel = 1.0f - std::exp(-1.0f / (release_ms * 0.001f * kSampleRate));
    for (size_t i = 0; i < n; ++i) {
      float a = std::fabs(x[i]) * gain;
      env_ += (a - env_) * (a > env_ ? att : rel);
    }
    out_ = env_ > 1.0f ? 1.0f : env_;
    return out_;
  }
  float out() const { return out_; }

 private:
  float env_ = 0.0f;
  float out_ = 0.0f;
};

class SampleHold {
 public:
  void Seed(uint32_t seed) { rng_ = seed ? seed : 1; }
  void Trigger() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    target_ = (rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
  }
  // `internal_clock`: true when nothing is patched into its trigger input.
  float Process(size_t n, float hz, float slew_ms, bool internal_clock) {
    if (internal_clock) {
      phase_ += hz * n / kSampleRate;
      if (phase_ >= 1.0f) { phase_ -= std::floor(phase_); Trigger(); }
    }
    if (slew_ms < 0.5f) {
      out_ = target_;
    } else {
      float k = 1.0f - std::exp(-static_cast<float>(n) / (slew_ms * 0.001f * kSampleRate));
      out_ += (target_ - out_) * k;
    }
    return out_;
  }
  float out() const { return out_; }

 private:
  uint32_t rng_ = 0x2545f491u;
  float phase_ = 0.0f;
  float target_ = 0.0f;
  float out_ = 0.0f;
};

// The master clock: one beat position for the whole instrument (in the
// plugin it follows the host). Clock boxes are divisions of it.
class Clock {
 public:
  static constexpr double kWrap = 4096.0;  // beats; a multiple of every division
  // Restart from beat zero; a tick on the next block.
  void Reset() { beats_ = 0.0; pending_ = true; }
  // Advances one block and gives the beat position before and after it, unwrapped.
  void Advance(size_t n, float bpm, double* before, double* after) {
    *before = beats_;
    beats_ += static_cast<double>(bpm) / 60.0 * n / kSampleRate;
    *after = beats_;
    if (beats_ >= kWrap) beats_ -= kWrap;
  }
  bool TakePending() { bool p = pending_; pending_ = false; return p; }
  double beats() const { return beats_; }

 private:
  double beats_ = 0.0;
  bool pending_ = false;
};

// True when a block from `before` to `after` crosses a line of the grid that
// starts at `anchor`, repeats every `division` beats and is shifted by
// `shift` (0 .. 1 of a division).
inline bool GridCrossed(double before, double after, double anchor, double division, double shift) {
  return std::floor((before - anchor) / division - shift) != std::floor((after - anchor) / division - shift);
}

// Attack-decay envelope, 0 .. 1: a trigger restarts the attack from where the
// output is, so retriggers never jump. Linear attack, exponential decay
// (down to 1/1000 in the decay time).
class AdEnvelope {
 public:
  void Trigger() { attacking_ = true; }
  float Process(size_t n, float attack_ms, float decay_ms) {
    float t = static_cast<float>(n) / (0.001f * kSampleRate);  // block length in ms
    if (attacking_) {
      out_ += t / attack_ms;
      if (out_ >= 1.0f) { out_ = 1.0f; attacking_ = false; }
    } else {
      out_ *= std::exp(-6.9078f * t / decay_ms);
      if (out_ < 1e-5f) out_ = 0.0f;
    }
    return out_;
  }
  float out() const { return out_; }

 private:
  bool attacking_ = false;
  float out_ = 0.0f;
};

}  // namespace gr
