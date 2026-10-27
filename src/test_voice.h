// Synthetic singing voice used as a test input (no microphone needed).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "granular_looper.h"

namespace gr {

struct Biquad {
  float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void SetBandpass(float f, float bw) {
    float w = 2.0f * 3.14159265f * f / kSampleRate;
    float q = f / bw;
    float alpha = std::sin(w) / (2.0f * q);
    float a0 = 1.0f + alpha;
    b0 = alpha / a0; b1 = 0.0f; b2 = -alpha / a0;
    a1 = -2.0f * std::cos(w) / a0; a2 = (1.0f - alpha) / a0;
  }
  float Process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

struct Syllable { int midi; char vowel; };

inline std::vector<float> SynthVoice(const std::vector<Syllable>& syl, float dur, uint32_t seed) {
  struct F { float f1, f2, f3; };
  auto formants = [](char v) -> F {
    switch (v) {
      case 'a': return {800, 1200, 2500};
      case 'e': return {500, 1800, 2500};
      case 'i': return {300, 2300, 3000};
      case 'o': return {500, 900, 2400};
      default:  return {320, 800, 2400};
    }
  };
  size_t per = static_cast<size_t>(dur * kSampleRate);
  std::vector<float> out(per * syl.size(), 0.0f);
  Biquad bp[3];
  float phase = 0.0f, lp = 0.0f, f1 = 500, f2 = 1500, f3 = 2500;
  uint32_t rng = seed;
  for (size_t s = 0; s < syl.size(); ++s) {
    F target = formants(syl[s].vowel);
    float f0 = 440.0f * std::exp2((syl[s].midi - 69) / 12.0f);
    for (size_t i = 0; i < per; ++i) {
      float t = i / kSampleRate;
      float env = std::min(1.0f, t / 0.04f) * std::min(1.0f, std::max(0.0f, (dur - 0.08f - t) / 0.1f));
      float vib = t > 0.15f ? 0.004f * std::sin(2 * 3.14159265f * 5.5f * t) : 0.0f;
      phase += f0 * (1.0f + vib) / kSampleRate;
      if (phase >= 1.0f) phase -= 1.0f;
      float src = 2.0f * phase - 1.0f;
      lp += 0.25f * (src - lp);  // spectral tilt of the glottal source
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      float noise = (rng / 4294967296.0f) * 2.0f - 1.0f;
      float exc = lp + 0.04f * noise;
      f1 += 0.002f * (target.f1 - f1);
      f2 += 0.002f * (target.f2 - f2);
      f3 += 0.002f * (target.f3 - f3);
      if ((i & 31) == 0) {
        bp[0].SetBandpass(f1, 80); bp[1].SetBandpass(f2, 100); bp[2].SetBandpass(f3, 140);
      }
      float y = bp[0].Process(exc) + 0.6f * bp[1].Process(exc) + 0.3f * bp[2].Process(exc);
      out[s * per + i] = y * env;
    }
  }
  float peak = 0.0f;
  for (float v : out) peak = std::max(peak, std::fabs(v));
  for (float& v : out) v *= 0.5f / peak;  // -6 dBFS, like a healthy mic level
  return out;
}


// Phrase 1 (4 s) + 1 s rest + phrase 2 (2 s) + 1 s rest, for looping.
inline std::vector<float> TestVoiceLoop() {
  std::vector<float> a = SynthVoice({{57,'a'},{60,'o'},{64,'e'},{62,'i'},{60,'o'},{57,'a'},{55,'u'},{57,'a'}}, 0.5f, 7);
  std::vector<float> b = SynthVoice({{64,'i'},{67,'a'},{69,'e'},{71,'o'}}, 0.5f, 11);
  std::vector<float> out(a);
  out.resize(out.size() + static_cast<size_t>(kSampleRate), 0.0f);
  out.insert(out.end(), b.begin(), b.end());
  out.resize(out.size() + static_cast<size_t>(kSampleRate), 0.0f);
  return out;
}

}  // namespace gr
