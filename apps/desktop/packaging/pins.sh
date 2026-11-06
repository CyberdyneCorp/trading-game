# Pinned third-party packaging inputs (sourced by the packaging scripts). Each one is downloaded by URL
# and refused unless its SHA-256 matches; bump a pin in a PR of its own and re-run the package smoke tests.

# MoltenVK (Vulkan on Metal), universal dylib, bundled in TradeLab.app/Contents/Frameworks.
MOLTENVK_VERSION=v1.4.2
MOLTENVK_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/${MOLTENVK_VERSION}/MoltenVK-macos.tar"
MOLTENVK_SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e

# appimagetool and the AppImage type 2 runtime it embeds (passed with --runtime-file, never fetched by
# the tool itself).
APPIMAGETOOL_VERSION=1.9.1
APPIMAGE_RUNTIME_VERSION=20251108
appimagetool_url() { echo "https://github.com/AppImage/appimagetool/releases/download/${APPIMAGETOOL_VERSION}/appimagetool-$1.AppImage"; }
appimage_runtime_url() { echo "https://github.com/AppImage/type2-runtime/releases/download/${APPIMAGE_RUNTIME_VERSION}/runtime-$1"; }
appimagetool_sha256() {
  case "$1" in
    x86_64) echo ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 ;;
    aarch64) echo f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158 ;;
  esac
}
appimage_runtime_sha256() {
  case "$1" in
    x86_64) echo 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d ;;
    aarch64) echo 00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 ;;
  esac
}

# Downloads $1 to $2 (kept as a cache) and checks it against SHA-256 $3; a mismatch deletes the file.
fetch_pinned() {
  local url="$1" out="$2" want="$3" got
  if [ ! -f "$out" ]; then
    mkdir -p "$(dirname "$out")"
    curl -sSfL --retry 3 -o "$out.part" "$url"
    mv "$out.part" "$out"
  fi
  if command -v sha256sum >/dev/null; then got=$(sha256sum "$out" | cut -d' ' -f1); else got=$(shasum -a 256 "$out" | cut -d' ' -f1); fi
  if [ "$got" != "$want" ]; then
    echo "pinned download $url: SHA-256 $got, expected $want" >&2
    rm -f "$out"
    return 1
  fi
}

# The app version: project(tradelab_desktop VERSION x.y.z) in apps/desktop/CMakeLists.txt.
desktop_version() {
  sed -n 's/^project(tradelab_desktop VERSION \([0-9.]*\).*/\1/p' "$(dirname "${BASH_SOURCE[0]}")/../CMakeLists.txt"
}
