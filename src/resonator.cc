#include "resonator.h"

#include <algorithm>
#include <cmath>

namespace rings {
bool full_resolution = true;
}  // namespace rings

namespace gr {

namespace {
constexpr float kFadeStep = 1.0f / 240.0f;  // 5 ms at 48 kHz
inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
}  // namespace

void Resonator::Init() {
  std::fill(reverb_buffer_, reverb_buffer_ + 32768, 0);
  part_.Init(reverb_buffer_);
  strummer_.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
  state_ = rings::PerformanceState();
  patch_ = rings::Patch();
  model_ = 0;
  polyphony_ = 1;
  fade_ = 1.0f;
  fade_direction_ = 0;
}

void Resonator::Process(const float* exciter, const float* dry_l,
                        const float* dry_r, bool strum, float* out_l,
                        float* out_r, size_t n, const ResonatorParams& p) {
  n = std::min(n, kBlockSize);

  int model = std::max(0, std::min(p.model, int(rings::RESONATOR_MODEL_LAST) - 1));
  int poly = p.polyphony >= 4 ? 4 : (p.polyphony >= 2 ? 2 : 1);
  bool changed = model != model_ || poly != polyphony_ || p.full_resolution != full_resolution_;
  if (changed && fade_direction_ == 0) fade_direction_ = -1;
  if (fade_direction_ == -1 && fade_ <= 0.0f) {
    // Silent: apply the new settings, then fade back in.
    model_ = model;
    polyphony_ = poly;
    full_resolution_ = p.full_resolution;
    rings::full_resolution = full_resolution_;
    part_.set_model(static_cast<rings::ResonatorModel>(model_));
    part_.set_polyphony(polyphony_);  // also makes Rings reconfigure its modes
    fade_direction_ = 1;
  }

  patch_.structure = Clamp(p.structure, 0.0f, 0.9995f);
  patch_.brightness = Clamp(p.brightness, 0.0f, 1.0f);
  patch_.damping = Clamp(p.damping, 0.0f, 1.0f);
  patch_.position = Clamp(p.position, 0.0f, 1.0f);

  // Strums come from our own sources; with no audio routed in, Rings plucks itself.
  state_.internal_exciter = p.internal_exciter;
  state_.internal_strum = false;
  state_.internal_note = false;
  state_.strum = strum;
  state_.tonic = 12.0f + std::round(60.0f * Clamp(p.frequency, 0.0f, 1.0f));
  state_.note = p.note;
  state_.fm = 0.0f;
  state_.chord = static_cast<int32_t>(
      std::round(patch_.structure * (rings::kNumChords - 1)));

  std::copy(exciter, exciter + n, in_);
  strummer_.Process(nullptr, n, &state_);
  part_.Process(state_, patch_, in_, odd_, even_, n);

  float wet = Clamp(p.dry_wet, 0.0f, 1.0f), dry = 1.0f - wet;
  for (size_t i = 0; i < n; ++i) {
    float g = wet * fade_;
    out_l[i] = dry * dry_l[i] + g * odd_[i];
    out_r[i] = dry * dry_r[i] + g * even_[i];
    if (fade_direction_) {
      fade_ = Clamp(fade_ + fade_direction_ * kFadeStep, 0.0f, 1.0f);
      if (fade_direction_ == 1 && fade_ >= 1.0f) fade_direction_ = 0;
    }
  }
}

}  // namespace gr
