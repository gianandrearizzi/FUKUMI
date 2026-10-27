// Delay checks: echo times and sides, decay, filters in the loop, bounded
// feedback, and no clicks on mode or time changes.
#include <cmath>
#include <cstdio>
#include <vector>

#include "delay.h"

using gr::Delay;
using gr::DelayParams;

static int failures = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); printf("%s ", ok_ ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) ++failures; } while (0)

struct Out { std::vector<float> l, r; };

static Out Run(Delay& d, const DelayParams& p, size_t samples, bool impulse) {
  Out o; o.l.assign(samples, 0.0f); o.r.assign(samples, 0.0f);
  if (impulse) { o.l[0] = 1.0f; o.r[0] = 1.0f; }
  for (size_t i = 0; i < samples; i += 24) {
    size_t n = std::min<size_t>(24, samples - i);
    d.Process(&o.l[i], &o.r[i], n, p);
  }
  return o;
}

// Peak of |x| in a window around t (samples).
static float PeakNear(const std::vector<float>& x, size_t t, size_t w = 40) {
  float m = 0.0f;
  for (size_t i = t > w ? t - w : 0; i < std::min(x.size(), t + w); ++i) m = std::max(m, std::fabs(x[i]));
  return m;
}

int main() {
  std::vector<float> mem(2 * Delay::kLineSize);
  const size_t T = 14400;  // 300 ms

  {  // Pan mode, centre, mix 1, feedback 0.5, filters open
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 300; p.feedback = 0.5f; p.mix = 1.0f; p.pan = 0.5f;
    p.hp_fb_hz = 20; p.lp_fb_hz = 20000;
    Out o = Run(d, p, 5 * T, true);
    float e1 = PeakNear(o.l, T), e2 = PeakNear(o.l, 2 * T), e3 = PeakNear(o.l, 3 * T);
    CHECK(std::fabs(e1 - 1.0f) < 0.05f && std::fabs(e2 - 0.5f) < 0.05f && std::fabs(e3 - 0.25f) < 0.05f,
          "pan mode: echoes at 300/600/900 ms of %.2f %.2f %.2f (expect 1, 0.5, 0.25)", e1, e2, e3);
    CHECK(PeakNear(o.l, T / 2) < 0.01f, "nothing between the impulse and the first echo (%.4f)", PeakNear(o.l, T / 2));
  }
  {  // Pan hard right
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 300; p.feedback = 0.0f; p.mix = 1.0f; p.pan = 1.0f;
    Out o = Run(d, p, 2 * T, true);
    CHECK(PeakNear(o.l, T) < 0.01f && PeakNear(o.r, T) > 1.3f, "pan right: echo L %.3f, R %.3f (equal power, +3 dB at the side)", PeakNear(o.l, T), PeakNear(o.r, T));
  }
  {  // Ping-pong: L at T, R at 2T, L at 3T, decaying by feedback each time
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 300; p.feedback = 0.5f; p.mix = 1.0f; p.ping_pong = true;
    p.hp_fb_hz = 20; p.lp_fb_hz = 20000;
    Run(d, p, 600, false);  // let the 5 ms mode fade settle
    Out o = Run(d, p, 5 * T, true);
    CHECK(PeakNear(o.l, T) > 0.95f && PeakNear(o.r, T) < 0.01f, "ping-pong: first echo left only (L %.2f, R %.3f)", PeakNear(o.l, T), PeakNear(o.r, T));
    CHECK(PeakNear(o.r, 2 * T) > 0.45f && PeakNear(o.r, 2 * T) < 0.55f && PeakNear(o.l, 2 * T) < 0.01f,
          "ping-pong: second echo right only (R %.2f, L %.3f)", PeakNear(o.r, 2 * T), PeakNear(o.l, 2 * T));
    CHECK(PeakNear(o.l, 3 * T) > 0.2f && PeakNear(o.l, 3 * T) < 0.3f, "ping-pong: third echo back left (%.2f)", PeakNear(o.l, 3 * T));
  }
  {  // Low-pass in the feedback: every repeat loses more highs
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 100; p.feedback = 0.9f; p.mix = 1.0f; p.lp_fb_hz = 1500; p.hp_fb_hz = 20;
    const size_t t = 4800;
    Out o = Run(d, p, 6 * t, true);
    auto hf = [&](size_t c) { float e = 0; for (size_t i = c - 200; i < c + 200; ++i) { float dx = o.l[i] - o.l[i - 1]; e += dx * dx; } return e; };
    CHECK(hf(2 * t) < 0.5f * hf(t) && hf(3 * t) < 0.5f * hf(2 * t), "feedback low-pass: high-frequency energy per repeat %.3g > %.3g > %.3g", hf(t), hf(2 * t), hf(3 * t));
  }
  {  // Input high-pass removes a low tone from the echoes
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 50; p.feedback = 0.0f; p.mix = 1.0f; p.hp_in_hz = 800;
    const size_t N = 48000; Out o; o.l.resize(N); o.r.resize(N);
    for (size_t i = 0; i < N; ++i) o.l[i] = o.r[i] = std::sin(2 * M_PI * 80 * i / 48000.0);
    std::vector<float> dry = o.l;
    for (size_t i = 0; i < N; i += 24) d.Process(&o.l[i], &o.r[i], 24, p);
    float wet = 0; for (size_t i = 24000; i < N; ++i) { float w = o.l[i] - dry[i]; wet = std::max(wet, std::fabs(w)); }
    CHECK(wet < 0.02f, "input high-pass at 800 Hz: an 80 Hz tone leaves echoes of %.4f", wet);
  }
  {  // Feedback 110% with saturation: grows, then stays bounded
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 50; p.feedback = 1.1f; p.mix = 1.0f;
    Out o = Run(d, p, 48000 * 10, true);
    float m = 0; for (float x : o.l) m = std::max(m, std::fabs(x));
    CHECK(m < 4.0f && std::isfinite(m), "feedback 110%% for 10 s stays bounded (peak %.2f)", m);
  }
  {  // Time jump and mode change on a running sine: no clicks. A click shows as a
     // spike in the second difference; a pitch bend of at most 1.5x only scales
     // it by up to 2.25x.
    Delay d; d.Init(mem.data());
    DelayParams p; p.time_ms = 300; p.feedback = 0.6f; p.mix = 0.8f;
    const size_t N = 48000 * 4; std::vector<float> l(N), r(N);
    for (size_t i = 0; i < N; ++i) l[i] = r[i] = 0.4f * std::sin(2 * M_PI * 220 * i / 48000.0);
    for (size_t i = 0; i < N; i += 24) {
      if (i == 96000) p.time_ms = 120;       // jump in time: glides
      if (i == 144000) p.ping_pong = true;   // mode change: fades
      d.Process(&l[i], &r[i], 24, p);
    }
    auto d2 = [&](size_t a, size_t b) { float m = 0; for (size_t i = a + 2; i < b; ++i) for (auto* x : {&l, &r}) m = std::max(m, std::fabs((*x)[i] - 2 * (*x)[i - 1] + (*x)[i - 2])); return m; };
    float steady = d2(72000, 96000), jump = d2(96000, 144000), mode = d2(144000, 168000);
    // During the glide the echoes bend up to 1.5x in pitch, and an echo that goes
    // round the loop twice while it glides bends twice: its second difference grows
    // with the square of the pitch, up to 1.5^4 = 5x. A click is far larger (the
    // earlier switch-over gave 80x).
    CHECK(jump < 6.0f * steady, "time jump 300 -> 120 ms: second-difference peak %.4f against %.4f steady (pitch bend only)", jump, steady);
    CHECK(mode < 2.0f * steady, "mode change: second-difference peak %.4f against %.4f steady", mode, steady);
    // The 300 -> 120 ms glide takes about 8640 / 0.5 samples, a third of a second.
  }
  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
