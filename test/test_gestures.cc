// Checks the rec-button gestures against the spec:
// first recording, single tap (punch), long press (overdub),
// double tap (replace all) and undo.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../src/granular_looper.h"

using namespace gr;

static int failures = 0;
#define CHECK(cond, msg)                                   \
  do {                                                     \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else { std::printf("ok:   %s\n", msg); }               \
  } while (0)

struct Rig {
  std::vector<float> buf, undo, scratch;
  GranularLooper looper;
  LooperParams p;
  ExciteParams e;
  float l[24], r[24], x[24];
  Rig() : buf(48000 * 10), undo(48000 * 10), scratch(48000) {
    looper.Init(buf.data(), undo.data(), buf.size(), scratch.data(),
                scratch.size(), 1);
    p.speed = 0.0f;  // freeze the playhead so punch positions are known
  }
  void Run(float seconds, float value) {
    size_t blocks = static_cast<size_t>(seconds * 48000 / 24);
    float in[24];
    for (float& v : in) v = value;
    for (size_t b = 0; b < blocks; ++b) looper.Process(in, l, r, x, 24, p, e);
  }
  void Tap() { looper.SetRecButton(true); Run(0.01f, current); looper.SetRecButton(false); }
  float current = 0.0f;
};

int main() {
  {
    Rig rig;
    rig.current = 0.5f;
    rig.Tap();
    rig.Run(0.99f, 0.5f);
    rig.looper.SetRecButton(true);  // second tap closes the loop
    rig.looper.SetRecButton(false);
    CHECK(rig.looper.state() == LooperState::kPlaying, "first recording: tap, tap -> playing");
    CHECK(std::abs(static_cast<long>(rig.looper.length()) - 48000) <= 24,
          "first recording: loop length = time between taps (1 s)");
  }
  {
    Rig rig;
    rig.current = 0.5f;
    rig.Tap(); rig.Run(0.99f, 0.5f); rig.Tap();
    // Single tap at playhead 0: replace 0.5 s with 0.25, then tap to stop.
    rig.current = 0.25f;
    rig.Tap();
    rig.Run(0.49f, 0.25f);
    CHECK(rig.looper.state() == LooperState::kPunch, "single tap -> punch (replace from playhead)");
    rig.looper.SetRecButton(true); rig.looper.SetRecButton(false);
    CHECK(rig.looper.state() == LooperState::kPlaying, "tap after 300 ms -> stop, play");
    CHECK(std::fabs(rig.buf[1000] - 0.25f) < 1e-6f, "punch: replaced region holds new input");
    CHECK(std::fabs(rig.buf[40000] - 0.5f) < 1e-6f, "punch: outside the region untouched");
    rig.looper.Undo();
    CHECK(std::fabs(rig.buf[1000] - 0.5f) < 1e-6f, "undo restores the original");
    rig.looper.Undo();
    CHECK(std::fabs(rig.buf[1000] - 0.25f) < 1e-6f, "undo again = redo");
  }
  {
    Rig rig;
    rig.current = 0.5f;
    rig.Tap(); rig.Run(0.99f, 0.5f); rig.Tap();
    // Long press: hold 0.8 s with input 0.25 -> overdub, sum = 0.75 everywhere
    // written, including the first 500 ms recorded before the gesture was known.
    rig.looper.SetRecButton(true);
    rig.Run(0.8f, 0.25f);
    CHECK(rig.looper.state() == LooperState::kOverdub, "long press -> overdub");
    rig.looper.SetRecButton(false);
    CHECK(rig.looper.state() == LooperState::kPlaying, "release -> stop overdub");
    CHECK(std::fabs(rig.buf[2000] - 0.75f) < 1e-5f, "overdub: first 500 ms summed, not replaced");
    CHECK(std::fabs(rig.buf[30000] - 0.75f) < 1e-5f, "overdub: after 500 ms summed");
    CHECK(std::fabs(rig.buf[45000] - 0.5f) < 1e-5f, "overdub: untouched after release");
  }
  {
    Rig rig;
    rig.current = 0.5f;
    rig.Tap(); rig.Run(0.99f, 0.5f); rig.Tap();
    // Double tap within 300 ms -> replace all; tap after 2 s -> new length 2 s.
    rig.current = 0.25f;
    rig.Tap();
    rig.Run(0.1f, 0.25f);
    rig.Tap();
    CHECK(rig.looper.state() == LooperState::kReplaceAll, "double tap -> replace all");
    rig.Run(1.88f, 0.25f);
    rig.looper.SetRecButton(true); rig.looper.SetRecButton(false);
    CHECK(rig.looper.state() == LooperState::kPlaying, "tap -> stop, play");
    CHECK(std::abs(static_cast<long>(rig.looper.length()) - 96000) <= 48,
          "replace all: new length counts from the first tap (2 s)");
    CHECK(std::fabs(rig.buf[300] - 0.25f) < 1e-6f, "replace all: starts with audio from the first tap");
    rig.looper.Undo();
    CHECK(std::abs(static_cast<long>(rig.looper.length()) - 48000) <= 24, "undo restores old length");
    CHECK(std::fabs(rig.buf[300] - 0.5f) < 1e-6f, "undo restores old content");
  }
  std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
