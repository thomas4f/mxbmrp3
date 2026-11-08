#!/usr/bin/env bash
# ============================================================================
# tools/demo_reel/make_demo.sh
# Shoot the hover-preview demo: a Recorder tape played into the test DLL while
# the companion window renders it, with only the mouse scripted (hover_reel.cpp).
# The companion frame is captured straight from the plugin (no screen grab) at
# 1920x1080, 30 fps, and composited on a backdrop colour into an mp4 and a GIF,
# with a caption naming what each beat demonstrates (captions_ass.py).
#
#   tools/demo_reel/make_demo.sh [out.mp4] [liveSecs]
#   (out.mp4 defaults into the git-ignored companion_demo_run/ build folder)
#
#   TAPE=race.tape(.gz)  play this tape (a real one from [Recorder] enabled=1)
#                        instead of tapegen's made-up race
#   BACKDROP=3d5a80      backdrop colour (hex)
#   LAYOUT=toc           the game frame at 1600x900 (rendered at that size, not
#                        scaled) right of a contents column listing every beat,
#                        the current one marked with its detail line
#   LAYOUT=caption       the game frame full size, one caption in a box the reel
#                        checks stays clear (fails on any overlap)
#   REEL_STRIP=main      rest on the main strip switches only (default: every one that previews)
#   REEL_KEEP_THEME=..   the theme kept after the themes beat (default Modern Dark)
#   VERSION=1.35         the version under the contents title (default resource.h's)
#   --no-build           reuse the existing test DLL
#
# The tape's last liveSecs (default 165, the length of the mouse script; 130
# with REEL_STRIP=main) play in real time; everything before them plays at
# once, so the HUDs open with the history the tape carries. A real tape wants:
# unthinned telemetry, several riders (some near you at times), 5+ laps with
# splits.
#
# Requires what tools/hud_window/companion_demo.sh does (mingw-w64, wine64, Xvfb)
# plus ffmpeg. The DLL needs the demo hooks in core/test_hooks_toggle_strip.cpp
# (MXBMRP3_Test_CompanionCaptureRgba, MXBMRP3_Test_FriendsInject,
# MXBMRP3_Test_FakeGamepadInput); test builds only.
# ============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUNDIR="${ROOT}/tests/integration/build/companion_demo_run"
BUILD=1
ARGS=()
for a in "$@"; do if [ "$a" = "--no-build" ]; then BUILD=0; else ARGS+=("$a"); fi; done
OUT="${ARGS[0]:-${RUNDIR}/demo.mp4}"
DEFAULT_LIVE=165; [ "${REEL_STRIP:-}" = main ] && DEFAULT_LIVE=130
LIVE="${ARGS[1]:-${DEFAULT_LIVE}}"
BACKDROP="${BACKDROP:-3d5a80}"
LAYOUT="${LAYOUT:-toc}"
PAGE=2b405c   # the toc layout's page around the frame
if [ "${LAYOUT}" = toc ]; then FW=1600; FH=900; FX=320; FY=90; else FW=1920; FH=1080; FX=0; FY=0; fi
VERSION="${VERSION:-$(awk '/#define VER_MAJOR/{a=$3} /#define VER_MINOR/{b=$3} END{print a"."b}' "${ROOT}/mxbmrp3/resource.h")}"

if [ "${BUILD}" = 1 ]; then
    echo "==> building the test DLL"
    CCACHE_DISABLE=1 "${ROOT}/tests/integration/build.sh" >/tmp/demo_reel_build.log 2>&1
fi
echo "==> staging the plugin assets"
# Only its staging is used here: its own screenshot can come back blank under a
# busy Xvfb, which says nothing about the reel's capture (straight from the renderer).
MXBMRP3_DEMO_NO_BUILD=1 "${ROOT}/tools/hud_window/companion_demo.sh" /tmp/demo_reel_stage.png >/tmp/demo_reel_stage.log 2>&1 || true
[ -d "${RUNDIR}/plugins/mxbmrp3_data/themes" ] || { echo "ERROR: staging failed, see /tmp/demo_reel_stage.log" >&2; exit 1; }
ln -f "${ROOT}/tests/integration/build/mxbmrp3_test.dlo" "${RUNDIR}/mxbmrp3_test.dlo"
# The language packs, for the Language row (companion_demo.sh leaves them out, so
# its screenshots never depend on the host's Windows language).
rm -rf "${RUNDIR}/plugins/mxbmrp3_data/languages"
cp -r "${ROOT}/mxbmrp3_data/languages" "${RUNDIR}/plugins/mxbmrp3_data/languages"

echo "==> compiling tapegen and the reel"
CXX=(x86_64-w64-mingw32-g++ -std=c++20 -O1 -w -static -static-libgcc -static-libstdc++
     -I "${ROOT}/tests/integration/harness" -I "${ROOT}/mxbmrp3" -I "${ROOT}/mxbmrp3/vendor")
"${CXX[@]}" "${ROOT}/tools/demo_reel/tapegen.cpp" -o "${RUNDIR}/tapegen.exe" -lws2_32
"${CXX[@]}" "${ROOT}/tools/demo_reel/hover_reel.cpp" -o "${RUNDIR}/showreel.exe" -lws2_32

if [ -n "${TAPE:-}" ]; then
    echo "==> using ${TAPE}"
    case "${TAPE}" in *.gz) gunzip -c "${TAPE}" > "${RUNDIR}/demo.tape" ;; *) cp "${TAPE}" "${RUNDIR}/demo.tape" ;; esac
else
    echo "==> writing the made-up race"
    (cd "${RUNDIR}" && wine tapegen.exe mxbmrp3_test.dlo demo.tape 4.5 "${LIVE}" 2>&1 | grep wrote)
fi

# Settings the reel starts from: a fresh install, strip folded, Achievements and
# Friends off (they only preview), the companion window full HD.
W=$((FW + 8)); H=$((FH + 34))   # the window with Wine's frame
SAVE=/tmp/mxbmrp3-tests/companion
rm -rf "${SAVE}"
mkdir -p "${SAVE}/mxbmrp3"
cat > "${SAVE}/mxbmrp3/mxbmrp3_settings.ini" <<INI
[Settings]
version=4

[Updates]
updateMode=off

[Display]
companionWindowX=0
companionWindowY=0
companionWindowW=${W}
companionWindowH=${H}
togglesCollapsed=1

[Achievements]
visible=0

[FriendsHud]
visible=0
INI

echo "==> playing the tape (about $((LIVE + 30)) s)"
export DISPLAY=:99
pkill -f "Xvfb :99" 2>/dev/null || true   # only the display this script uses
wineserver -k 2>/dev/null || true
sleep 1
Xvfb :99 -screen 0 "1928x1114x24" >/tmp/demo_reel_xvfb.log 2>&1 &
XVFB=$!
sleep 2
# The reel writes full-HD RGBA frames into a FIFO; ffmpeg lays them on the
# backdrop and encodes as they arrive. That live pass is a fast lossless one: a
# slow encoder there takes the CPU the reel needs, and the capture repeats
# frames to keep time (the judder this used to show). The slow, small encode
# runs afterwards from the lossless file.
rm -f "${RUNDIR}/capture.rgba" "${RUNDIR}/captions.tsv" "${RUNDIR}/overlap.rgba"
mkfifo "${RUNDIR}/capture.rgba"
# The reel first writes PREROLL warm-up frames, trimmed off here.
PREROLL=150
ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s ${FW}x${FH} -r 30 -i "${RUNDIR}/capture.rgba" \
    -f lavfi -i "color=c=0x${PAGE}:s=1920x1080:r=30" \
    -filter_complex "[1]drawbox=x=${FX}:y=${FY}:w=${FW}:h=${FH}:color=0x${BACKDROP}:t=fill[b];[0]trim=start_frame=${PREROLL},setpts=PTS-STARTPTS[v];[b][v]overlay=x=${FX}:y=${FY}:format=auto:shortest=1" \
    -c:v libx264 -preset ultrafast -qp 0 -pix_fmt yuv420p "${RUNDIR}/live.mkv" &
FF=$!
CHECK=; [ "${LAYOUT}" = caption ] && CHECK=1
(cd "${RUNDIR}" && REEL_CAPTURE=1 REEL_PREROLL=${PREROLL} REEL_W=${FW} REEL_H=${FH} \
    REEL_CAPTION_CHECK=${CHECK} timeout $((LIVE + 600)) wine showreel.exe mxbmrp3_test.dlo demo.tape "${LIVE}") \
    >/tmp/demo_reel.log 2>&1 || true
wait "${FF}" || true
wineserver -k 2>/dev/null || true
kill "${XVFB}" 2>/dev/null || true
grep -E "preview|captured|not found|caption" /tmp/demo_reel.log || true
if grep -qE "caption overlap: [1-9]" /tmp/demo_reel.log; then
    ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgba -s ${FW}x${FH} -i "${RUNDIR}/overlap.rgba" "${OUT%.mp4}-overlap.png" || true
    echo "ERROR: something was drawn under a caption (first frame: ${OUT%.mp4}-overlap.png); move that beat's box" >&2
    exit 1
fi
python3 "${ROOT}/tools/demo_reel/captions_ass.py" "${RUNDIR}/captions.tsv" "${RUNDIR}/captions.ass" "${OUT%.mp4}.srt" \
    --layout "${LAYOUT}" --frame "${FX},${FY},${FW},${FH}" --version "${VERSION}"
# veryslow + tune animation suits the flat UI: crf 26 measured no visible loss
# (text zoomed 2x, SSIM 0.9989) at ~3.8 MB for 116 s, against 10 MB at crf 14.
# H.264, not H.265: browsers must play it inline on GitHub.
ffmpeg -y -loglevel error -i "${RUNDIR}/live.mkv" \
    -vf "ass=${RUNDIR}/captions.ass:fontsdir=${ROOT}/mxbmrp3_data/web/fonts" -c:v libx264 -preset veryslow -tune animation -crf 26 \
    -pix_fmt yuv420p -movflags +faststart "${OUT}"
rm -f "${RUNDIR}/live.mkv"
# A light GIF (~3 MB for 2 min, 6 fps, 64 colours) for where video does not
# play; the mp4 is the full-quality reel.
ffmpeg -y -loglevel error -i "${OUT}" \
    -vf "fps=6,scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    "${OUT%.mp4}.gif"
rm -f "${RUNDIR}/capture.rgba"
echo "==> wrote ${OUT}, ${OUT%.mp4}.gif and ${OUT%.mp4}.srt"
