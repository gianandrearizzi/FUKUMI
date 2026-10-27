MI      ?= vendor/eurorack
CXX     ?= g++
CXXFLAGS = -O2 -std=c++17 -DTEST -I$(MI) -Isrc -Wall -Wno-unused-parameter

RINGS = build/patched/part.cc $(MI)/rings/dsp/resonator.cc $(MI)/rings/dsp/string.cc \
        $(MI)/rings/dsp/fm_voice.cc $(MI)/rings/resources.cc \
        $(MI)/stmlib/dsp/units.cc $(MI)/stmlib/utils/random.cc
ENGINE = src/granular_looper.cc src/resonator.cc src/vocoder.cc src/delay.cc

all: build/patched/part.cc build/test_gestures build/test_delay build/render_demo build/render_vocoder

build/patched/part.cc: tools/patch_rings.py
	python3 tools/patch_rings.py

build/test_gestures: test/test_gestures.cc src/granular_looper.cc src/granular_looper.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test/test_gestures.cc src/granular_looper.cc -o $@

build/test_delay: test/test_delay.cc src/delay.cc src/delay.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test/test_delay.cc src/delay.cc -o $@

build/render_demo: test/render_demo.cc $(ENGINE) src/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test/render_demo.cc $(ENGINE) $(RINGS) -o $@

build/render_vocoder: test/render_vocoder.cc $(ENGINE) src/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test/render_vocoder.cc $(ENGINE) $(RINGS) -o $@

test: build/test_gestures build/test_delay
	./build/test_gestures
	./build/test_delay

demo: build/render_demo
	@mkdir -p out
	./build/render_demo out

clean:
	rm -rf build out
