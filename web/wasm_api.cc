// C interface of the engine for the web prototype (WebAssembly, AudioWorklet).

#include <algorithm>

#include "engine.h"

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

namespace {
gr::Engine* engine = nullptr;
constexpr int kQuantum = 128;
float in_a[kQuantum], in_b[kQuantum], out_l[kQuantum], out_r[kQuantum];
float grain_buf[gr::kMaxGrains * 3];
float overview_buf[1024];
}  // namespace

EXPORT(gr_init) void gr_init() {
  if (engine) return;
  engine = new gr::Engine();
  engine->Init(60);
}

EXPORT(gr_in_a) float* gr_in_a() { return in_a; }
EXPORT(gr_in_b) float* gr_in_b() { return in_b; }
EXPORT(gr_out_l) float* gr_out_l() { return out_l; }
EXPORT(gr_out_r) float* gr_out_r() { return out_r; }
EXPORT(gr_grain_buf) float* gr_grain_buf() { return grain_buf; }
EXPORT(gr_overview_buf) float* gr_overview_buf() { return overview_buf; }
EXPORT(gr_dest_values) const float* gr_dest_values() { return engine->control_dest_values(); }
EXPORT(gr_source_values) const float* gr_source_values() { return engine->control_source_values(); }
EXPORT(gr_source_pulses) const uint32_t* gr_source_pulses() { return engine->source_pulses(); }
EXPORT(gr_trigger_fires) const uint32_t* gr_trigger_fires() { return engine->trigger_fires(); }

EXPORT(gr_control_source_count) int gr_control_source_count() { return gr::CS_COUNT; }
EXPORT(gr_control_dest_count) int gr_control_dest_count() { return gr::CD_COUNT; }
EXPORT(gr_trigger_count) int gr_trigger_count() { return gr::kTriggerCount; }
EXPORT(gr_box_count) int gr_box_count() { return gr::kMaxBoxes; }
// type: 0 none, 1 LFO, 2 sample & hold, 3 clock (a division of the master), 4 AD envelope.
EXPORT(gr_box_type) void gr_box_type(int slot, int type) { engine->SetBoxType(slot, type); }

EXPORT(gr_process) void gr_process(int n) {
  engine->Process(in_a, in_b, out_l, out_r, std::min(n, kQuantum));
}

// module: 0 looper A, 1 looper B, 2 resonator A, 3 resonator B,
// 4 global {master, full resolution, BPM, clock division},
// 5 inputs (channel * 2 + {source, level}), 6 envelopes (source * 2 + {release, gain}),
// 7 vocoders (channel * 4 + {layout, release, formant, noise}), 8 boxes (slot * 2 + knob),
// 9 mixer (audio source * 2 + {volume, pan}).
EXPORT(gr_param) void gr_param(int module, int index, float v) { engine->SetParam(module, index, v); }
EXPORT(gr_audio_pin) void gr_audio_pin(int s, int d, float v) { engine->SetAudioPin(s, d, v); }
EXPORT(gr_control_pin) void gr_control_pin(int s, int d, float v) { engine->SetControlPin(s, d, v); }
EXPORT(gr_clear_pins) void gr_clear_pins() { engine->ClearPins(); }
EXPORT(gr_control_trim) void gr_control_trim(int s, float v) { engine->SetControlTrim(s, v); }

EXPORT(gr_rec) void gr_rec(int ch, int pressed) { engine->SetRecButton(ch, pressed != 0); }
EXPORT(gr_rec_sync) void gr_rec_sync(int ch, int on) { engine->SetRecSync(ch, on != 0); }
EXPORT(gr_rec_armed) int gr_rec_armed(int ch) { return engine->rec_armed(ch) ? 1 : 0; }
EXPORT(gr_live) int gr_live(int ch) { return engine->looper(ch).live() ? 1 : 0; }
// Saved loops: read the buffer while playing; to restore, write into it and call gr_loop_restore.
EXPORT(gr_loop_ptr) float* gr_loop_ptr(int ch) { return engine->looper(ch).writable_data(); }
EXPORT(gr_loop_capacity) int gr_loop_capacity(int ch) { return static_cast<int>(engine->looper(ch).capacity()); }
EXPORT(gr_loop_samples) int gr_loop_samples(int ch) {
  const gr::GranularLooper& lp = engine->looper(ch);
  return lp.state() == gr::LooperState::kPlaying ? static_cast<int>(lp.length()) : 0;
}
EXPORT(gr_loop_restore) void gr_loop_restore(int ch, int n) { engine->looper(ch).Restore(static_cast<size_t>(n)); }
EXPORT(gr_undo) void gr_undo(int ch) { engine->looper(ch).Undo(); }
EXPORT(gr_clear) void gr_clear(int ch) { engine->looper(ch).Clear(); }

EXPORT(gr_state) int gr_state(int ch) { return static_cast<int>(engine->looper(ch).state()); }
EXPORT(gr_length) float gr_length(int ch) { return engine->looper(ch).recorded() / gr::kSampleRate; }
EXPORT(gr_playhead) float gr_playhead(int ch) { return engine->looper(ch).playhead(); }
EXPORT(gr_version) int gr_version(int ch) { return static_cast<int>(engine->looper(ch).version()); }

EXPORT(gr_grains) int gr_grains(int ch) {
  gr::GrainView views[gr::kMaxGrains];
  int n = engine->looper(ch).GetGrains(views, gr::kMaxGrains);
  for (int i = 0; i < n; ++i) {
    grain_buf[i * 3] = views[i].position;
    grain_buf[i * 3 + 1] = views[i].envelope;
    grain_buf[i * 3 + 2] = views[i].excites ? 1.0f : 0.0f;
  }
  return n;
}

EXPORT(gr_overview) void gr_overview(int ch, int bins) {
  engine->Overview(ch, overview_buf, std::min(bins, 1024));
}
