#!/usr/bin/env bash
# ============================================================================
# tools/mxbmrp3_hud_window/companion_demo.sh
# Open the plugin's REAL in-process companion window and screenshot it, headless.
# Cross-compiles the test DLL, stages the plugin assets next to it (fonts/textures/
# icons + the .ttf under web/fonts/, which the companion window rasterizes), then runs
# companion_demo.exe under Wine on a virtual X display and grabs the window.
#
# Proves the end-to-end feature off the game: the DLL opens its own Win32 window
# and renders its live HUD via core/hud_sw_renderer.
#
# Requires: mingw-w64 (posix), wine64, Xvfb + ImageMagick (`import`).
#   tools/mxbmrp3_hud_window/companion_demo.sh [out.png] [hold_seconds]
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD="${ROOT}/tests/integration/build"
OUT="${1:-${HERE}/companion_window.png}"
HOLD="${2:-12}"
MODE="${3:-}"   # optional scene mode passed to the exe (e.g. "gamepad")

export WINELOADER="${WINELOADER:-/usr/lib/wine/wine64}"
export WINEARCH="${WINEARCH:-win64}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"

for t in x86_64-w64-mingw32-g++ wine Xvfb import; do
    command -v "$t" >/dev/null || { echo "ERROR: '$t' not found"; exit 1; }
done

echo "==> building test DLL"
"${ROOT}/tests/integration/build.sh" >/dev/null

echo "==> staging plugin assets (fonts/textures/icons + web/fonts/*.ttf)"
STAGE="${BUILD}/plugins/mxbmrp3_data"
mkdir -p "${STAGE}/web/fonts"
for d in fonts textures icons; do rm -rf "${STAGE:?}/${d}"; cp -r "${ROOT}/mxbmrp3_data/${d}" "${STAGE}/${d}"; done
cp "${ROOT}/mxbmrp3_data/web/fonts/"*.ttf "${STAGE}/web/fonts/"

echo "==> compiling companion_demo.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O1 -w -static -static-libgcc -static-libstdc++ \
    -I "${ROOT}/tests/integration/harness" -I "${ROOT}/mxbmrp3" -I "${ROOT}/mxbmrp3/vendor" \
    "${HERE}/companion_demo.cpp" -o "${BUILD}/companion_demo.exe" -lws2_32

echo "==> launching under Wine on a virtual display, capturing the window"
export DISPLAY=:99
pkill Xvfb 2>/dev/null || true; sleep 1
Xvfb :99 -screen 0 1200x760x24 >/tmp/mxbmrp3-companion-xvfb.log 2>&1 &
XVFB_PID=$!
sleep 2
( cd "${BUILD}" && timeout $((HOLD + 6)) wine companion_demo.exe mxbmrp3_test.dlo "${HOLD}" "${MODE}" ) \
    >/tmp/mxbmrp3-companion.log 2>&1 &
WINE_PID=$!
sleep 6
import -window root "${OUT}" 2>/dev/null && echo "==> wrote ${OUT}"
wait "${WINE_PID}" 2>/dev/null || true
kill "${XVFB_PID}" 2>/dev/null || true
echo "==> done"
