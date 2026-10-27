#include "granular_looper.h"

#include <algorithm>
#include <cmath>

namespace gr {

namespace {

constexpr uint64_t kDoubleTapSamples = static_cast<uint64_t>(0.300 * kSampleRate);
constexpr uint64_t kLongPressSamples = static_cast<uint64_t>(0.500 * kSampleRate);
constexpr size_t kMinLoop = static_cast<size_t>(0.050 * kSampleRate);
constexpr double kMinRegion = 0.010 * kSampleRate;
constexpr float kHalfPi = 1.57079632679f;
constexpr size_t kFade = 96;  // 2 ms crossfade at punch edges and loop seam

inline float Clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

inline double Wrap01(double x) {
  x -= std::floor(x);
  return x;
}

}  // namespace

void GranularLooper::Init(float* buffer, float* undo, size_t capacity,
                          float* scratch, size_t scratch_capacity,
                          uint32_t seed) {
  buffer_ = buffer;
  undo_ = undo;
  capacity_ = capacity;
  scratch_ = scratch;
  scratch_capacity_ = scratch_capacity;
  rng_ = seed ? seed : 1;
  std::fill(buffer_, buffer_ + capacity_, 0.0f);
  std::fill(undo_, undo_ + capacity_, 0.0f);
  Clear();
}

void GranularLooper::Restore(size_t length) {
  size_t n = std::min(length, capacity_);
  Clear();
  if (n < kMinLoop) return;
  length_ = n;
  state_ = LooperState::kPlaying;
  ++version_;
}

size_t GranularLooper::live_window() const {
  return std::min(live_filled_, static_cast<size_t>(kLiveWindowSeconds * kSampleRate));
}

void GranularLooper::LiveOverview(float* out, int bins) const {
  size_t w = live_window();
  for (int b = 0; b < bins; ++b) {
    size_t from = w * b / bins, to = w * (b + 1) / bins;
    size_t step = std::max<size_t>(1, (to - from) / 64);
    float peak = 0.0f;
    for (size_t i = from; i < to; i += step) {
      size_t idx = (live_write_ + capacity_ - w + i) % capacity_;
      peak = std::max(peak, std::fabs(undo_[idx]));
    }
    out[b] = std::min(1.0f, peak);
  }
}

void GranularLooper::Clear() {
  state_ = LooperState::kEmpty;
  live_filled_ = 0;  // the live ring starts again from silence
  length_ = 0;
  pressed_ = false;
  long_press_possible_ = false;
  undo_valid_ = false;
  tail_remaining_ = 0;
  write_pos_ = pass_start_ = pass_count_ = pass_written_ = 0;
  playhead_ = 0.0;
  playhead_index_ = 0.0;
  spawn_timer_ = 0.0;
  grain_counter_ = 0;
  ++version_;
  for (Grain& g : grains_) g.active = false;
}

float GranularLooper::Random() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f);
}

float GranularLooper::playhead() const {
  if (live()) {
    size_t w = live_window();
    return w ? static_cast<float>(playhead_index_ / static_cast<double>(w)) : 0.0f;
  }
  return length_ ? static_cast<float>(playhead_index_ / length_) : 0.0f;
}

// --- Gestures ---------------------------------------------------------------

void GranularLooper::SetRecButton(bool pressed) {
  if (pressed == pressed_) return;
  if (pressed) {
    OnPress();
  } else {
    OnRelease();
  }
}

void GranularLooper::OnPress() {
  pressed_ = true;
  press_time_ = clock_;
  switch (state_) {
    case LooperState::kEmpty:
      write_pos_ = 0;
      undo_valid_ = false;
      state_ = LooperState::kRecordingFirst;
      break;
    case LooperState::kRecordingFirst:
    case LooperState::kReplaceAll:
      ClosePass(true);
      break;
    case LooperState::kPlaying:
      // Recording starts at once; the gesture is decided later.
      BeginPass(static_cast<size_t>(playhead_index_) % length_);
      state_ = LooperState::kPunch;
      first_tap_time_ = clock_;
      long_press_possible_ = true;
      break;
    case LooperState::kPunch:
      if (clock_ - first_tap_time_ < kDoubleTapSamples) {
        ConvertPunchToReplaceAll();
      } else {
        ClosePass(false);
      }
      break;
    case LooperState::kOverdub:
      ClosePass(false);
      break;
  }
}

void GranularLooper::OnRelease() {
  pressed_ = false;
  if (state_ == LooperState::kOverdub) {
    ClosePass(false);
  } else if (state_ == LooperState::kPunch) {
    long_press_possible_ = false;
  }
}

void GranularLooper::UpdateGestureTimers() {
  if (state_ == LooperState::kPunch && pressed_ && long_press_possible_ &&
      clock_ - press_time_ >= kLongPressSamples) {
    ConvertPunchToOverdub();
  }
}

void GranularLooper::BeginPass(size_t write_pos) {
  tail_remaining_ = 0;
  length_before_pass_ = length_;
  pass_modulo_ = length_;
  pass_start_ = write_pos;
  pass_count_ = 0;
  pass_written_ = 0;
}

void GranularLooper::ConvertPunchToOverdub() {
  // Everything written so far replaced the original: sum it back in.
  for (size_t k = 0; k < pass_count_; ++k) {
    size_t idx = (pass_start_ + k) % pass_modulo_;
    buffer_[idx] = undo_[idx] * overdub_feedback_ + buffer_[idx];
  }
  state_ = LooperState::kOverdub;
}

void GranularLooper::ConvertPunchToReplaceAll() {
  // Keep what was captured since the first tap, restore the original,
  // then start a new recording from the top that begins with the capture.
  size_t captured = std::min(pass_count_, scratch_capacity_);
  for (size_t k = 0; k < pass_count_; ++k) {
    size_t idx = (pass_start_ + k) % pass_modulo_;
    if (k < captured) scratch_[k] = buffer_[idx];
    buffer_[idx] = undo_[idx];
  }
  length_before_pass_ = length_;
  pass_modulo_ = capacity_;
  pass_start_ = 0;
  pass_count_ = 0;
  pass_written_ = 0;
  write_pos_ = 0;
  state_ = LooperState::kReplaceAll;
  for (size_t k = 0; k < captured; ++k) WriteSample(scratch_[k]);
}

void GranularLooper::ClosePass(bool redefine_length) {
  if (redefine_length) {
    size_t new_length = write_pos_;
    if (new_length < kMinLoop) {
      std::fill(buffer_ + new_length, buffer_ + kMinLoop, 0.0f);
      new_length = kMinLoop;
    }
    length_ = std::min(new_length, capacity_);
    // Fade the seam so the loop wraps without a click.
    for (size_t k = 0; k < kFade && k < length_ / 2; ++k) {
      float g = static_cast<float>(k) / kFade;
      buffer_[k] *= g;
      buffer_[length_ - 1 - k] *= g;
    }
  }
  // A punch fades back into the original over the next 2 ms.
  tail_remaining_ = state_ == LooperState::kPunch ? kFade : 0;
  undo_valid_ = state_ != LooperState::kRecordingFirst;
  state_ = LooperState::kPlaying;
  long_press_possible_ = false;
}

void GranularLooper::Undo() {
  if (state_ != LooperState::kPlaying || !undo_valid_) return;
  // Swapping makes undo its own redo.
  for (size_t k = 0; k < pass_count_; ++k) {
    size_t idx = (pass_start_ + k) % pass_modulo_;
    std::swap(buffer_[idx], undo_[idx]);
  }
  std::swap(length_, length_before_pass_);
  for (Grain& g : grains_) g.active = false;
  ++version_;
}

void GranularLooper::WriteSample(float x) {
  switch (state_) {
    case LooperState::kRecordingFirst:
      buffer_[write_pos_++] = x;
      if (write_pos_ >= capacity_) ClosePass(true);
      break;
    case LooperState::kReplaceAll:
      undo_[write_pos_] = buffer_[write_pos_];
      buffer_[write_pos_++] = x;
      pass_count_ = write_pos_;
      if (write_pos_ >= capacity_) ClosePass(true);
      break;
    case LooperState::kPunch:
    case LooperState::kOverdub: {
      size_t idx = (pass_start_ + pass_written_) % pass_modulo_;
      if (pass_written_ < pass_modulo_) {
        undo_[idx] = buffer_[idx];  // first visit: keep the original
        pass_count_ = pass_written_ + 1;
      }
      if (state_ == LooperState::kPunch) {
        float a = std::min(1.0f, static_cast<float>(pass_written_ + 1) / kFade);
        buffer_[idx] = undo_[idx] * (1.0f - a) + x * a;
      } else {
        buffer_[idx] = buffer_[idx] * overdub_feedback_ + x;
      }
      ++pass_written_;
      break;
    }
    default:
      break;
  }
}

void GranularLooper::WriteTail(float x) {
  size_t idx = (pass_start_ + pass_written_) % pass_modulo_;
  if (pass_written_ < pass_modulo_) {
    undo_[idx] = buffer_[idx];
    pass_count_ = pass_written_ + 1;
  }
  float a = static_cast<float>(tail_remaining_) / kFade;
  buffer_[idx] = buffer_[idx] * (1.0f - a) + x * a;
  ++pass_written_;
  --tail_remaining_;
}

// --- Grains -----------------------------------------------------------------

float GranularLooper::ReadInterpolated(double pos, double lo, double len) const {
  double rel = std::fmod(pos - lo, len);
  if (rel < 0.0) rel += len;
  size_t n = static_cast<size_t>(len);
  size_t base = static_cast<size_t>(lo);
  size_t i1 = static_cast<size_t>(rel);
  float t = static_cast<float>(rel - static_cast<double>(i1));
  size_t i0 = (i1 + n - 1) % n, i2 = (i1 + 1) % n, i3 = (i1 + 2) % n;
  float xm1 = LoopAt(base + i0), x0 = LoopAt(base + i1 % n);
  float x1 = LoopAt(base + i2), x2 = LoopAt(base + i3);
  // 4-point Hermite.
  float c = (x1 - xm1) * 0.5f;
  float v = x0 - x1;
  float w = c + v;
  float a = w + v + (x2 - x0) * 0.5f;
  float b = w + a;
  return (((a * t) - b) * t + c) * t + x0;
}

float GranularLooper::ReadLive(double pos) const {
  double cap = static_cast<double>(capacity_);
  pos = std::fmod(pos, cap);
  if (pos < 0.0) pos += cap;
  size_t i1 = static_cast<size_t>(pos);
  float t = static_cast<float>(pos - static_cast<double>(i1));
  size_t i0 = (i1 + capacity_ - 1) % capacity_, i2 = (i1 + 1) % capacity_, i3 = (i1 + 2) % capacity_;
  float xm1 = undo_[i0], x0 = undo_[i1 % capacity_], x1 = undo_[i2], x2 = undo_[i3];
  float c = (x1 - xm1) * 0.5f;
  float v = x0 - x1;
  float w = c + v;
  float a = w + v + (x2 - x0) * 0.5f;
  float b = w + a;
  return (((a * t) - b) * t + c) * t + x0;
}

void GranularLooper::SpawnGrain(const LooperParams& p, const ExciteParams& e,
                                double region_lo, double region_len,
                                bool reverse) {
  ++grain_counter_;
  Grain* g = nullptr;
  for (Grain& candidate : grains_) {
    if (!candidate.active) { g = &candidate; break; }
  }
  bool excites = e.every > 0 && (grain_counter_ % e.every) == 0 &&
                 Random() < e.probability;
  if (!g) return;

  double frac = playhead_ + p.position +
                p.spread * (Random() * 2.0f - 1.0f) * 0.5f;
  frac = Wrap01(frac);
  if (reverse) frac = 1.0 - frac;

  float semitones = p.tune_semitones + 12.0f * std::round(p.tune_octaves);
  double rate = std::exp2(semitones / 12.0f);

  g->active = true;
  g->excites = excites;
  g->age = 0.0f;
  g->dur = Clamp(p.size_ms, 5.0f, 2000.0f) * 0.001f * kSampleRate;
  g->inc = reverse ? -rate : rate;
  g->live = live();
  if (g->live) {
    // Live input: the region is a stretch of the last seconds. A grain may not
    // overtake the write head, nor read back beyond what has been captured.
    double window = static_cast<double>(live_window());
    double rel = region_lo + frac * region_len;  // 0 = oldest, window = now
    double need_ahead = std::max(0.0, rate - 1.0) * g->dur + 4.0;
    if (!reverse) rel = std::min(rel, window - need_ahead);
    else rel = std::max(rel, std::min(window - 4.0, rate * g->dur + 4.0));
    rel = Clamp(static_cast<float>(rel), 0.0f, static_cast<float>(window - 4.0));
    g->lo = 0.0;
    g->len = static_cast<double>(capacity_);
    g->pos = static_cast<double>(live_write_) - window + rel;
  } else {
    // Grains read across the whole loop: the region only decides where they
    // start, so a grain never jumps at the region edge.
    g->lo = 0.0;
    g->len = static_cast<double>(length_);
    g->pos = region_lo + frac * region_len;
  }
  g->attack_share = Clamp(p.attack_ratio, 0.01f, 0.99f);
  // Rational curve x / (x + (1 - x) r): r > 1 slow start, r < 1 fast start.
  g->r_attack = std::exp2(-4.0f * Clamp(p.attack_curve, -1.0f, 1.0f));
  g->r_decay = std::exp2(4.0f * Clamp(p.decay_curve, -1.0f, 1.0f));
  float width = Clamp(p.spread * p.stereo_spread * 2.0f, 0.0f, 1.0f);
  float pan = 0.5f + 0.5f * width * (Random() * 2.0f - 1.0f);
  g->gain_l = std::cos(pan * kHalfPi);
  g->gain_r = std::sin(pan * kHalfPi);
}

int GranularLooper::Process(const float* in, float* out_l, float* out_r,
                            float* exciter, size_t n, const LooperParams& p,
                            const ExciteParams& e) {
  UpdateGestureTimers();
  overdub_feedback_ = Clamp(p.feedback, 0.0f, 1.0f);
  std::fill(out_l, out_l + n, 0.0f);
  std::fill(out_r, out_r + n, 0.0f);
  std::fill(exciter, exciter + n, 0.0f);

  int triggers = 0;
  if ((state_ != LooperState::kEmpty && state_ != LooperState::kPlaying) || tail_remaining_ > 0) {
    ++version_;
  }
  bool live_input = live();
  if (live_input) {
    // The live ring takes the input while there is no loop.
    for (size_t i = 0; i < n; ++i) {
      undo_[live_write_] = in ? in[i] : 0.0f;
      live_write_ = live_write_ + 1 == capacity_ ? 0 : live_write_ + 1;
    }
    live_filled_ = std::min(live_filled_ + n, capacity_);
    ++version_;
  }
  // What the grains read: the loop, or with no loop the last seconds of input.
  double source_len = live_input ? static_cast<double>(live_window()) : static_cast<double>(length_);
  bool can_play = live_input ? source_len >= 2.0 * kMinRegion
                             : length_ >= kMinLoop && state_ != LooperState::kRecordingFirst;

  double lo = 0.0, len = 1.0;
  bool reverse = false;
  double interval = 1.0;
  float norm = 1.0f;
  float direction = 1.0f;
  if (can_play) {
    float s = Clamp(p.start, 0.0f, 1.0f), en = Clamp(p.end, 0.0f, 1.0f);
    reverse = en < s;
    lo = std::floor(std::min(s, en) * source_len);
    len = std::floor(std::fabs(en - s) * source_len);
    len = std::max(len, kMinRegion);
    if (lo + len > source_len) lo = std::max(0.0, source_len - len);
    len = std::min(len, source_len);
    float grains = Clamp(p.grains, 1.0f, static_cast<float>(kMaxGrains));
    double size = Clamp(p.size_ms, 5.0f, 2000.0f) * 0.001 * kSampleRate;
    interval = size / grains;
    norm = 1.0f / std::sqrt(std::max(1.0f, grains * 0.5f));
    direction = reverse ? -1.0f : 1.0f;
  }

  for (size_t i = 0; i < n; ++i) {
    if (state_ != LooperState::kEmpty && state_ != LooperState::kPlaying) {
      WriteSample(in ? in[i] : 0.0f);
    } else if (tail_remaining_ > 0) {
      WriteTail(in ? in[i] : 0.0f);
    }
    if (!can_play) continue;

    playhead_ = Wrap01(playhead_ + direction * p.speed / len);
    double frac = Wrap01(playhead_ + p.position);
    playhead_index_ = lo + (reverse ? 1.0 - frac : frac) * len;

    spawn_timer_ -= 1.0;
    while (spawn_timer_ <= 0.0) {
      SpawnGrain(p, e, lo, len, reverse);
      spawn_timer_ += interval;
    }

    float l = 0.0f, r = 0.0f, x = 0.0f;
    for (Grain& g : grains_) {
      if (!g.active) continue;
      float t = g.age / g.dur;
      float env;
      if (t < g.attack_share) {
        float a = t / g.attack_share;
        env = a / (a + (1.0f - a) * g.r_attack);
      } else {
        float d = (t - g.attack_share) / (1.0f - g.attack_share);
        env = 1.0f - d / (d + (1.0f - d) * g.r_decay);
      }
      float sample = (g.live ? ReadLive(g.pos) : ReadInterpolated(g.pos, g.lo, g.len)) * env * norm;
      l += sample * g.gain_l;
      r += sample * g.gain_r;
      float mono = sample * 0.5f * (g.gain_l + g.gain_r);
      if (!e.gate || g.excites) x += mono;
      if (g.age == 0.0f && g.excites) ++triggers;
      g.pos += g.inc;
      g.age += 1.0f;
      if (g.age >= g.dur) g.active = false;
    }
    out_l[i] = l;
    out_r[i] = r;
    exciter[i] = x;
  }
  clock_ += n;
  return triggers;
}

int GranularLooper::GetGrains(GrainView* out, int max) const {
  int count = 0;
  double window = static_cast<double>(live_window()), cap = static_cast<double>(capacity_);
  for (const Grain& g : grains_) {
    if (!g.active || count >= max) continue;
    double p;
    if (g.live) {
      // Position inside the live window: 0 the oldest sound, 1 now.
      if (!live() || window < 1.0) continue;
      double rel = std::fmod(g.pos - (static_cast<double>(live_write_) - window), cap);
      if (rel < 0.0) rel += cap;
      if (rel > window) continue;
      p = rel / window;
    } else {
      if (!length_ || live()) continue;
      double q = std::fmod(g.pos, static_cast<double>(length_));
      if (q < 0.0) q += length_;
      p = q / length_;
    }
    float t = g.age / g.dur;
    out[count].position = static_cast<float>(p);
    out[count].envelope = t < g.attack_share ? t / g.attack_share
                                             : 1.0f - (t - g.attack_share) / (1.0f - g.attack_share);
    out[count].excites = g.excites;
    ++count;
  }
  return count;
}

}  // namespace gr
