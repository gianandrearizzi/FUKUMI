// Resonator: a thin wrapper around the Rings DSP (MIT licence, Emilie Gillet).
// Processes blocks of up to rings::kMaxBlockSize (24) samples at 48 kHz.

#pragma once

#include <cstddef>
#include <cstdint>

#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"

namespace gr {

constexpr size_t kBlockSize = rings::kMaxBlockSize;

struct ResonatorParams {
  float dry_wet = 0.5f;     // 0 .. 1
  float frequency = 0.55f;  // 0 .. 1 -> 5 octaves from C0, quantised
  float structure = 0.5f;   // 0 .. 1
  float brightness = 0.5f;  // 0 .. 1
  float damping = 0.5f;     // 0 .. 1
  float position = 0.5f;    // 0 .. 1
  int model = 0;            // 0 .. 5, see the spec
  int polyphony = 1;        // 1, 2 or 4
  float note = 0.0f;        // semitones added by MIDI or pitch tracking
  bool full_resolution = true;  // 60 modes per voice even in polyphony
  bool internal_exciter = false;  // Rings' own pluck/burst, when no audio arrives
};

class Resonator {
 public:
  void Init();
  // `exciter`: mono audio into the resonator. `dry_l/r`: what dry/wet blends
  // against. `strum`: a trigger for this block.
  void Process(const float* exciter, const float* dry_l, const float* dry_r,
               bool strum, float* out_l, float* out_r, size_t n,
               const ResonatorParams& p);

 private:
  rings::Part part_;
  rings::Strummer strummer_;
  rings::PerformanceState state_;
  rings::Patch patch_;
  uint16_t reverb_buffer_[32768];
  float in_[kBlockSize];
  float odd_[kBlockSize];
  float even_[kBlockSize];
  // Model, polyphony and resolution change under a 5 ms fade, so they never click.
  int model_ = 0, polyphony_ = 1;
  bool full_resolution_ = true;
  float fade_ = 1.0f;
  int fade_direction_ = 0;
};

}  // namespace gr
