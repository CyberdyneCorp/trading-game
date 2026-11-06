#!/usr/bin/env bash
# Wayland smoke test (design "Testing": a Wayland smoke test under headless Weston). Starts Weston with its
# headless backend, runs the app with --smoke-test on that compositor only (no X11 display, Raven told to
# use Wayland, so an X11 fallback cannot hide a Wayland failure) and checks the report: the menu showed
# with every named widget and action. Needs weston, libwayland-client0, libxkbcommon0 and a Vulkan driver
# with Wayland presentation (CI: Mesa lavapipe through VK_ICD_FILENAMES).
#   packaging/smoke_wayland.sh <tradelab executable, or a TradeLab-<arch>.AppImage>
set -euo pipefail
app="$(cd "$(dirname "${1:?executable}")" && pwd)/$(basename "$1")"
work="$(mktemp -d)"
export XDG_RUNTIME_DIR="$work/run"
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless --socket=tradelab-wayland --width=1280 --height=800 --idle-time=0 \
  --log="$work/weston.log" &
weston_pid=$!
trap 'kill $weston_pid 2>/dev/null || true; rm -rf "$work"' EXIT
for _ in $(seq 1 50); do
  [ -S "$XDG_RUNTIME_DIR/tradelab-wayland" ] && break
  sleep 0.1
done
[ -S "$XDG_RUNTIME_DIR/tradelab-wayland" ] || { cat "$work/weston.log"; echo "weston did not start" >&2; exit 1; }

(cd / && env -u DISPLAY WAYLAND_DISPLAY=tradelab-wayland RAVEN_WINDOW_SYSTEM=wayland XDG_SESSION_TYPE=wayland \
  TRADELAB_DATA_DIR="$work/data" TRADELAB_NO_DIALOG=1 TRADELAB_AUDIO=off APPIMAGE_EXTRACT_AND_RUN=1 \
  "$app" --smoke-test "$work/report.json") || { cat "$work/weston.log"; exit 1; }
python3 - "$work/report.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
problems = []
if not report["menu"]: problems.append("the menu did not show")
if report["missing"]: problems.append("missing: " + ", ".join(report["missing"]))
print(json.dumps(report))
if problems:
    sys.exit("Wayland smoke test failed: " + "; ".join(problems))
print("Wayland smoke test passed: menu shown on headless Weston")
PY
