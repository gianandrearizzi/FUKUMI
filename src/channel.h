// One channel of the instrument: granular looper -> resonator, with the
// grain-triggered excitation between them. Block size: 24 samples.

#pragma once

#include <vector>

#include "granular_looper.h"
#include "resonator.h"

namespace gr {

struct ChannelParams {
  LooperParams looper;
  ResonatorParams resonator;
  ExciteParams excite;
};

class Channel {
 public:
  void Init(size_t loop_seconds, uint32_t seed) {
    size_t capacity = static_cast<size_t>(loop_seconds * kSampleRate);
    buffer_.assign(capacity, 0.0f);
    undo_.assign(capacity, 0.0f);
    scratch_.assign(static_cast<size_t>(kSampleRate), 0.0f);
    looper_.Init(buffer_.data(), undo_.data(), capacity, scratch_.data(),
                 scratch_.size(), seed);
    resonator_.Init();
  }

  // Strum mode: a trigger re-strikes the resonator.
  // Gate mode: only the triggering grains reach the resonator.
  void Process(const float* in, float* out_l, float* out_r, size_t n,
               const ChannelParams& p) {
    int triggers = looper_.Process(in, loop_l_, loop_r_, exciter_, n,
                                   p.looper, p.excite);
    bool strum = triggers > 0 && !p.excite.gate;
    resonator_.Process(exciter_, loop_l_, loop_r_, strum, out_l, out_r, n,
                       p.resonator);
  }

  GranularLooper& looper() { return looper_; }

 private:
  std::vector<float> buffer_, undo_, scratch_;
  GranularLooper looper_;
  Resonator resonator_;
  float loop_l_[kBlockSize], loop_r_[kBlockSize], exciter_[kBlockSize];
};

}  // namespace gr
