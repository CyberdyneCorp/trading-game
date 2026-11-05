#!/usr/bin/env bash
# Cognitive complexity <= 15 per C++ function (clang-tidy readability-function-cognitive-complexity),
# over the app sources and the tests. Usage: scripts/check_complexity.sh <build dir with compile_commands.json>
set -euo pipefail
cd "$(dirname "$0")/.."
build="${1:?build directory}"
extra=()
if [[ "$(uname)" == "Darwin" ]]; then extra+=("--extra-arg=-isysroot$(xcrun --show-sdk-path)"); fi
files=(src/*/*.cpp src/*/*/*.cpp src/main.cpp tests/{model,viewmodel,headless,windowed,support,storage,packs}/*.cpp)
"${CLANG_TIDY:-clang-tidy}" -p "$build" --quiet "${extra[@]}" \
  --checks='-*,readability-function-cognitive-complexity' \
  --config='{CheckOptions: {readability-function-cognitive-complexity.Threshold: 15, readability-function-cognitive-complexity.IgnoreMacros: true}}' \
  --warnings-as-errors='*' "${files[@]}"
echo "cognitive complexity: every function <= 15"
