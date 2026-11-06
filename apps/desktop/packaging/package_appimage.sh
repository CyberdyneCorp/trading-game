#!/usr/bin/env bash
# Builds TradeLab-<arch>.AppImage from a Linux build folder (linux-release preset), with the pinned
# appimagetool and type 2 runtime (packaging/pins.sh). Run it in the baseline container (ubuntu:24.04,
# as CI does): the binary needs that glibc/libstdc++ or newer. Nothing else is bundled: libX11,
# libvulkan.so.1 and the GPU driver come from the system (like any Vulkan app), libcurl is opened at run
# time when present.
#   packaging/package_appimage.sh <build dir> <out dir>      -> <out dir>/TradeLab-<arch>.AppImage
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/pins.sh"
build="$(cd "${1:?build dir}" && pwd)"
out="${2:?out dir}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
cache="${TRADELAB_PACKAGING_CACHE:-$out/.cache}"
arch="$(uname -m)"
case "$arch" in
  x86_64 | aarch64) ;;
  arm64) arch=aarch64 ;;
  *) echo "unsupported architecture $arch" >&2; exit 1 ;;
esac

tool="$cache/appimagetool-$APPIMAGETOOL_VERSION-$arch.AppImage"
runtime="$cache/runtime-$APPIMAGE_RUNTIME_VERSION-$arch"
fetch_pinned "$(appimagetool_url "$arch")" "$tool" "$(appimagetool_sha256 "$arch")"
fetch_pinned "$(appimage_runtime_url "$arch")" "$runtime" "$(appimage_runtime_sha256 "$arch")"
chmod +x "$tool"

appdir="$out/TradeLab.AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/512x512/apps"
install -m 755 "$build/tradelab" "$appdir/usr/bin/tradelab"
strip "$appdir/usr/bin/tradelab" 2>/dev/null || true
install -m 644 "$here/linux/tradelab.desktop" "$appdir/tradelab.desktop"
install -m 644 "$here/linux/tradelab.desktop" "$appdir/usr/share/applications/tradelab.desktop"
install -m 644 "$here/../../web/static/icons/icon-512.png" "$appdir/tradelab.png"
install -m 644 "$here/../../web/static/icons/icon-512.png" "$appdir/usr/share/icons/hicolor/512x512/apps/tradelab.png"
cat > "$appdir/AppRun" <<'SH'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
exec "$here/usr/bin/tradelab" "$@"
SH
chmod 755 "$appdir/AppRun"
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$appdir/tradelab.desktop"; fi

image="$out/TradeLab-$arch.AppImage"
rm -f "$image"
# No FUSE in containers: the tool runs extracted.
ARCH="$arch" APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$image" >/dev/null
rm -rf "$appdir"
echo "$image ($(desktop_version), $arch, $(du -h "$image" | cut -f1))"
