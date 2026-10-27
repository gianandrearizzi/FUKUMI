// Parameters as normalised 0..1 values (what a knob, a CC or a matrix pin
// moves) and their mapping to physical units. Shared by app and firmware.

#pragma once

#include <algorithm>
#include <cmath>

#include "delay.h"
#include "granular_looper.h"
#include "resonator.h"
#include "vocoder.h"

namespace gr {

enum LooperParam {
  LP_TUNE, LP_OCTAVE, LP_START, LP_END, LP_POSITION, LP_SPEED, LP_GRAINS,
  LP_SPREAD, LP_ATTACK, LP_DECAY, LP_SIZE, LP_EVERY, LP_PROBABILITY,
  LP_FEEDBACK,
  // The looper's delay (see delay.h): part of the looper, not a module.
  LP_DLY_TIME, LP_DLY_FEEDBACK, LP_DLY_MIX, LP_DLY_PAN, LP_DLY_MODE,
  LP_DLY_HP_IN, LP_DLY_LP_IN, LP_DLY_HP_FB, LP_DLY_LP_FB,
  LP_TUNE_QUANT,  // > 0.5: tune in whole semitones; else continuous
  LP_COUNT
};

enum ResoParam {
  RP_DRY_WET, RP_FREQUENCY, RP_STRUCTURE, RP_BRIGHTNESS, RP_DAMPING,
  RP_POSITION, RP_MODEL, RP_POLYPHONY, RP_COUNT
};

// Global parameters. The master clock is fixed; the other modulators are boxes, see BoxType.
enum ModParam {
  MP_MASTER,
  MP_FULL_RES,   // Rings keeps 60 modes per voice in polyphony
  MP_BPM,        // master clock tempo
  MP_CLOCK_DIV,  // master clock: division of its own pulse output
  MP_COUNT
};

// The output mixer: every audio source reaches the output through its own
// volume and pan, set by the player. Pan is also a control destination.
enum MixParam { XP_VOLUME, XP_PAN, XP_COUNT };
inline float VolumeFromKnob(float v) { return 2.0f * v * v; }  // 0.707 = 0 dB, 1 = +6 dB


// Modulation boxes: every box has two knobs, an output (a row of the control
// matrix) and a trigger input (a column).
enum BoxType { BOX_NONE, BOX_LFO, BOX_SH, BOX_CLOCK, BOX_AD, BOX_TYPE_COUNT };
constexpr int kMaxBoxes = 32;
enum BoxParam { BP_A, BP_B, BP_COUNT };  // LFO: rate, shape; S&H: rate, slew;
                                          // clock: division of the master, shift; AD: attack, decay

// Per channel: the modal vocoder. Its body shares the resonator's parameters.
enum VocoderParam { VP_LAYOUT, VP_RELEASE, VP_FORMANT, VP_NOISE, VP_COUNT };


inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Per channel: which host input feeds it and at what level.
enum InputParam { IP_SOURCE, IP_LEVEL, IP_COUNT };  // source: host left, right, or both
// Per audio source: its envelope follower.
enum EnvParam { EP_RELEASE, EP_GAIN, EP_COUNT };

inline int InputSourceFromKnob(float v) { return static_cast<int>(std::lround(Clamp01(v) * 2.0f)); }
inline float InputLevelFromKnob(float v) { return 2.0f * Clamp01(v); }

inline const float* LooperDefaults() {
  static const float d[LP_COUNT] = {
      0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.75f, 3.0f / 31.0f, 0.15f, 0.5f, 0.5f,
      0.5000f /* size: 100 ms */, 4.0f / 12.0f, 1.0f, 1.0f,
      // delay: about 300 ms, 38% feedback, off, centre, Pan mode,
      // input filters open, feedback filters at 56 Hz and 3.6 kHz
      0.64f, 0.35f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.15f, 0.75f,
      1.0f /* tune in semitones */};
  return d;
}

inline float DelayMsFromKnob(float v) { return 10.0f * std::pow(200.0f, Clamp01(v)); }    // 10 ms .. 2 s
inline float DelayFilterHzFromKnob(float v) { return 20.0f * std::pow(1000.0f, Clamp01(v)); }  // 20 Hz .. 20 kHz

inline void ApplyDelay(const float* n, DelayParams* d) {
  d->time_ms = DelayMsFromKnob(n[LP_DLY_TIME]);
  d->feedback = 1.1f * Clamp01(n[LP_DLY_FEEDBACK]);
  d->mix = Clamp01(n[LP_DLY_MIX]);
  d->pan = Clamp01(n[LP_DLY_PAN]);
  d->ping_pong = n[LP_DLY_MODE] > 0.5f;
  d->hp_in_hz = DelayFilterHzFromKnob(n[LP_DLY_HP_IN]);
  d->lp_in_hz = DelayFilterHzFromKnob(n[LP_DLY_LP_IN]);
  d->hp_fb_hz = DelayFilterHzFromKnob(n[LP_DLY_HP_FB]);
  d->lp_fb_hz = DelayFilterHzFromKnob(n[LP_DLY_LP_FB]);
}

inline const float* ResoDefaults() {
  static const float d[RP_COUNT] = {0.6f, 0.55f, 0.5f, 0.5f, 0.6f, 0.3f, 0.0f, 0.0f};
  return d;
}

inline const float* ModDefaults() {
  static const float d[MP_COUNT] = {0.8f, 1.0f, 0.4f, 3.0f / 7.0f};
  return d;
}

inline const float* BoxDefaults(int type) {
  static const float d[BOX_TYPE_COUNT][BP_COUNT] = {
      {0.0f, 0.0f}, {0.3f, 0.0f}, {0.35f, 0.2f}, {2.0f / 7.0f, 0.0f}, {0.3f, 0.5f}};
  return d[type > 0 && type < BOX_TYPE_COUNT ? type : 0];
}

// Speed: -2x .. +2x with sticky zones at -1x, freeze and +1x.
inline float SpeedFromKnob(float v) {
  float x = (v - 0.5f) * 4.0f;
  if (std::fabs(x) < 0.08f) return 0.0f;
  if (std::fabs(x - 1.0f) < 0.08f) return 1.0f;
  if (std::fabs(x + 1.0f) < 0.08f) return -1.0f;
  return x;
}

inline int EveryFromKnob(float v) {
  static const int steps[] = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};
  return steps[static_cast<int>(std::lround(Clamp01(v) * 12.0f))];
}

inline float SizeMsFromKnob(float v) { return 5.0f * std::pow(400.0f, Clamp01(v)); }
inline float LfoHzFromKnob(float v) { return 0.02f * std::pow(1000.0f, Clamp01(v)); }
inline int LfoShapeFromKnob(float v) { return static_cast<int>(std::lround(Clamp01(v) * 3.0f)); }
inline float EnvReleaseMsFromKnob(float v) { return 10.0f * std::pow(50.0f, Clamp01(v)); }
inline float EnvGainFromKnob(float v) { return 1.0f + 7.0f * Clamp01(v); }
inline float ShHzFromKnob(float v) { return 0.1f * std::pow(100.0f, Clamp01(v)); }
inline float ShSlewMsFromKnob(float v) { return 300.0f * Clamp01(v) * Clamp01(v); }
inline float BpmFromKnob(float v) { return 40.0f + 200.0f * Clamp01(v); }
inline float AdAttackMsFromKnob(float v) { return 1.0f * std::pow(2000.0f, Clamp01(v)); }  // 1 ms .. 2 s
inline float AdDecayMsFromKnob(float v) { return 5.0f * std::pow(2000.0f, Clamp01(v)); }   // 5 ms .. 10 s
inline float BeatsFromKnob(float v) {
  static const float beats[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
  return beats[static_cast<int>(std::lround(Clamp01(v) * 7.0f))];
}

inline void ApplyLooper(const float* n, LooperParams* p, ExciteParams* e) {
  float tune = n[LP_TUNE] * 24.0f - 12.0f;
  p->tune_semitones = n[LP_TUNE_QUANT] > 0.5f ? std::round(tune) : tune;
  p->tune_octaves = std::round(n[LP_OCTAVE] * 6.0f - 3.0f);
  p->start = n[LP_START];
  p->end = n[LP_END];
  p->position = n[LP_POSITION];
  p->speed = SpeedFromKnob(n[LP_SPEED]);
  p->grains = std::round(1.0f + n[LP_GRAINS] * 31.0f);
  p->spread = n[LP_SPREAD];
  p->attack_curve = n[LP_ATTACK] * 2.0f - 1.0f;
  p->decay_curve = n[LP_DECAY] * 2.0f - 1.0f;
  p->size_ms = SizeMsFromKnob(n[LP_SIZE]);
  p->feedback = n[LP_FEEDBACK];
  e->every = EveryFromKnob(n[LP_EVERY]);
  e->probability = n[LP_PROBABILITY];
  e->gate = true;  // the exciter bus carries only the chosen grains
}

inline const float* VocoderDefaults() {
  static const float d[VP_COUNT] = {1.0f, 0.5f, 0.5f, 0.0f};  // body, 50 ms, no shift, no noise
  return d;
}

inline void ApplyVocoder(const float* n, const ResonatorParams& body, VocoderParams* v) {
  v->layout = n[VP_LAYOUT] > 0.5f ? 1 : 0;
  v->release_ms = 5.0f * std::pow(100.0f, Clamp01(n[VP_RELEASE]));  // 5 .. 500 ms
  v->formant = (Clamp01(n[VP_FORMANT]) - 0.5f) * 24.0f;             // -12 .. +12 semitones
  v->noise = Clamp01(n[VP_NOISE]);
  v->frequency = body.frequency;
  v->note = body.note;
  v->structure = body.structure;
  v->brightness = body.brightness;
  v->damping = body.damping;
  v->position = body.position;
}

inline void ApplyResonator(const float* n, ResonatorParams* p) {
  static const int poly[] = {1, 2, 4};
  p->dry_wet = n[RP_DRY_WET];
  p->frequency = n[RP_FREQUENCY];
  p->structure = n[RP_STRUCTURE];
  p->brightness = n[RP_BRIGHTNESS];
  p->damping = n[RP_DAMPING];
  p->position = n[RP_POSITION];
  p->model = static_cast<int>(std::lround(Clamp01(n[RP_MODEL]) * 5.0f));
  p->polyphony = poly[static_cast<int>(std::lround(Clamp01(n[RP_POLYPHONY]) * 2.0f))];
}

}  // namespace gr
