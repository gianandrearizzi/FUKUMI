// Modal vocoder: a bank of resonators whose amplitudes follow the spectrum
// of a modulator. The bank is either 24 fixed bands (classic vocoder) or the
// partials of the resonator's body, computed like Rings' modal model from the
// same frequency, structure, brightness, damping and position.
//
// Carrier: any audio from the matrix, plus optional internal noise, which
// lets the body ring on its own and the modulator choose its partials.

#pragma once

#include <cstddef>
#include <cstdint>

#include "stmlib/dsp/filter.h"

namespace gr {

struct VocoderParams {
  int layout = 1;             // 0: bands, 1: body (resonator partials)
  float frequency = 0.55f;    // shared with the resonator: 0..1 -> C0 + 60 semitones
  float note = 0.0f;          // extra semitones (MIDI, modulation)
  float structure = 0.25f;    // inharmonicity, as in Rings
  float brightness = 0.5f;
  float damping = 0.5f;       // resonance of the synthesis bank
  float position = 0.5f;      // amplitude pattern over the partials
  float release_ms = 60.0f;   // envelope of each analysis band
  float formant = 0.0f;       // semitones, shifts analysis against synthesis
  float noise = 0.0f;         // internal noise added to the carrier, 0..1
};

class Vocoder {
 public:
  static constexpr int kMaxBands = 48;

  void Init(uint32_t seed);
  // Blocks of up to 24 samples.
  void Process(const float* modulator, const float* carrier, float* out_l,
               float* out_r, size_t n, const VocoderParams& p);
  int num_bands() const { return num_bands_; }

 private:
  void Configure(const VocoderParams& p);

  stmlib::Svf analysis_[kMaxBands];
  stmlib::Svf synthesis_[kMaxBands];
  float env_[kMaxBands];
  float amplitude_[kMaxBands];
  int num_bands_ = 0;
  int layout_ = -1;
  float makeup_ = 1.0f;
  float attack_ = 0.0f, release_ = 0.0f;
  uint32_t rng_ = 1;
};

}  // namespace gr
