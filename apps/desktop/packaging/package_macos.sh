#!/usr/bin/env bash
# Builds TradeLab.app from a macOS build folder (macos-release or macos-universal preset):
#   Contents/MacOS/tradelab                 the app (Raven + engine, resources embedded)
#   Contents/Frameworks/libMoltenVK.dylib   pinned MoltenVK (packaging/pins.sh), install name @rpath/...
#   Contents/Resources/vulkan/icd.d/MoltenVK_icd.json   for a system Vulkan loader, if one is used
#   Contents/Resources/TradeLab.icns, Info.plist (version from CMakeLists.txt)
#   Contents/Resources/audio/*.mp3          the web's soundtrack (apps/web/static/audio), played with miniaudio
# The app loads the bundled MoltenVK by absolute path at start (platform::preload_bundled_vulkan), so no
# Homebrew MoltenVK is needed.
#
# Signing: with CODESIGN_IDENTITY set (a Developer ID, human-held: tasks 1.21) everything is signed with
# the hardened runtime and packaging/macos/entitlements.plist; otherwise ad-hoc (CI artifacts, local use).
# Notarization is not done here.
#
#   packaging/package_macos.sh <build dir> <out dir>      -> <out dir>/TradeLab.app and TradeLab-macos.zip
# Env: MOLTENVK_DYLIB=<path> uses that dylib instead of the pinned download (offline builds).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
source "$here/pins.sh"
build="$(cd "${1:?build dir}" && pwd)"
out="${2:?out dir}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
cache="${TRADELAB_PACKAGING_CACHE:-$out/.cache}"
version="$(desktop_version)"
app="$out/TradeLab.app"

moltenvk_dir() {
  local tar="$cache/MoltenVK-macos-$MOLTENVK_VERSION.tar" dir="$cache/moltenvk-$MOLTENVK_VERSION"
  fetch_pinned "$MOLTENVK_URL" "$tar" "$MOLTENVK_SHA256"
  if [ ! -f "$dir/libMoltenVK.dylib" ]; then
    rm -rf "$dir.tmp" && mkdir -p "$dir.tmp"
    tar -xf "$tar" -C "$dir.tmp" MoltenVK/MoltenVK/dynamic/dylib/macOS
    mv "$dir.tmp/MoltenVK/MoltenVK/dynamic/dylib/macOS" "$dir" && rm -rf "$dir.tmp"
  fi
  echo "$dir"
}

make_icns() {
  local png="$here/../../web/static/icons/icon-512.png" set="$out/.TradeLab.iconset"
  rm -rf "$set" && mkdir -p "$set"
  for size in 16 32 128 256; do
    sips -z $size $size "$png" --out "$set/icon_${size}x${size}.png" >/dev/null
    sips -z $((size * 2)) $((size * 2)) "$png" --out "$set/icon_${size}x${size}@2x.png" >/dev/null
  done
  cp "$png" "$set/icon_512x512.png"
  iconutil -c icns "$set" -o "$app/Contents/Resources/TradeLab.icns"
  rm -rf "$set"
}

sign() {
  if [ -n "${CODESIGN_IDENTITY:-}" ]; then
    codesign --force --timestamp --options runtime --entitlements "$here/macos/entitlements.plist" -s "$CODESIGN_IDENTITY" "$1"
  else
    codesign --force -s - "$1"
  fi
}

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/Resources/vulkan/icd.d"
cp "$build/tradelab" "$app/Contents/MacOS/tradelab"
if [ -n "${MOLTENVK_DYLIB:-}" ]; then
  cp "$MOLTENVK_DYLIB" "$app/Contents/Frameworks/libMoltenVK.dylib"
else
  cp "$(moltenvk_dir)/libMoltenVK.dylib" "$app/Contents/Frameworks/libMoltenVK.dylib"
fi
chmod u+w "$app/Contents/Frameworks/libMoltenVK.dylib"
install_name_tool -id @rpath/libMoltenVK.dylib "$app/Contents/Frameworks/libMoltenVK.dylib" 2>/dev/null
cat > "$app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
  "file_format_version": "1.0.0",
  "ICD": { "library_path": "../../../Frameworks/libMoltenVK.dylib", "api_version": "1.4.0", "is_portability_driver": true }
}
JSON
mkdir -p "$app/Contents/Resources/audio"
cp "$here/../../web/static/audio/"*.mp3 "$app/Contents/Resources/audio/"
sed "s/@VERSION@/$version/g" "$here/macos/Info.plist.in" > "$app/Contents/Info.plist"
plutil -lint "$app/Contents/Info.plist" >/dev/null
make_icns

# Every slice of the app must find a matching MoltenVK slice.
for arch in $(lipo -archs "$app/Contents/MacOS/tradelab"); do
  lipo -archs "$app/Contents/Frameworks/libMoltenVK.dylib" | grep -qw "$arch" || {
    echo "MoltenVK has no $arch slice" >&2
    exit 1
  }
done

sign "$app/Contents/Frameworks/libMoltenVK.dylib"
sign "$app"
codesign --verify --strict --deep "$app"
(cd "$out" && rm -f TradeLab-macos.zip && ditto -c -k --keepParent TradeLab.app TradeLab-macos.zip)
echo "$app ($version, $(lipo -archs "$app/Contents/MacOS/tradelab"))"
