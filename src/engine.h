// The whole instrument as modules joined by two pin matrices, in the manner
// of the EMS Synthi: an audio matrix (module outputs -> module inputs) and a
// control matrix (control sources -> triggers and parameters).
//
// Order inside each 0.5 ms block: control sources (LFO and clock boxes),
// loopers, triggers, resonators, vocoders, output, envelope followers of
// every audio source, sample & hold and AD boxes. A connection whose
// source is computed at or after its destination arrives one block later.

#pragma once

#include <vector>

#include "delay.h"
#include "granular_looper.h"
#include "modulators.h"
#include "params.h"
#include "resonator.h"
#include "vocoder.h"

namespace gr {

enum AudioSource {
  AS_IN_A, AS_IN_B, AS_LOOPER_A, AS_LOOPER_B, AS_GRAINS_A, AS_GRAINS_B,
  AS_RESO_A, AS_RESO_B, AS_VOC_A, AS_VOC_B, AS_COUNT
};
// Module inputs only: every source reaches the output through the mixer.
enum AudioDest {
  AD_LOOPER_A, AD_LOOPER_B, AD_RESO_A, AD_RESO_B,
  AD_VOC_A_MOD, AD_VOC_A_CAR, AD_VOC_B_MOD, AD_VOC_B_CAR, AD_COUNT
};
// Control sources: the envelope of every audio source (same order), the grain
// triggers and playheads, the master clock, then one row per box slot.
enum ControlSource {
  CS_ENV_FIRST = 0, CS_GRAINS_A = AS_COUNT, CS_GRAINS_B, CS_HEAD_A, CS_HEAD_B,
  CS_CLOCK, CS_BOX_FIRST, CS_COUNT = CS_BOX_FIRST + kMaxBoxes
};
// Control destinations: two strums and the master clock reset, six parameters
// per looper and resonator, two per vocoder, the pan of every audio source,
// then the trigger input of every box slot.
enum ControlDest {
  CD_STRUM_A, CD_STRUM_B, CD_CLOCK_RESET, CD_LOOPER_A, CD_LOOPER_B = CD_LOOPER_A + 6,
  CD_RESO_A = CD_LOOPER_B + 6, CD_RESO_B = CD_RESO_A + 6,
  CD_VOC_A = CD_RESO_B + 6, CD_VOC_B = CD_VOC_A + 2,
  CD_DELAY_A = CD_VOC_B + 2, CD_DELAY_B = CD_DELAY_A + 8,
  CD_PAN_FIRST = CD_DELAY_B + 8,
  CD_BOX_TRIG_FIRST = CD_PAN_FIRST + AS_COUNT, CD_COUNT = CD_BOX_TRIG_FIRST + kMaxBoxes
};
// Trigger inputs, for the fire counters: strum A, strum B, clock reset, then the boxes.
constexpr int kFixedTriggers = 3;
constexpr int kTriggerCount = kFixedTriggers + kMaxBoxes;
constexpr int kVocoderTargets[2] = {VP_FORMANT, VP_NOISE};
constexpr int kLooperTargets[6] = {LP_TUNE, LP_POSITION, LP_SPEED, LP_GRAINS, LP_SPREAD, LP_SIZE};
constexpr int kResoTargets[6] = {RP_FREQUENCY, RP_STRUCTURE, RP_BRIGHTNESS, RP_DAMPING, RP_POSITION, RP_DRY_WET};
// The looper's delay: every continuous parameter. Pan is ignored in Ping-pong mode.
constexpr int kDelayTargets[8] = {LP_DLY_TIME, LP_DLY_FEEDBACK, LP_DLY_MIX, LP_DLY_PAN,
                                  LP_DLY_HP_IN, LP_DLY_LP_IN, LP_DLY_HP_FB, LP_DLY_LP_FB};

// Parameter modules. M_INPUT: index = channel * IP_COUNT + InputParam;
// M_ENV: index = audio source * EP_COUNT + EnvParam;
// M_BOX: index = slot * BP_COUNT + BoxParam; M_MIX: index = audio source * XP_COUNT + MixParam.
enum Module { M_LOOPER_A, M_LOOPER_B, M_RESO_A, M_RESO_B, M_MOD, M_INPUT, M_ENV, M_VOC, M_BOX, M_MIX };

// One modulation box: whichever generator its type selects.
struct Box {
  int type = BOX_NONE;
  float knob[BP_COUNT] = {};
  Lfo lfo;
  SampleHold sh;
  AdEnvelope ad;
  double anchor = 0.0;   // clock box: where its grid starts on the master clock
  bool pending = false;  // clock box: tick on the next block (after a reset)
  float trigger_prev = 0.0f;
};

class Engine {
 public:
  void Init(size_t loop_seconds);
  // in_l / in_r: the host's stereo input.
  void Process(const float* in_l, const float* in_r, float* out_l,
               float* out_r, size_t n);

  void SetParam(int module, int index, float value);
  float param(int module, int index) const;
  void SetAudioPin(int source, int dest, float level);  // 0 .. 1
  void SetControlPin(int source, int dest, float depth);  // -1 .. +1
  // Per-row trim of the control matrix, 0 .. 2: scales a source before its
  // pins; the trimmed signal saturates at -1 .. +1.
  void SetControlTrim(int source, float trim);
  // The REC button of a looper. With sync on, a new gesture waits for the next
  // tick of the master clock and all its events move by the same delay, so a
  // tap, a double tap and a hold read exactly as played, while recordings start
  // and close on the grid.
  void SetRecButton(int ch, bool pressed);
  void SetRecSync(int ch, bool on);
  // A press waiting to happen (a queued release does not count).
  bool rec_armed(int ch) const {
    const RecSync& r = rec_sync_[ch & 1];
    if (r.waiting) return true;
    for (int i = 0; i < r.count; ++i) if (r.queue[i].pressed) return true;
    return false;
  }
  void ClearPins();
  // Puts a box of `type` in `slot` (BOX_NONE removes it). A new box starts
  // from its default knobs with its row and trigger column cleared.
  void SetBoxType(int slot, int type);
  int box_type(int slot) const { return boxes_[slot].type; }

  GranularLooper& looper(int ch) { return looper_[ch & 1]; }
  // For the display: the value each control destination receives now
  // (parameters after modulation, triggers as their summed input).
  const float* control_dest_values() const { return dest_values_; }
  const float* control_source_values() const { return source_values_; }
  // Counters that grow on every pulse (clock, grains) and every trigger
  // fired, so a display can flash even for 0.5 ms events.
  const uint32_t* source_pulses() const { return source_pulses_; }
  const uint32_t* trigger_fires() const { return trigger_fires_; }
  void Overview(int ch, float* out, int bins) const;

 private:
  void ProcessChunk(const float* in_a, const float* in_b, float* out_l,
                    float* out_r, size_t n);
  float ControlSum(int dest);
  void MixAudio(int dest, size_t n, float* mono, float* left, float* right) const;

  std::vector<float> buffer_[2], undo_[2], scratch_[2];
  GranularLooper looper_[2];
  std::vector<float> delay_memory_[2];
  Delay delay_[2];
  Resonator resonator_[2];
  Vocoder vocoder_[2];
  EnvelopeFollower env_[AS_COUNT];
  Box boxes_[kMaxBoxes];
  Clock clock_;
  double beats_now_ = 0.0;  // master clock position at the end of this block
  uint64_t now_ = 0;        // samples since start

  struct RecSync {
    bool enabled = false;
    bool waiting = false;      // a gesture is waiting for the next tick
    bool shift_known = false;
    uint64_t gesture_start = 0, shift = 0, last_press = 0;
    struct Event { bool pressed; uint64_t arrival; } queue[16];
    int count = 0;
  };
  RecSync rec_sync_[2];
  void DeliverRec(int ch);

  float looper_base_[2][LP_COUNT];
  float reso_base_[2][RP_COUNT];
  float voc_base_[2][VP_COUNT];
  float mod_base_[MP_COUNT];
  float input_base_[2][IP_COUNT];
  float env_base_[AS_COUNT][EP_COUNT];
  float mix_base_[AS_COUNT][XP_COUNT];
  float mix_gl_[AS_COUNT] = {}, mix_gr_[AS_COUNT] = {};  // last block's gains, for ramps
  float audio_[AS_COUNT][AD_COUNT] = {};
  float control_[CS_COUNT][CD_COUNT] = {};
  float trim_[CS_COUNT];
  float source_values_[CS_COUNT] = {};
  float dest_values_[CD_COUNT] = {};
  float trigger_prev_[kFixedTriggers] = {};
  uint32_t source_pulses_[CS_COUNT] = {};
  uint32_t trigger_fires_[kTriggerCount] = {};

  // Latest audio of every source (stereo); modules overwrite theirs when they
  // run, so later modules hear them now and earlier ones a block later.
  float src_l_[AS_COUNT][kBlockSize] = {};
  float src_r_[AS_COUNT][kBlockSize] = {};

};

}  // namespace gr
