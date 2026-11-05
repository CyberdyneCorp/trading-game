#!/usr/bin/env bash
# Runs the AppImage with --smoke-test under the caller's display (CI: xvfb-run with lavapipe) and checks
# the report: the menu showed with every named widget and action, from the AppImage's own binary, with
# the system Vulkan loader.
#   packaging/smoke_appimage.sh <TradeLab-<arch>.AppImage>
set -euo pipefail
image="$(cd "$(dirname "${1:?AppImage}")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
chmod +x "$image"
# Without FUSE (containers) the runtime extracts itself to a temp folder and runs from there.
(cd / && TRADELAB_DATA_DIR="$work/data" TRADELAB_NO_DIALOG=1 APPIMAGE_EXTRACT_AND_RUN=1 \
  "$image" --smoke-test "$work/report.json")
python3 - "$work/report.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
problems = []
if not report["menu"]: problems.append("the menu did not show")
if report["missing"]: problems.append("missing: " + ", ".join(report["missing"]))
if "libvulkan.so" not in report["vulkan"]: problems.append("no system Vulkan loader: %r" % report["vulkan"])
if not report["executable"].endswith("/usr/bin/tradelab"): problems.append("ran %r" % report["executable"])
print(json.dumps(report))
if problems:
    sys.exit("smoke test failed: " + "; ".join(problems))
print("smoke test passed: menu shown from the AppImage")
PY
