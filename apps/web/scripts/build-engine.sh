#!/usr/bin/env bash
# Builds the C++ engine to WebAssembly and copies the ES module into the web app.
# Requires Emscripten (brew install emscripten, or emsdk with emcmake on PATH).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$ROOT/engine/build/wasm-web"
emcmake cmake -S "$ROOT/engine" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTRADELAB_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD" --target tradelab_wasm
cp "$BUILD/tradelab.mjs" "$ROOT/apps/web/src/lib/engine/wasm/tradelab.mjs"
echo "engine -> apps/web/src/lib/engine/wasm/tradelab.mjs"
