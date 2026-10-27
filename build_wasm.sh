#!/bin/sh
# Builds the engine as WebAssembly for the web prototype.
set -e
python3 tools/patch_rings.py >/dev/null
# Rings and stmlib: tools/fetch_vendor.sh puts them in vendor/eurorack.
# WebAssembly: clang 17 or newer, plus the WASI sysroot and the wasm32 builtins
# (wasi-sdk has both); set WASI_SYSROOT and CLANG_RESOURCE if they live elsewhere.
MI=${MI:-vendor/eurorack}
WASI_SYSROOT=${WASI_SYSROOT:-vendor/wasi/wasi-sysroot}
CLANG_RESOURCE=${CLANG_RESOURCE:-vendor/wasi/resource}
CXX=${CXX:-clang++}
mkdir -p build
$CXX --target=wasm32-wasi --sysroot=$WASI_SYSROOT -resource-dir=$CLANG_RESOURCE \
  -O2 -std=c++17 -fno-exceptions -fno-rtti -DTEST -DNDEBUG -I$MI -Isrc \
  -mexec-model=reactor -Wl,--strip-all -Wl,--initial-memory=67108864 -Wl,--max-memory=268435456 \
  -Wno-unused-local-typedefs \
  web/wasm_api.cc src/engine.cc src/granular_looper.cc src/resonator.cc src/vocoder.cc src/delay.cc \
  build/patched/part.cc $MI/rings/dsp/resonator.cc $MI/rings/dsp/string.cc \
  $MI/rings/dsp/fm_voice.cc $MI/rings/resources.cc \
  $MI/stmlib/dsp/units.cc $MI/stmlib/utils/random.cc \
  -o build/engine.wasm
ls -la build/engine.wasm
