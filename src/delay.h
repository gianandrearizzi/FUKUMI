// Delay bound to a looper: it echoes the looper's output and is part of the
// looper, not a module of its own. The looper's L+R goes in; the echoes are
// added to the dry signal. Two lines: in Pan mode the first line feeds back
// on itself and its echoes are panned; in Ping-pong mode the second line takes
// the first one's output and feeds it back, so the echoes alternate left and
// right. High- and low-pass filters sit on the way in and in the feedback path.

#pragma once

#include <cstddef>

#include "stmlib/dsp/filter.h"

namespace gr {

struct DelayParams {
  float time_ms = 300.0f;      // 10 .. 2000
  float feedback = 0.4f;       // 0 .. 1.1, saturated inside the loop
  float mix = 0.0f;            // echo level added to the dry signal, 0 .. 1
  float pan = 0.5f;            // 0 left .. 1 right, Pan mode only
  bool ping_pong = false;
  float hp_in_hz = 20.0f;      // filters on the way in
  float lp_in_hz = 20000.0f;
  float hp_fb_hz = 20.0f;      // filters in the feedback path: each repeat
  float lp_fb_hz = 20000.0f;   // goes through them once more
};

class Delay {
 public:
  static constexpr size_t kLineSize = 100800 + 8;  // 2.1 s at 48 kHz
  // `memory`: 2 * kLineSize floats.
  void Init(float* memory);
  // Adds the echoes of l/r to l/r, in place.
  void Process(float* l, float* r, size_t n, const DelayParams& p);

 private:
  float Read(const float* line) const;
  float FeedbackFilter(int i, float x);

  float* line_[2] = {nullptr, nullptr};
  size_t write_ = 0;
  float delay_ = 14400.0f;  // in samples, glides towards the set time
  stmlib::Svf hp_in_, lp_in_, hp_fb_[2], lp_fb_[2];
  // A filter at the end of its range is left out, so "open" means untouched.
  bool hp_in_on_ = false, lp_in_on_ = false, hp_fb_on_ = false, lp_fb_on_ = false;
  float rate_ = 0.0f;       // samples of delay change per sample
  bool fresh_ = true;
  float ping_pong_ = 0.0f;  // 0 Pan .. 1 Ping-pong; moves in 5 ms when the mode changes
};

}  // namespace gr
