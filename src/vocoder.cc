#include "vocoder.h"

#include <algorithm>
#include <cmath>

#include "rings/dsp/dsp.h"
#include "rings/resources.h"
#include "stmlib/dsp/dsp.h"

namespace gr {

using stmlib::Svf;

namespace {
constexpr float kSampleRate = 48000.0f;  // as rings::kSampleRate
constexpr int kBands = 24;           // classic layout
constexpr float kLowHz = 100.0f, kHighHz = 8000.0f;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
}  // namespace

void Vocoder::Init(uint32_t seed) {
  for (int i = 0; i < kMaxBands; ++i) {
    analysis_[i].Init();
    synthesis_[i].Init();
    env_[i] = 0.0f;
    amplitude_[i] = 1.0f;
  }
  rng_ = seed ? seed : 1;
}

void Vocoder::Configure(const VocoderParams& p) {
  float shift = std::exp2(-Clamp(p.formant, -24.0f, 24.0f) / 12.0f);
  float damping = Clamp(p.damping, 0.0f, 1.0f);

  if (p.layout == 0) {
    // Fixed log-spaced bands; damping sets how much each band rings.
    num_bands_ = kBands;
    float q = 3.0f + 60.0f * damping * damping;
    for (int i = 0; i < kBands; ++i) {
      float hz = kLowHz * std::pow(kHighHz / kLowHz, i / float(kBands - 1));
      float f = hz / kSampleRate;
      synthesis_[i].set_f_q<stmlib::FREQUENCY_FAST>(f, q);
      analysis_[i].set_f_q<stmlib::FREQUENCY_FAST>(Clamp(f * shift, 0.0005f, 0.45f), 6.0f);
      amplitude_[i] = 1.0f;
    }
    makeup_ = 2.2f;
    return;
  }

  // The resonator's body: partials as in Rings' modal model.
  float structure = Clamp(p.structure, 0.0f, 0.9995f);
  float semitones = 12.0f + std::round(60.0f * Clamp(p.frequency, 0.0f, 1.0f)) + p.note;
  float frequency = 440.0f * std::exp2((semitones - 69.0f) / 12.0f) / kSampleRate;
  float stiffness = stmlib::Interpolate(rings::lut_stiffness, structure, 256.0f);
  float q = 500.0f * stmlib::Interpolate(rings::lut_4_decades, damping, 256.0f);
  float brightness = Clamp(p.brightness, 0.0f, 1.0f);
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  float q_loss_damping_rate = structure * (2.0f - structure) * 0.1f;
  float harmonic = frequency, stretch = 1.0f;
  float position = Clamp(p.position, 0.0f, 1.0f);
  int n = 0;
  for (int i = 0; i < kMaxBands; ++i) {
    float f = harmonic * stretch;
    if (f >= 0.45f) break;
    synthesis_[i].set_f_q<stmlib::FREQUENCY_FAST>(f, 1.0f + f * q);
    // Each analysis band spans roughly the gap to the next partial.
    float qa = Clamp(static_cast<float>(i + 1), 2.0f, 12.0f);
    analysis_[i].set_f_q<stmlib::FREQUENCY_FAST>(Clamp(f * shift, 0.0005f, 0.45f), qa);
    amplitude_[i] = std::cos(6.2831853f * position * i);
    n = i + 1;
    stretch += stiffness;
    stiffness *= stiffness < 0.0f ? 0.93f : 0.98f;
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    harmonic += frequency;
    q *= q_loss;
  }
  num_bands_ = n;
  makeup_ = 0.5f;
}

void Vocoder::Process(const float* modulator, const float* carrier,
                      float* out_l, float* out_r, size_t n,
                      const VocoderParams& p) {
  if (p.layout != layout_) {
    // A new bank starts from silence, so switching does not click.
    for (int i = 0; i < kMaxBands; ++i) { analysis_[i].Init(); synthesis_[i].Init(); env_[i] = 0.0f; }
    layout_ = p.layout;
  }
  Configure(p);
  attack_ = 1.0f - std::exp(-1.0f / (0.002f * kSampleRate));
  release_ = 1.0f - std::exp(-1.0f / (Clamp(p.release_ms, 2.0f, 2000.0f) * 0.001f * kSampleRate));
  float noise = Clamp(p.noise, 0.0f, 1.0f) * 0.3f;
  bool body = layout_ == 1;
  float in_scale = body ? 0.125f : 1.0f;  // Rings feeds its modes at 1/8

  for (size_t s = 0; s < n; ++s) {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    float white = (rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
    float m = modulator[s];
    float c = (carrier[s] + noise * white) * in_scale;
    float odd = 0.0f, even = 0.0f;
    for (int i = 0; i < num_bands_; ++i) {
      float a = std::fabs(analysis_[i].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(m));
      env_[i] += (a - env_[i]) * (a > env_[i] ? attack_ : release_);
      float y = body ? synthesis_[i].Process<stmlib::FILTER_MODE_BAND_PASS>(c)
                     : synthesis_[i].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(c);
      float v = y * env_[i] * amplitude_[i];
      if (i & 1) even += v; else odd += v;
    }
    out_l[s] = odd * makeup_ * 8.0f;
    out_r[s] = even * makeup_ * 8.0f;
  }
}

}  // namespace gr
