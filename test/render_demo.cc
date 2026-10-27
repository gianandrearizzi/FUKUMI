// Renders the demo WAVs: a synthetic voice is recorded into the looper with
// the rec gestures, then played through grains and the resonator.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../src/channel.h"
#include "../src/test_voice.h"

using namespace gr;

// --- WAV --------------------------------------------------------------------

struct Stats { float peak_before; float rms_db; bool finite; };

Stats WriteWav(const std::string& path, std::vector<float>& l, std::vector<float>& r) {
  Stats st{0.0f, 0.0f, true};
  double sum = 0.0;
  for (size_t i = 0; i < l.size(); ++i) {
    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { st.finite = false; l[i] = r[i] = 0.0f; }
    st.peak_before = std::max(st.peak_before, std::max(std::fabs(l[i]), std::fabs(r[i])));
  }
  float gain = st.peak_before > 0.0f ? 0.891f / st.peak_before : 1.0f;  // -1 dBFS
  FILE* f = std::fopen(path.c_str(), "wb");
  uint32_t frames = l.size(), data = frames * 4, riff = 36 + data, sr = 48000, br = sr * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f);
  std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&align, 2, 1, f);
  std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
  for (size_t i = 0; i < frames; ++i) {
    float a = l[i] * gain, b = r[i] * gain;
    sum += 0.5 * (a * a + b * b);
    int16_t s[2] = {static_cast<int16_t>(std::lrint(std::max(-1.0f, std::min(1.0f, a)) * 32767)),
                    static_cast<int16_t>(std::lrint(std::max(-1.0f, std::min(1.0f, b)) * 32767))};
    std::fwrite(s, 2, 2, f);
  }
  std::fclose(f);
  st.rms_db = 10.0f * std::log10(sum / frames + 1e-12);
  return st;
}

// --- Scenario ---------------------------------------------------------------

struct Press { float time; bool down; };

float Ramp(float t, float t0, float t1, float a, float b) {
  if (t <= t0) return a;
  if (t >= t1) return b;
  return a + (b - a) * (t - t0) / (t1 - t0);
}
float Midi(int note) { return (note - 12) / 60.0f; }  // resonator frequency knob

void Render(const std::string& name, float seconds, const std::vector<float>& input,
            const std::vector<Press>& presses,
            const std::function<void(float, ChannelParams&)>& automate) {
  auto channel = std::make_unique<Channel>();
  channel->Init(60, 0x1234567);
  ChannelParams p;
  size_t frames = static_cast<size_t>(seconds * kSampleRate);
  frames -= frames % kBlockSize;
  std::vector<float> l(frames), r(frames);
  size_t next_press = 0;
  float in[kBlockSize], ol[kBlockSize], orr[kBlockSize];
  for (size_t pos = 0; pos < frames; pos += kBlockSize) {
    float t = pos / kSampleRate;
    while (next_press < presses.size() && presses[next_press].time <= t) {
      channel->looper().SetRecButton(presses[next_press].down);
      ++next_press;
    }
    automate(t, p);
    for (size_t i = 0; i < kBlockSize; ++i) in[i] = pos + i < input.size() ? input[pos + i] : 0.0f;
    channel->Process(in, ol, orr, kBlockSize, p);
    LooperState s = channel->looper().state();
    bool recording = s != LooperState::kPlaying && s != LooperState::kEmpty;
    float fade = std::min(1.0f, std::max(0.0f, seconds - 1.0f - t >= 0 ? 1.0f : (seconds - t)));
    for (size_t i = 0; i < kBlockSize; ++i) {
      // While recording, the input is monitored so you hear what goes in.
      float mon = recording ? 0.5f * in[i] : 0.0f;
      l[pos + i] = (ol[i] + mon) * fade;
      r[pos + i] = (orr[i] + mon) * fade;
    }
  }
  Stats st = WriteWav(name, l, r);
  std::printf("%-44s %5.1f s  peak before normalising %.2f  rms %.1f dBFS  %s\n", name.c_str(),
              seconds, st.peak_before, st.rms_db, st.finite ? "finite" : "NaN FOUND");
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  // Phrase 1 (4 s) is the loop; phrase 2 (2 s) is punched in during demo 1.
  std::vector<float> phrase1 = SynthVoice({{57,'a'},{60,'o'},{64,'e'},{62,'i'},{60,'o'},{57,'a'},{55,'u'},{57,'a'}}, 0.5f, 7);
  std::vector<float> phrase2 = SynthVoice({{64,'i'},{67,'a'},{69,'e'},{71,'o'}}, 0.5f, 11);

  {  // The raw test input, for reference.
    std::vector<float> l(phrase1), r;
    l.insert(l.end(), phrase2.begin(), phrase2.end());
    r = l;
    Stats st = WriteWav(dir + "/0_ingresso_di_prova.wav", l, r);
    std::printf("%-44s peak %.2f\n", (dir + "/0_ingresso_di_prova.wav").c_str(), st.peak_before);
  }

  // Tap at 0 s starts the first recording, tap at 4 s closes a 4 s loop.
  std::vector<Press> record = {{0.0f, true}, {0.06f, false}, {4.0f, true}, {4.06f, false}};

  // 1. Grains only: speed, tune, spread, punch-in, freeze + scrub, reverse.
  {
    std::vector<float> input(phrase1);
    input.resize(static_cast<size_t>(12.0f * kSampleRate), 0.0f);
    input.insert(input.end(), phrase2.begin(), phrase2.end());
    std::vector<Press> presses = record;
    presses.push_back({12.0f, true});   // single tap: replace from playhead
    presses.push_back({12.06f, false});
    presses.push_back({14.0f, true});   // tap: stop and play
    presses.push_back({14.06f, false});
    Render(dir + "/1_granulare.wav", 24.0f, input, presses, [](float t, ChannelParams& p) {
      LooperParams& g = p.looper;
      p.resonator.dry_wet = 0.0f;
      g.size_ms = Ramp(t, 4, 8, 150, 250);
      g.grains = t < 8 ? 6 : (t < 14 ? 10 : (t < 19 ? 16 : 8));
      g.speed = t < 14 ? Ramp(t, 4, 8, 1.0f, 0.3f) : (t < 19 ? 0.0f : 0.6f);
      g.spread = t < 8 ? 0.1f : (t < 14 ? Ramp(t, 8, 12, 0.1f, 0.5f) : 0.2f);
      g.tune_semitones = (t >= 8 && t < 14) ? 7.0f : 0.0f;
      g.tune_octaves = (t >= 14 && t < 19) ? -1.0f : 0.0f;
      g.attack_curve = (t >= 8 && t < 14) ? -0.8f : 0.0f;
      g.decay_curve = (t >= 8 && t < 14) ? 0.8f : 0.0f;
      g.position = (t >= 14 && t < 19) ? Ramp(t, 14, 19, 0.0f, 1.0f) : 0.0f;
      if (t >= 14 && t < 19) g.size_ms = 400;
      if (t >= 19) { g.start = 0.85f; g.end = 0.15f; g.size_ms = 120; g.tune_semitones = 12; }
    });
  }

  // 2. Grains strum the resonator every N grains: modal, sympathetic, string.
  Render(dir + "/2_risonatore_strum_ogni_4_grani.wav", 24.0f, phrase1, record, [](float t, ChannelParams& p) {
    LooperParams& g = p.looper;
    g.size_ms = 90; g.grains = 4; g.spread = 0.25f; g.speed = 0.5f;
    p.excite.every = t < 16 ? 4 : 3;
    p.excite.gate = false;
    ResonatorParams& r = p.resonator;
    r.dry_wet = 0.75f; r.brightness = 0.55f; r.damping = 0.65f; r.position = 0.3f;
    if (t < 10) { r.model = 0; r.frequency = Midi(45); r.structure = Ramp(t, 4, 10, 0.3f, 0.6f); }
    else if (t < 16) { r.model = 1; r.structure = 0.4f; r.frequency = t < 13 ? Midi(45) : Midi(50); }
    else { r.model = 2; r.polyphony = 2; r.frequency = Midi(52); r.damping = 0.75f;
           r.position = Ramp(t, 16, 23, 0.2f, 0.8f); }
  });

  // 3. Gate: only every 3rd grain (80% chance) reaches the resonator.
  Render(dir + "/3_risonatore_gate_ogni_3_grani.wav", 24.0f, phrase1, record, [](float t, ChannelParams& p) {
    LooperParams& g = p.looper;
    g.size_ms = 60; g.grains = 6; g.spread = 0.6f; g.speed = 1.0f; g.tune_octaves = 1;
    g.attack_curve = 0.9f; g.decay_curve = -0.9f;  // fast rise, fast fall: percussive grains
    p.excite.every = 3; p.excite.probability = 0.8f; p.excite.gate = true;
    ResonatorParams& r = p.resonator;
    r.dry_wet = 0.85f; r.polyphony = 4; r.damping = 0.8f; r.brightness = 0.7f; r.frequency = Midi(45);
    if (t < 12) { r.model = 5; r.structure = 0.45f; }
    else if (t < 18) { r.model = 4; r.structure = Ramp(t, 12, 18, 0.3f, 0.7f); }
    else { r.model = 3; r.frequency = Midi(50); r.structure = 0.5f; }
  });
  return 0;
}
