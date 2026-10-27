#include "engine.h"

#include <algorithm>
#include <cmath>


namespace gr {

namespace {

inline float SoftClip(float x) {
  float a = std::fabs(x);
  if (a < 0.7f) return x;
  float y = 0.7f + 0.3f * std::tanh((a - 0.7f) / 0.3f);
  return x < 0.0f ? -y : y;
}

constexpr float kTriggerThreshold = 0.1f;
constexpr uint64_t kRecDoubleTap = static_cast<uint64_t>(0.300 * 48000);  // as the looper's

}  // namespace

void Engine::Init(size_t loop_seconds) {
  size_t capacity = static_cast<size_t>(loop_seconds * kSampleRate);
  for (int c = 0; c < 2; ++c) {
    buffer_[c].assign(capacity, 0.0f);
    undo_[c].assign(capacity, 0.0f);
    scratch_[c].assign(static_cast<size_t>(kSampleRate), 0.0f);
    looper_[c].Init(buffer_[c].data(), undo_[c].data(), capacity,
                    scratch_[c].data(), scratch_[c].size(), 0x9e3779b9u + c);
    delay_memory_[c].assign(2 * Delay::kLineSize, 0.0f);
    delay_[c].Init(delay_memory_[c].data());
    resonator_[c].Init();
    vocoder_[c].Init(0x51ed270bu + c);
    std::copy(VocoderDefaults(), VocoderDefaults() + VP_COUNT, voc_base_[c]);
    std::copy(LooperDefaults(), LooperDefaults() + LP_COUNT, looper_base_[c]);
    std::copy(ResoDefaults(), ResoDefaults() + RP_COUNT, reso_base_[c]);
  }
  std::copy(ModDefaults(), ModDefaults() + MP_COUNT, mod_base_);
  // Starting boxes: two LFOs and a sample & hold (the clock is fixed).
  SetBoxType(0, BOX_LFO);
  SetBoxType(1, BOX_LFO);
  boxes_[1].knob[BP_A] = 0.15f;
  boxes_[1].knob[BP_B] = 0.34f;
  SetBoxType(2, BOX_SH);
  std::fill(trim_, trim_ + CS_COUNT, 1.0f);
  for (int ch = 0; ch < 2; ++ch) {
    input_base_[ch][IP_SOURCE] = ch * 0.5f;  // A: left, B: right
    input_base_[ch][IP_LEVEL] = 0.5f;        // unity
  }
  for (auto& e : env_base_) { e[EP_RELEASE] = 0.5f; e[EP_GAIN] = 0.3f; }
  // Mixer: inputs at -6 dB, resonators and vocoders at 0 dB, loopers and
  // chosen grains silent until the player brings them up. A left, B right.
  for (int s = 0; s < AS_COUNT; ++s) {
    float v = 0.0f;
    if (s == AS_IN_A || s == AS_IN_B) v = 0.5f;
    if (s == AS_RESO_A || s == AS_RESO_B || s == AS_VOC_A || s == AS_VOC_B) v = 0.7071f;
    mix_base_[s][XP_VOLUME] = v;
    mix_base_[s][XP_PAN] = (s - AS_IN_A) % 2 == 0 ? 0.35f : 0.65f;
  }

  // The default patch is the chain of the spec, with grains strumming.
  audio_[AS_IN_A][AD_LOOPER_A] = 1.0f;
  audio_[AS_IN_B][AD_LOOPER_B] = 1.0f;
  audio_[AS_LOOPER_A][AD_RESO_A] = 1.0f;
  audio_[AS_LOOPER_B][AD_RESO_B] = 1.0f;
  control_[CS_GRAINS_A][CD_STRUM_A] = 1.0f;
  control_[CS_GRAINS_B][CD_STRUM_B] = 1.0f;
}

void Engine::SetParam(int module, int index, float value) {
  value = Clamp01(value);
  switch (module) {
    case M_LOOPER_A: case M_LOOPER_B:
      if (index >= 0 && index < LP_COUNT) looper_base_[module][index] = value;
      break;
    case M_RESO_A: case M_RESO_B:
      if (index >= 0 && index < RP_COUNT) reso_base_[module - M_RESO_A][index] = value;
      break;
    case M_MOD:
      if (index >= 0 && index < MP_COUNT) mod_base_[index] = value;
      break;
    case M_INPUT:
      if (index >= 0 && index < 2 * IP_COUNT) input_base_[index / IP_COUNT][index % IP_COUNT] = value;
      break;
    case M_ENV:
      if (index >= 0 && index < AS_COUNT * EP_COUNT) env_base_[index / EP_COUNT][index % EP_COUNT] = value;
      break;
    case M_VOC:
      if (index >= 0 && index < 2 * VP_COUNT) voc_base_[index / VP_COUNT][index % VP_COUNT] = value;
      break;
    case M_BOX:
      if (index >= 0 && index < kMaxBoxes * BP_COUNT) boxes_[index / BP_COUNT].knob[index % BP_COUNT] = value;
      break;
    case M_MIX:
      if (index >= 0 && index < AS_COUNT * XP_COUNT) mix_base_[index / XP_COUNT][index % XP_COUNT] = value;
      break;
  }
}

void Engine::SetRecButton(int ch, bool pressed) {
  ch &= 1;
  RecSync& r = rec_sync_[ch];
  if (!r.enabled) { looper_[ch].SetRecButton(pressed); return; }
  if (pressed) {
    // A press that is not part of the gesture in course (not the second tap of
    // a double tap, nothing still queued) starts a new gesture: it waits.
    bool idle = !r.waiting && r.count == 0;
    if (idle && (!r.shift_known || now_ - r.last_press >= kRecDoubleTap)) {
      r.waiting = true;
      r.shift_known = false;
      r.gesture_start = now_;
    }
    r.last_press = now_;
  }
  if (r.count < 16) r.queue[r.count++] = {pressed, now_};
  DeliverRec(ch);
}

void Engine::SetRecSync(int ch, bool on) {
  ch &= 1;
  RecSync& r = rec_sync_[ch];
  if (r.enabled == on) return;
  r.enabled = on;
  if (!on) {  // whatever was waiting happens now, in order
    for (int i = 0; i < r.count; ++i) looper_[ch].SetRecButton(r.queue[i].pressed);
    r.count = 0;
    r.waiting = false;
    r.shift_known = false;
  }
}

void Engine::DeliverRec(int ch) {
  RecSync& r = rec_sync_[ch];
  if (r.waiting) return;
  int k = 0;
  while (k < r.count && r.queue[k].arrival + r.shift <= now_) looper_[ch].SetRecButton(r.queue[k++].pressed);
  for (int i = k; i < r.count; ++i) r.queue[i - k] = r.queue[i];
  r.count -= k;
}

void Engine::SetBoxType(int slot, int type) {
  if (slot < 0 || slot >= kMaxBoxes || type < 0 || type >= BOX_TYPE_COUNT) return;
  Box& b = boxes_[slot];
  if (b.type == type) return;
  b = Box();
  b.type = type;
  std::copy(BoxDefaults(type), BoxDefaults(type) + BP_COUNT, b.knob);
  b.sh.Seed(0x2545f491u + 0x9e3779b9u * static_cast<uint32_t>(slot));
  b.anchor = 0.0;  // a new clock box sits on the master grid
  int row = CS_BOX_FIRST + slot, column = CD_BOX_TRIG_FIRST + slot;
  std::fill(control_[row], control_[row] + CD_COUNT, 0.0f);
  for (auto& r : control_) r[column] = 0.0f;
  trim_[row] = 1.0f;
  source_values_[row] = 0.0f;
  dest_values_[column] = 0.0f;
}

float Engine::param(int module, int index) const {
  if (module <= M_LOOPER_B) return looper_base_[module][index];
  if (module <= M_RESO_B) return reso_base_[module - M_RESO_A][index];
  if (module == M_INPUT) return input_base_[index / IP_COUNT][index % IP_COUNT];
  if (module == M_ENV) return env_base_[index / EP_COUNT][index % EP_COUNT];
  if (module == M_VOC) return voc_base_[index / VP_COUNT][index % VP_COUNT];
  if (module == M_BOX) return boxes_[index / BP_COUNT].knob[index % BP_COUNT];
  if (module == M_MIX) return mix_base_[index / XP_COUNT][index % XP_COUNT];
  return mod_base_[index];
}

void Engine::SetAudioPin(int s, int d, float level) {
  if (s >= 0 && s < AS_COUNT && d >= 0 && d < AD_COUNT) audio_[s][d] = Clamp01(level);
}

void Engine::SetControlPin(int s, int d, float depth) {
  if (s >= 0 && s < CS_COUNT && d >= 0 && d < CD_COUNT)
    control_[s][d] = std::max(-1.0f, std::min(1.0f, depth));
}

void Engine::SetControlTrim(int s, float trim) {
  if (s >= 0 && s < CS_COUNT) trim_[s] = std::max(0.0f, std::min(2.0f, trim));
}

void Engine::ClearPins() {
  for (auto& row : audio_) std::fill(row, row + AD_COUNT, 0.0f);
  for (auto& row : control_) std::fill(row, row + CD_COUNT, 0.0f);
}

float Engine::ControlSum(int dest) {
  float v = 0.0f;
  for (int s = 0; s < CS_COUNT; ++s) {
    if (control_[s][dest] == 0.0f) continue;
    float x = std::max(-1.0f, std::min(1.0f, source_values_[s] * trim_[s]));
    v += control_[s][dest] * x;
  }
  return v;
}

void Engine::MixAudio(int d, size_t n, float* mono, float* left, float* right) const {
  std::fill(mono, mono + n, 0.0f);
  if (left) std::fill(left, left + n, 0.0f);
  if (right) std::fill(right, right + n, 0.0f);
  for (int s = 0; s < AS_COUNT; ++s) {
    float g = audio_[s][d];
    if (g == 0.0f) continue;
    for (size_t i = 0; i < n; ++i) {
      float l = src_l_[s][i], r = src_r_[s][i];
      mono[i] += g * 0.5f * (l + r);
      if (left) left[i] += g * l;
      if (right) right[i] += g * r;
    }
  }
}

void Engine::Process(const float* in_l, const float* in_r, float* out_l,
                     float* out_r, size_t n) {
  size_t done = 0;
  while (done < n) {
    size_t chunk = std::min(kBlockSize, n - done);
    ProcessChunk(in_l + done, in_r + done, out_l + done, out_r + done, chunk);
    done += chunk;
  }
}

void Engine::ProcessChunk(const float* in_l, const float* in_r, float* out_l,
                          float* out_r, size_t n) {
  // Inputs: each channel takes the host's left, right, or both.
  int sel[2] = {InputSourceFromKnob(input_base_[0][IP_SOURCE]), InputSourceFromKnob(input_base_[1][IP_SOURCE])};
  float lvl[2] = {InputLevelFromKnob(input_base_[0][IP_LEVEL]), InputLevelFromKnob(input_base_[1][IP_LEVEL])};
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 2; ++c) {
      float x = sel[c] == 0 ? in_l[i] : sel[c] == 1 ? in_r[i] : 0.5f * (in_l[i] + in_r[i]);
      src_l_[AS_IN_A + c][i] = src_r_[AS_IN_A + c][i] = x * lvl[c];
    }
  }

  // Control sources at the start of the block.
  const float* m = mod_base_;
  for (int a = 0; a < AS_COUNT; ++a) source_values_[CS_ENV_FIRST + a] = env_[a].out();
  double before, after;
  clock_.Advance(n, BpmFromKnob(m[MP_BPM]), &before, &after);
  beats_now_ = after;
  bool tick = clock_.TakePending() || GridCrossed(before, after, 0.0, BeatsFromKnob(m[MP_CLOCK_DIV]), 0.0);
  source_values_[CS_CLOCK] = tick ? 1.0f : 0.0f;
  if (tick) ++source_pulses_[CS_CLOCK];
  // REC sync: a waiting gesture starts on this tick; its events keep their spacing.
  for (int c = 0; c < 2; ++c) {
    RecSync& r = rec_sync_[c];
    if (r.waiting && tick) {
      r.shift = now_ - r.gesture_start;
      r.waiting = false;
      r.shift_known = true;
    }
    DeliverRec(c);
  }
  for (int k = 0; k < kMaxBoxes; ++k) {
    Box& b = boxes_[k];
    float v = 0.0f;
    switch (b.type) {
      case BOX_LFO: v = b.lfo.Process(LfoHzFromKnob(b.knob[BP_A]), LfoShapeFromKnob(b.knob[BP_B]), n); break;
      case BOX_SH: v = b.sh.out(); break;
      case BOX_CLOCK: {
        // A division of the master clock, shifted by part of a division.
        bool t = b.pending || GridCrossed(before, after, b.anchor, BeatsFromKnob(b.knob[BP_A]),
                                          std::min(b.knob[BP_B], 0.999f));
        b.pending = false;
        v = t ? 1.0f : 0.0f;
        if (t) ++source_pulses_[CS_BOX_FIRST + k];
        break;
      }
      case BOX_AD: v = b.ad.out(); break;
    }
    source_values_[CS_BOX_FIRST + k] = v;
  }
  source_values_[CS_HEAD_A] = looper_[0].playhead();
  source_values_[CS_HEAD_B] = looper_[1].playhead();

  // Modulated parameters.
  LooperParams lp[2];
  ExciteParams ep[2];
  ResonatorParams rp[2];
  VocoderParams vp[2];
  DelayParams dp[2];
  for (int c = 0; c < 2; ++c) {
    float nl[LP_COUNT], nr[RP_COUNT], nv[VP_COUNT];
    std::copy(voc_base_[c], voc_base_[c] + VP_COUNT, nv);
    for (int j = 0; j < 2; ++j) {
      int dv = CD_VOC_A + 2 * c + j;
      nv[kVocoderTargets[j]] = Clamp01(nv[kVocoderTargets[j]] + ControlSum(dv));
      dest_values_[dv] = nv[kVocoderTargets[j]];
    }
    std::copy(looper_base_[c], looper_base_[c] + LP_COUNT, nl);
    std::copy(reso_base_[c], reso_base_[c] + RP_COUNT, nr);
    for (int j = 0; j < 6; ++j) {
      int dl = CD_LOOPER_A + 6 * c + j, dr = CD_RESO_A + 6 * c + j;
      nl[kLooperTargets[j]] = Clamp01(nl[kLooperTargets[j]] + ControlSum(dl));
      nr[kResoTargets[j]] = Clamp01(nr[kResoTargets[j]] + ControlSum(dr));
      dest_values_[dl] = nl[kLooperTargets[j]];
      dest_values_[dr] = nr[kResoTargets[j]];
    }
    bool ping_pong = nl[LP_DLY_MODE] > 0.5f;
    for (int j = 0; j < 8; ++j) {
      int dd = CD_DELAY_A + 8 * c + j;
      // In Ping-pong mode the echoes place themselves: the pan column is off.
      if (!(ping_pong && kDelayTargets[j] == LP_DLY_PAN))
        nl[kDelayTargets[j]] = Clamp01(nl[kDelayTargets[j]] + ControlSum(dd));
      dest_values_[dd] = nl[kDelayTargets[j]];
    }
    ApplyLooper(nl, &lp[c], &ep[c]);
    ApplyDelay(nl, &dp[c]);
    ApplyResonator(nr, &rp[c]);
    rp[c].full_resolution = m[MP_FULL_RES] > 0.5f;
    ApplyVocoder(nv, rp[c], &vp[c]);
  }

  // Loopers.
  for (int c = 0; c < 2; ++c) {
    float x[kBlockSize];
    MixAudio(AD_LOOPER_A + c, n, x, nullptr, nullptr);
    float ex[kBlockSize];
    int triggers = looper_[c].Process(x, src_l_[AS_LOOPER_A + c], src_r_[AS_LOOPER_A + c],
                                      ex, n, lp[c], ep[c]);
    // The looper's own delay: its echoes are part of the looper's output.
    delay_[c].Process(src_l_[AS_LOOPER_A + c], src_r_[AS_LOOPER_A + c], n, dp[c]);
    std::copy(ex, ex + n, src_l_[AS_GRAINS_A + c]);
    std::copy(ex, ex + n, src_r_[AS_GRAINS_A + c]);
    source_values_[CS_GRAINS_A + c] = triggers > 0 ? 1.0f : 0.0f;
    if (triggers > 0) ++source_pulses_[CS_GRAINS_A + c];
  }

  // Trigger inputs fire on a rising edge of their summed input.
  bool fire[kFixedTriggers];
  for (int t = 0; t < kFixedTriggers; ++t) {
    float v = ControlSum(CD_STRUM_A + t);
    fire[t] = v > kTriggerThreshold && trigger_prev_[t] <= kTriggerThreshold;
    trigger_prev_[t] = v;
    if (fire[t]) ++trigger_fires_[t];
    dest_values_[CD_STRUM_A + t] = v;
  }
  if (fire[2]) {
    // Master reset: beat zero now, clock boxes back on the grid.
    clock_.Reset();
    beats_now_ = 0.0;
    for (Box& b : boxes_) {
      if (b.type != BOX_CLOCK) continue;
      b.anchor = 0.0;
      b.pending = b.knob[BP_B] < 0.001f;
    }
  }
  // Box triggers: the LFO and the clock restart, the S&H samples, the AD starts.
  bool box_patched[kMaxBoxes] = {};
  for (int k = 0; k < kMaxBoxes; ++k) {
    Box& b = boxes_[k];
    if (b.type == BOX_NONE) continue;
    int column = CD_BOX_TRIG_FIRST + k;
    for (int s = 0; s < CS_COUNT && !box_patched[k]; ++s) box_patched[k] = control_[s][column] != 0.0f;
    float v = box_patched[k] ? ControlSum(column) : 0.0f;
    bool fired = v > kTriggerThreshold && b.trigger_prev <= kTriggerThreshold;
    b.trigger_prev = v;
    dest_values_[column] = v;
    if (!fired) continue;
    ++trigger_fires_[kFixedTriggers + k];
    switch (b.type) {
      case BOX_LFO: b.lfo.Reset(); break;
      case BOX_SH: b.sh.Trigger(); break;
      case BOX_CLOCK: {
        // Restart the division from now: a tick at once, then every division.
        double division = BeatsFromKnob(b.knob[BP_A]);
        b.anchor = beats_now_ - std::min(b.knob[BP_B], 0.999f) * division;
        b.pending = true;
        break;
      }
      case BOX_AD: b.ad.Trigger(); break;
    }
  }

  // Resonators.
  for (int c = 0; c < 2; ++c) {
    float x[kBlockSize], dl[kBlockSize], dr[kBlockSize];
    MixAudio(AD_RESO_A + c, n, x, dl, dr);
    // Nothing patched into the resonator: strums use Rings' internal exciter.
    bool fed = false;
    for (int s = 0; s < AS_COUNT; ++s) fed |= audio_[s][AD_RESO_A + c] != 0.0f;
    rp[c].internal_exciter = !fed;
    resonator_[c].Process(x, dl, dr, fire[c], src_l_[AS_RESO_A + c],
                          src_r_[AS_RESO_A + c], n, rp[c]);
  }

  // Vocoders: modulator and carrier from the matrix. With nothing on the
  // modulator there is nothing to transfer, so the vocoder rests.
  for (int c = 0; c < 2; ++c) {
    bool active = false;
    for (int s = 0; s < AS_COUNT; ++s) active |= audio_[s][AD_VOC_A_MOD + 2 * c] != 0.0f;
    if (!active) {
      std::fill(src_l_[AS_VOC_A + c], src_l_[AS_VOC_A + c] + n, 0.0f);
      std::fill(src_r_[AS_VOC_A + c], src_r_[AS_VOC_A + c] + n, 0.0f);
      continue;
    }
    float mod[kBlockSize], car[kBlockSize];
    MixAudio(AD_VOC_A_MOD + 2 * c, n, mod, nullptr, nullptr);
    MixAudio(AD_VOC_A_CAR + 2 * c, n, car, nullptr, nullptr);
    vocoder_[c].Process(mod, car, src_l_[AS_VOC_A + c], src_r_[AS_VOC_A + c], n, vp[c]);
  }

  // Output: every source through its volume and pan (equal power; centre = unity).
  {
    float l[kBlockSize] = {}, r[kBlockSize] = {};
    for (int s = 0; s < AS_COUNT; ++s) {
      float pan = Clamp01(mix_base_[s][XP_PAN] + ControlSum(CD_PAN_FIRST + s));
      dest_values_[CD_PAN_FIRST + s] = pan;
      float volume = VolumeFromKnob(mix_base_[s][XP_VOLUME]);
      float angle = pan * 1.5707963f;
      float gl = volume * 1.4142136f * std::cos(angle), gr = volume * 1.4142136f * std::sin(angle);
      // Ramp from last block's gains, so jumps (a S&H on the pan, a fader) never click.
      float gl0 = mix_gl_[s], gr0 = mix_gr_[s];
      mix_gl_[s] = gl;
      mix_gr_[s] = gr;
      if (gl0 == 0.0f && gr0 == 0.0f && gl == 0.0f && gr == 0.0f) continue;
      float dl = (gl - gl0) / static_cast<float>(n), dr = (gr - gr0) / static_cast<float>(n);
      for (size_t i = 0; i < n; ++i) {
        gl0 += dl;
        gr0 += dr;
        l[i] += gl0 * src_l_[s][i];
        r[i] += gr0 * src_r_[s][i];
      }
    }
    float g = m[MP_MASTER];
    for (size_t i = 0; i < n; ++i) {
      out_l[i] = SoftClip(l[i] * g);
      out_r[i] = SoftClip(r[i] * g);
    }
  }

  // The envelope of every audio source, and the sample & hold, for the next block.
  for (int a = 0; a < AS_COUNT; ++a) {
    float x[kBlockSize];
    for (size_t i = 0; i < n; ++i) x[i] = 0.5f * (src_l_[a][i] + src_r_[a][i]);
    env_[a].Process(x, n, EnvReleaseMsFromKnob(env_base_[a][EP_RELEASE]), EnvGainFromKnob(env_base_[a][EP_GAIN]));
  }
  for (int k = 0; k < kMaxBoxes; ++k) {
    Box& b = boxes_[k];
    if (b.type == BOX_SH) {
      // With nothing on its trigger the S&H runs on its own clock.
      b.sh.Process(n, ShHzFromKnob(b.knob[BP_A]), ShSlewMsFromKnob(b.knob[BP_B]), !box_patched[k]);
    } else if (b.type == BOX_AD) {
      b.ad.Process(n, AdAttackMsFromKnob(b.knob[BP_A]), AdDecayMsFromKnob(b.knob[BP_B]));
    }
  }
  now_ += n;
}

void Engine::Overview(int ch, float* out, int bins) const {
  const GranularLooper& lp = looper_[ch & 1];
  if (lp.state() == LooperState::kEmpty) { lp.LiveOverview(out, bins); return; }  // the live input
  size_t len = lp.recorded();
  const float* data = lp.data();
  for (int b = 0; b < bins; ++b) {
    size_t from = len * b / bins, to = len * (b + 1) / bins;
    size_t step = std::max<size_t>(1, (to - from) / 64);
    float peak = 0.0f;
    for (size_t i = from; i < to; i += step) peak = std::max(peak, std::fabs(data[i]));
    out[b] = std::min(1.0f, peak);
  }
}

}  // namespace gr
