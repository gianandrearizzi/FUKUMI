// Granular looper: record / punch-in / overdub / replace-all with gesture
// detection on one button, plus a grain engine reading the loop buffer.
// With no loop the grains read the live input instead: the last seconds of it,
// kept in a ring (the undo memory, free while there is no loop).
//
// Portable C++ (no OS, no allocation in Process). Shared by the iPad app and
// the Daisy firmware.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gr {

constexpr float kSampleRate = 48000.0f;
constexpr int kMaxGrains = 32;
constexpr float kLiveWindowSeconds = 8.0f;  // how far back live grains reach

// Parameters in physical units. The UI / MIDI layer maps 0..1 knob values
// to these ranges (see params.h).
struct LooperParams {
  float tune_semitones = 0.0f;  // -12 .. +12, whole or continuous (see params.h)
  float tune_octaves = 0.0f;    // -3 .. +3, rounded to whole octaves
  float start = 0.0f;           // 0 .. 1 of the loop
  float end = 1.0f;             // 0 .. 1; end < start reads backwards
  float position = 0.0f;        // 0 .. 1 offset of the playhead in the region
  float speed = 1.0f;           // -2 .. +2, 0 = freeze
  float grains = 4.0f;          // 1 .. 32 overlapping grains
  float spread = 0.0f;          // 0 .. 1 random position spread
  float attack_curve = 0.0f;    // -1 exponential .. 0 linear .. +1 logarithmic
  float decay_curve = 0.0f;     // same scale
  float size_ms = 100.0f;       // 5 .. 2000
  // Menu-level settings.
  float attack_ratio = 0.5f;    // share of the grain used by the attack
  float stereo_spread = 0.5f;   // how much spread also pans grains
  float feedback = 1.0f;        // old material kept while overdubbing
};

enum class LooperState {
  kEmpty,
  kRecordingFirst,
  kPlaying,
  kPunch,       // replace from playhead (single tap)
  kReplaceAll,  // replace whole buffer (double tap)
  kOverdub,     // long press, while held
};

// Grain-triggered excitation of the resonator.
struct ExciteParams {
  int every = 0;             // 0 = off, else every N-th grain
  float probability = 1.0f;  // 0 .. 1
  bool gate = false;         // true: only triggering grains feed the exciter bus
};

struct GrainView {
  float position;  // 0 .. 1 in the loop
  float envelope;  // 0 .. 1
  bool excites;
};

class GranularLooper {
 public:
  // `buffer` and `undo` must each hold `capacity` floats. `scratch` holds
  // `scratch_capacity` floats (>= 0.5 s) for the double-tap conversion.
  void Init(float* buffer, float* undo, size_t capacity,
            float* scratch, size_t scratch_capacity, uint32_t seed);

  // Call on every edge of the rec button (or MIDI CC 25: 127 / 0).
  void SetRecButton(bool pressed);
  void Undo();
  void Clear();
  // A saved loop: write its samples into the buffer (writable_data()), then
  // call Restore with their count. The looper plays it from the top.
  float* writable_data() { return buffer_; }
  size_t capacity() const { return capacity_; }
  void Restore(size_t length);

  // Processes `n` samples. `exciter` receives a mono bus with the grains
  // selected for excitation (gate mode) or the whole mono mix otherwise.
  // Returns the number of excitation triggers fired in this block.
  int Process(const float* in, float* out_l, float* out_r, float* exciter,
              size_t n, const LooperParams& p, const ExciteParams& e);

  LooperState state() const { return state_; }
  size_t length() const { return length_; }
  float playhead() const;  // 0 .. 1 in the loop
  const float* data() const { return buffer_; }
  // Samples recorded so far while a loop is being (re)defined, else length.
  size_t recorded() const {
    return state_ == LooperState::kRecordingFirst || state_ == LooperState::kReplaceAll
               ? write_pos_ : length_;
  }
  uint32_t version() const { return version_; }  // bumps when audio is written
  int GetGrains(GrainView* out, int max) const;
  // No loop yet (empty or recording the first one): grains read the live input.
  bool live() const { return state_ == LooperState::kEmpty || state_ == LooperState::kRecordingFirst; }
  // Live input: the window the grains read, and its overview for a display.
  size_t live_window() const;
  void LiveOverview(float* out, int bins) const;

 private:
  struct Grain {
    bool active;
    bool excites;
    double pos;      // read position in samples
    double inc;      // samples per sample, signed
    double lo, len;  // region the grain wraps inside
    bool live;       // reads the live ring, not the loop
    float age, dur;
    float attack_share;
    float r_attack, r_decay;
    float gain_l, gain_r;
  };

  // Gesture handling.
  void OnPress();
  void OnRelease();
  void UpdateGestureTimers();
  void BeginPass(size_t write_pos);
  void ConvertPunchToOverdub();
  void ConvertPunchToReplaceAll();
  void ClosePass(bool redefine_length);
  void WriteSample(float x);
  void WriteTail(float x);

  // Grain engine.
  void SpawnGrain(const LooperParams& p, const ExciteParams& e,
                  double region_lo, double region_len, bool reverse);
  float ReadInterpolated(double pos, double lo, double len) const;
  float ReadLive(double pos) const;
  // The loop as it sounds: during a replace-all the part already rewritten is
  // read from the undo copy, so grains keep playing the old loop until it closes.
  float LoopAt(size_t i) const {
    return state_ == LooperState::kReplaceAll && i < write_pos_ ? undo_[i] : buffer_[i];
  }
  float Random();  // 0 .. 1

  float* buffer_ = nullptr;
  float* undo_ = nullptr;
  float* scratch_ = nullptr;
  size_t capacity_ = 0;
  size_t scratch_capacity_ = 0;
  size_t length_ = 0;

  LooperState state_ = LooperState::kEmpty;
  bool pressed_ = false;
  bool press_pending_ = false;
  bool release_pending_ = false;
  bool long_press_possible_ = false;
  uint64_t clock_ = 0;
  uint64_t press_time_ = 0;
  uint64_t first_tap_time_ = 0;

  // Current write pass (also the undo record).
  size_t write_pos_ = 0;
  size_t pass_start_ = 0;
  size_t pass_count_ = 0;     // distinct samples touched (undo range)
  size_t pass_written_ = 0;   // samples written, may wrap the loop
  size_t pass_modulo_ = 1;    // length used to wrap the write head
  size_t length_before_pass_ = 0;
  size_t tail_remaining_ = 0;  // fade-out after a punch
  float overdub_feedback_ = 1.0f;
  bool undo_valid_ = false;

  double playhead_ = 0.0;        // 0 .. 1 in the region
  double playhead_index_ = 0.0;  // in samples, in the loop
  double spawn_timer_ = 0.0;
  uint32_t grain_counter_ = 0;
  uint32_t version_ = 0;
  uint32_t rng_ = 1;
  Grain grains_[kMaxGrains];
  size_t live_write_ = 0;   // next write in the live ring
  size_t live_filled_ = 0;  // samples of live input held
};

}  // namespace gr
