// Renders vocoder demos: the synthetic voice as modulator, the resonator's
// body or Rings' output as carrier.

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../src/resonator.h"
#include "../src/test_voice.h"
#include "../src/vocoder.h"

using namespace gr;

static void WriteWav(const std::string& path, std::vector<float>& l, std::vector<float>& r) {
  float peak = 0.0f; double sum = 0.0;
  for (size_t i = 0; i < l.size(); ++i) {
    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { l[i] = r[i] = 0.0f; std::printf("NaN!\n"); }
    peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    sum += 0.5 * (l[i] * l[i] + r[i] * r[i]);
  }
  float gain = peak > 0.0f ? 0.891f / peak : 1.0f;
  FILE* f = std::fopen(path.c_str(), "wb");
  uint32_t frames = l.size(), data = frames * 4, riff = 36 + data, sr = 48000, br = sr * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
  std::fwrite(&fmt, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f);
  std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&align, 2, 1, f);
  std::fwrite(&bits, 2, 1, f); std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f);
  for (size_t i = 0; i < frames; ++i) {
    int16_t s[2] = {static_cast<int16_t>(std::lrint(l[i] * gain * 32767)), static_cast<int16_t>(std::lrint(r[i] * gain * 32767))};
    std::fwrite(s, 2, 2, f);
  }
  std::fclose(f);
  std::printf("%-48s peak before normalising %.3f  rms %.1f dBFS\n", path.c_str(), peak,
              10.0f * std::log10(sum / frames + 1e-12) + 20.0f * std::log10(gain));
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  std::vector<float> voice = TestVoiceLoop();  // about 8 s
  const size_t frames = voice.size();
  const float a2 = (45 - 12) / 60.0f, d3 = (50 - 12) / 60.0f, f3 = (53 - 12) / 60.0f, e3 = (52 - 12) / 60.0f;

  // 4. The body rings on internal noise; the voice chooses its partials.
  {
    auto voc = std::make_unique<Vocoder>(); voc->Init(3);
    VocoderParams p; p.layout = 1; p.noise = 1.0f; p.structure = 0.25f; p.damping = 0.75f; p.brightness = 0.6f; p.position = 0.15f;
    std::vector<float> l(frames), r(frames), zero(kBlockSize, 0.0f);
    for (size_t pos = 0; pos + kBlockSize <= frames; pos += kBlockSize) {
      float t = pos / 48000.0f;
      p.frequency = t < 2 ? a2 : t < 4 ? d3 : t < 6 ? f3 : e3;
      voc->Process(&voice[pos], zero.data(), &l[pos], &r[pos], kBlockSize, p);
    }
    WriteWav(dir + "/4_vocoder_corpo_che_canta.wav", l, r);
  }

  // Carrier: Rings' sympathetic strings, strummed every quarter second by noise bursts.
  std::vector<float> rings_l(frames), rings_r(frames);
  {
    auto res = std::make_unique<Resonator>(); res->Init();
    ResonatorParams rp; rp.model = 1; rp.dry_wet = 1.0f; rp.damping = 0.7f; rp.brightness = 0.6f; rp.structure = 0.4f; rp.frequency = a2;
    uint32_t rng = 9;
    std::vector<float> ex(kBlockSize), zero(kBlockSize, 0.0f);
    for (size_t pos = 0; pos + kBlockSize <= frames; pos += kBlockSize) {
      float t = pos / 48000.0f;
      bool strum = std::fmod(t, 0.25f) < kBlockSize / 48000.0f;
      for (size_t i = 0; i < kBlockSize; ++i) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float burst = std::fmod(t, 0.25f) < 0.01f ? 1.0f : 0.02f;
        ex[i] = burst * ((rng >> 8) * (2.0f / 16777216.0f) - 1.0f) * 0.5f;
      }
      rp.frequency = t < 4 ? a2 : d3;
      res->Process(ex.data(), zero.data(), zero.data(), strum, &rings_l[pos], &rings_r[pos], kBlockSize, rp);
    }
    std::vector<float> l(rings_l), r(rings_r);
    WriteWav(dir + "/5a_portante_rings_da_sola.wav", l, r);
  }

  // 5. Classic bands, carrier = Rings.
  for (int layout = 0; layout < 2; ++layout) {
    auto voc = std::make_unique<Vocoder>(); voc->Init(5);
    VocoderParams p; p.layout = layout; p.damping = layout ? 0.6f : 0.3f; p.release_ms = 40.0f;
    p.structure = 0.4f; p.brightness = 0.6f; p.position = 0.3f;
    std::vector<float> l(frames), r(frames), car(kBlockSize);
    for (size_t pos = 0; pos + kBlockSize <= frames; pos += kBlockSize) {
      float t = pos / 48000.0f;
      p.frequency = t < 4 ? a2 : d3;
      for (size_t i = 0; i < kBlockSize; ++i) car[i] = 0.5f * (rings_l[pos + i] + rings_r[pos + i]);
      voc->Process(&voice[pos], car.data(), &l[pos], &r[pos], kBlockSize, p);
    }
    WriteWav(dir + (layout ? "/6_vocoder_corpo_portante_rings.wav" : "/5_vocoder_bande_portante_rings.wav"), l, r);
  }
  return 0;
}
