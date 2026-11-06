#!/usr/bin/env bash
# Launches TradeLab.app the way Finder does (LaunchServices `open`: no terminal, cwd "/", no shell
# environment) with --smoke-test, then checks the report: the menu showed with every named widget and
# action, and Vulkan came from the bundled MoltenVK.
#   packaging/smoke_macos.sh <path to TradeLab.app>
set -euo pipefail
app="$(cd "$(dirname "${1:?TradeLab.app}")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
report="$work/report.json"
open -n -W --env "TRADELAB_DATA_DIR=$work/data" --env TRADELAB_NO_DIALOG=1 --stderr "$work/stderr.txt" \
  "$app" --args --smoke-test "$report"
if [ ! -f "$report" ]; then
  echo "no smoke report; stderr:" >&2
  cat "$work/stderr.txt" >&2 || true
  exit 1
fi
python3 - "$report" "$app" <<'PY'
import json, os, sys
report, app = json.load(open(sys.argv[1])), os.path.realpath(sys.argv[2])
bundled = os.path.join(app, "Contents/Frameworks/libMoltenVK.dylib")
problems = []
if not report["menu"]: problems.append("the menu did not show")
if report["missing"]: problems.append("missing: " + ", ".join(report["missing"]))
if os.path.realpath(report["bundledVulkan"] or "/nonexistent") != bundled: problems.append("bundled MoltenVK not preloaded: %r" % report["bundledVulkan"])
if os.path.realpath(report["vulkan"] or "/nonexistent") != bundled: problems.append("Vulkan came from %r, not the bundle" % report["vulkan"])
if not os.path.realpath(report["executable"]).startswith(app + "/"): problems.append("ran %r" % report["executable"])
print(json.dumps(report))
if problems:
    sys.exit("smoke test failed: " + "; ".join(problems))
print("smoke test passed: menu shown, Vulkan from the bundled MoltenVK")
PY
