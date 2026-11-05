#!/usr/bin/env bash
# Compiles resources/shaders/crt.frag to SPIR-V (resources/shaders/crt.spv, checked in so builds need no
# shader compiler) and records the source's SHA-256 in crt.spv.sha256, which the `script` ctest
# (tests/scripts/test_shaders.py) compares with crt.frag: editing the shader without rebuilding fails.
#   scripts/build_shaders.sh <raven source dir>    (the pinned Raven checkout: include/raven/shaders)
# Needs glslangValidator (brew install glslang / apt install glslang-tools).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
raven="${1:?usage: build_shaders.sh <raven source dir>}"
glslang="${GLSLANG:-glslangValidator}"
src="$here/resources/shaders/crt.frag"
out="$here/resources/shaders/crt.spv"
"$glslang" -V --quiet -I"$raven/include/raven/shaders" "$src" -o "$out"
if command -v spirv-val >/dev/null; then spirv-val "$out"; fi
python3 - "$src" "$out.sha256" <<'EOF'
import hashlib, sys
digest = hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest()
open(sys.argv[2], 'w').write(f"{digest}  crt.frag\n")
EOF
echo "wrote $out"
