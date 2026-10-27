#include "delay.h"

#include <algorithm>
#include <cmath>

namespace gr {

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kModeStep = 1.0f / 240.0f;  // a change of mode blends over 5 ms
constexpr float kGlide = 0.0007f;           // delay time follows in about 30 ms...
constexpr float kMaxRate = 0.5f;            // ...never faster than half a sample per sample
                                            // (echoes bend by at most +-50% in pitch)...
constexpr float kMaxAccel = 0.0005f;        // ...and the speed itself changes smoothly
constexpr float kHpOpenHz = 21.0f, kLpOpenHz = 19000.0f;

inline float Clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Saturation in the loop: linear up to 0.8, then a soft knee towards 1, so
// feedback above 100% swells and stays bounded instead of exploding.
inline float Saturate(float x) {
  float a = std::fabs(x);
  if (a <= 0.8f) return x;
  float y = 0.8f + 0.2f * std::tanh((a - 0.8f) * 5.0f);
  return x < 0.0f ? -y : y;
}

inline float Cutoff(float hz) { return Clamp(hz / kSampleRate, 0.0002f, 0.45f); }

}  // namespace

void Delay::Init(float* memory) {
  line_[0] = memory;
  line_[1] = memory + kLineSize;
  std::fill(memory, memory + 2 * kLineSize, 0.0f);
  write_ = 0;
  delay_ = 0.3f * kSampleRate;
  rate_ = 0.0f;
  fresh_ = true;
  hp_in_.Init();
  lp_in_.Init();
  for (int i = 0; i < 2; ++i) { hp_fb_[i].Init(); lp_fb_[i].Init(); }
  ping_pong_ = 0.0f;
}

float Delay::FeedbackFilter(int i, float x) {
  if (hp_fb_on_) x = hp_fb_[i].Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
  if (lp_fb_on_) x = lp_fb_[i].Process<stmlib::FILTER_MODE_LOW_PASS>(x);
  return x;
}

float Delay::Read(const float* line) const {
  // Whole and fractional samples kept apart: a float position near the end of
  // the buffer could otherwise round up past it.
  size_t whole = static_cast<size_t>(delay_);
  float frac = delay_ - static_cast<float>(whole);
  size_t newer = write_ >= whole ? write_ - whole : write_ + kLineSize - whole;
  size_t older = newer == 0 ? kLineSize - 1 : newer - 1;
  return line[newer] + (line[older] - line[newer]) * frac;
}

void Delay::Process(float* l, float* r, size_t n, const DelayParams& p) {
  using stmlib::FILTER_MODE_HIGH_PASS;
  using stmlib::FILTER_MODE_LOW_PASS;
  constexpr float kQ = 0.7071f;
  hp_in_on_ = p.hp_in_hz > kHpOpenHz;
  lp_in_on_ = p.lp_in_hz < kLpOpenHz;
  hp_fb_on_ = p.hp_fb_hz > kHpOpenHz;
  lp_fb_on_ = p.lp_fb_hz < kLpOpenHz;
  hp_in_.set_f_q<stmlib::FREQUENCY_ACCURATE>(Cutoff(p.hp_in_hz), kQ);
  lp_in_.set_f_q<stmlib::FREQUENCY_ACCURATE>(Cutoff(p.lp_in_hz), kQ);
  for (int i = 0; i < 2; ++i) {
    hp_fb_[i].set_f_q<stmlib::FREQUENCY_ACCURATE>(Cutoff(p.hp_fb_hz), kQ);
    lp_fb_[i].set_f_q<stmlib::FREQUENCY_ACCURATE>(Cutoff(p.lp_fb_hz), kQ);
  }

  float target = Clamp(p.time_ms, 10.0f, 2000.0f) * 0.001f * kSampleRate;
  if (fresh_) { delay_ = target; fresh_ = false; }  // the first block starts at the set time
  float mode_target = p.ping_pong ? 1.0f : 0.0f;
  float feedback = Clamp(p.feedback, 0.0f, 1.1f);
  float mix = Clamp(p.mix, 0.0f, 1.0f);
  float angle = Clamp(p.pan, 0.0f, 1.0f) * 1.5707963f;
  float pan_l = 1.4142136f * std::cos(angle), pan_r = 1.4142136f * std::sin(angle);  // centre: both 1

  for (size_t i = 0; i < n; ++i) {
    float wanted = Clamp((target - delay_) * kGlide, -kMaxRate, kMaxRate);
    rate_ += Clamp(wanted - rate_, -kMaxAccel, kMaxAccel);
    delay_ += rate_;
    ping_pong_ += Clamp(mode_target - ping_pong_, -kModeStep, kModeStep);
    float m = ping_pong_;  // 0: Pan, 1: Ping-pong, in between while switching

    float in = 0.5f * (l[i] + r[i]);
    if (hp_in_on_) in = hp_in_.Process<FILTER_MODE_HIGH_PASS>(in);
    if (lp_in_on_) in = lp_in_.Process<FILTER_MODE_LOW_PASS>(in);
    float a = Read(line_[0]);
    float b = Read(line_[1]);
    // Pan: the first line feeds back on itself. Ping-pong: it takes the second
    // line's echoes back, and the second line takes the first one's.
    float back_a = FeedbackFilter(0, a + (b - a) * m);
    float back_b = m > 0.0f ? FeedbackFilter(1, a) : 0.0f;
    line_[0][write_] = in + Saturate(feedback * back_a);
    line_[1][write_] = m * Saturate(feedback * back_b);
    write_ = write_ + 1 == kLineSize ? 0 : write_ + 1;

    float out_l = a * (pan_l + (1.0f - pan_l) * m);
    float out_r = a * pan_r * (1.0f - m) + b * m;
    l[i] += out_l * mix;
    r[i] += out_r * mix;
  }
}

}  // namespace gr
