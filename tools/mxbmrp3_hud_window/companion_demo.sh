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
#   tools/mxbmrp3_hud_window/companion_demo.sh [out.png] [hold_seconds] [mode...]
#
# EXTRA_INI appends raw lines to the seeded settings, for comparing one setting
# across captures without hand-editing this script:
#   EXTRA_INI=$'[Fonts]\nnormal=Tiny5-Regular' companion_demo.sh out.png
#
# Screenshot resolution: SHOT_RES (default 1920x1080). The capture resolution IS
# the companion window's render resolution — the window restores its size from
# the [Display] companionWindowW/H settings, so this script seeds those into the
# scenario's settings INI and sizes the virtual X display to match. Without the
# seed the window opens at the plugin's default 980x560 and every capture is
# sub-1080p no matter how big the X screen is.
#   SHOT_RES=2560x1440 tools/mxbmrp3_hud_window/companion_demo.sh out.png
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD="${ROOT}/tests/integration/build"

# $1 is an OUTPUT PATH, so a habitual `--help` would otherwise be taken as one and
# screenshotted into a file literally named `--help` -- which is how a 12MB
# ImageMagick dump got committed once. Answer the flag instead, and reject any
# other dash-leading $1 rather than writing to it. --verify-deterministic is the
# one real flag in this slot and is handled further down, so it must pass through.
case "${1:-}" in
-h | --help | help)
    sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
    exit 0
    ;;
--verify-deterministic) ;;
-*)
    echo "ERROR: '$1' is not an option -- \$1 is the output path." >&2
    echo "       Try: ${BASH_SOURCE[0]} --help" >&2
    exit 2
    ;;
esac

OUT="${1:-${HERE}/companion_window.png}"
# Default raised from 12 after a same-hold non-determinism was measured at BOTH 6
# and 12 (on different machines), and none at 25. The seeded updateMode=off below
# is the actual fix -- this is the belt to its braces, and it costs only wall time.
DEFAULT_HOLD=25
HOLD="${2:-${DEFAULT_HOLD}}"
# Args from $3 on are passed through to the exe: a scene mode ("gamepad", "gear",
# "timing", "eventlog", "close") or a settings tab ("tab Map", "tab Timing", ...).
SHOT_RES="${SHOT_RES:-1920x1080}"
SHOT_W="${SHOT_RES%x*}"; SHOT_H="${SHOT_RES#*x}"

# Shared with the test scripts (prefix / arch / debug); see its header.
. "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../tests/integration" && pwd)/wine_env.sh"
mxb_wine_env
# Extra here only: this runs the real window under Xvfb, which needs the loader
# resolved by absolute path rather than via the argv[0]-sniffing launcher.
export WINELOADER="${WINELOADER:-/usr/lib/wine/wine64}"

for t in x86_64-w64-mingw32-g++ wine Xvfb import; do
    command -v "$t" >/dev/null || { echo "ERROR: '$t' not found"; exit 1; }
done

# --------------------------------------------------------------------------
# --verify-deterministic [hold] [mode...]     (VERIFY_N captures, default 3)
#
# Captures the same scene N times and requires every capture to be identical.
# Exists because "this capture is reproducible" was twice believed on the strength
# of runs that happened to agree, and twice wrong -- the same reason
# check_hud_raw_cache.sh grew a --self-test.
#
# IT IS A SCREEN, NOT A PROOF, and the difference is measured rather than
# theoretical: at N=2 this reported AE=0 for a scene that was demonstrably NOT
# reproducible -- a later capture of that same scene differed by 10,654 px. Both
# samples had simply landed on the same side of a repaint race. N=3 is a cheap
# improvement on that, not a fix for it; a pass means "no divergence seen in N
# runs", never "this scene cannot race".
#
# So when a cross-tree diff comes back non-zero, RE-CAPTURE BOTH SIDES before
# believing it. A stale capture that caught the race reads exactly like a
# rendering regression, which is how a 10,839 px "regression" here turned out to
# be 185 px of version string plus one flaky frame.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--verify-deterministic" ]]; then
    command -v compare >/dev/null || { echo "ERROR: 'compare' (ImageMagick) not found"; exit 1; }
    shift
    # Hold is optional, but it CANNOT be left empty when re-invoking: the recursive
    # call passes positionally, so an omitted hold would let the first mode word land
    # in the hold slot ("--verify-deterministic tab Map" would run with hold="tab" and
    # mode "Map"). Fall back to the same default the script itself uses.
    # DEFAULT_HOLD, not ${HOLD}: in this branch $2 is the first MODE word, not a hold
    # ("--verify-deterministic tab Map" makes HOLD="tab" up at the top), so inheriting
    # HOLD reintroduces the very misroute this guards against.
    vhold="${DEFAULT_HOLD}"
    if [[ "${1:-}" =~ ^[0-9]+$ ]]; then vhold="$1"; shift; fi
    vn="${VERIFY_N:-3}"
    vtmp=$(mktemp -d); trap 'rm -rf "${vtmp}"' EXIT
    echo "==> determinism screen: capturing the same scene ${vn}x at hold ${vhold}s: ${*:-default}"
    for ((i = 1; i <= vn; i++)); do
        "${BASH_SOURCE[0]}" "${vtmp}/cap${i}.png" "${vhold}" "$@" >/dev/null || {
            echo "DETERMINISM SCREEN FAILED: capture ${i} did not produce a frame." >&2
            exit 1
        }
    done
    for ((i = 2; i <= vn; i++)); do
        vae=$(compare -metric AE "${vtmp}/cap1.png" "${vtmp}/cap${i}.png" null: 2>&1 || true)
        if [[ "${vae}" != "0" ]]; then
            echo "DETERMINISM SCREEN FAILED: captures 1 and ${i} differ by ${vae} pixels." >&2
            echo "       A pixel diff of this scene cannot tell a code change from this noise." >&2
            echo "       Something async repaints mid-capture. Quiesce it in the seeded settings" >&2
            echo "       INI above rather than raising the hold, which only moves the window." >&2
            exit 1
        fi
    done
    echo "==> no divergence across ${vn} captures (not a guarantee — see the header)"
    exit 0
fi

# Pin the 4th version component. It is normally the git commit count, so two
# checkouts of the same code differ in it by construction -- and every capture
# showing the version then differs across branches no matter what the renderer
# does. Pinning removes that floor at the source instead of teaching readers to
# subtract it. Captures are only comparable to other captures anyway.
export MXBMRP3_VER_BUILD=0

echo "==> building test DLL"
"${ROOT}/tests/integration/build.sh" >/dev/null

# Assets are staged into a SANDBOX, never into ${BUILD} itself, and the demo runs
# from there. The plugin resolves plugins/mxbmrp3_data relative to the working
# directory, and ${BUILD} is also the cwd every Wine integration test runs in — so
# staging there leaves icons+fonts lying around that the next `run_tests.sh` picks
# up. That is not hypothetical: with icons present the settings panel draws tab
# ICONS where it otherwise falls back to "[x]"/"[ ]" TEXT, which emits 23 fewer
# strings for the same click regions and fails settings_layout_test's golden —
# looking exactly like a code regression in a diff that never touched the panel.
RUNDIR="${BUILD}/companion_demo_run"
STAGE="${RUNDIR}/plugins/mxbmrp3_data"
rm -rf "${BUILD:?}/plugins"   # self-heal a checkout polluted by an older run
echo "==> staging plugin assets into ${RUNDIR} (fonts/textures/icons + web/fonts/*.ttf)"
mkdir -p "${STAGE}/web/fonts"
for d in fonts textures icons; do rm -rf "${STAGE:?}/${d}"; cp -r "${ROOT}/mxbmrp3_data/${d}" "${STAGE}/${d}"; done
cp "${ROOT}/mxbmrp3_data/web/fonts/"*.ttf "${STAGE}/web/fonts/"
# Hardlink rather than copy: same inode, so it tracks the DLL just rebuilt above.
ln -f "${BUILD}/mxbmrp3_test.dlo" "${RUNDIR}/mxbmrp3_test.dlo"

# PRECONDITION: the assets have to be there, because a capture rendering WITHOUT
# them is the failure mode nothing else catches. Icons silently fall back to
# "[x]"/"[ ]" text -- measured at 72,784 differing pixels for one scene -- and the
# result is a fully-formed, plausible-looking frame that sails through the
# blank-frame guard below (it is nowhere near uniform, so the stddev test is
# happy). Blankness is the obvious failure; this is the quiet one.
for d in fonts textures icons; do
    if [[ -z "$(ls -A "${STAGE}/${d}" 2>/dev/null)" ]]; then
        echo "ERROR: asset staging failed — ${STAGE}/${d} is empty." >&2
        echo "       A capture without assets renders text fallbacks and looks fine." >&2
        exit 1
    fi
done

echo "==> compiling companion_demo.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O1 -w -static -static-libgcc -static-libstdc++ \
    -I "${ROOT}/tests/integration/harness" -I "${ROOT}/mxbmrp3" -I "${ROOT}/mxbmrp3/vendor" \
    "${HERE}/companion_demo.cpp" -o "${RUNDIR}/companion_demo.exe" -lws2_32

echo "==> seeding [Display] window geometry (${SHOT_RES}) into the scenario settings"
# The window opens at its last-saved rect; seed it so the render (and therefore
# the capture) is exactly SHOT_RES instead of the 980x560 default. The demo's
# save path is Z:\tmp\mxbmrp3-tests\companion\ (see companion_demo.cpp).
SAVE=/tmp/mxbmrp3-tests/companion/mxbmrp3
mkdir -p "${SAVE}"
# updateMode=off removes ONE async source. It is worth having and it is NOT a fix:
# a repaint over the panel title (~10,654 px) was still observed with this seeded,
# so something else async is still in play and this scene is not yet reliably
# diffable. Do not upgrade this comment to "quiescent" without evidence.
#
# What IS established: the repaint is not a function of the hold. Two runs at the
# SAME hold differed by 10,654 px -- at hold 6 on one machine, at hold 12 on
# another -- so raising the hold only moves which machine it bites. Anything else
# async that repaints belongs in this seed as it is identified.
cat > "${SAVE}/mxbmrp3_settings.ini" <<INI
[Settings]
version=4

[Updates]
updateMode=off

[Display]
companionWindowX=0
companionWindowY=0
companionWindowW=${SHOT_W}
companionWindowH=${SHOT_H}
${EXTRA_INI:-}
INI

echo "==> launching under Wine on a virtual display, capturing the window"
export DISPLAY=:99
pkill Xvfb 2>/dev/null || true; sleep 1
Xvfb :99 -screen 0 "${SHOT_W}x${SHOT_H}x24" >/tmp/mxbmrp3-companion-xvfb.log 2>&1 &
XVFB_PID=$!
sleep 2
( cd "${RUNDIR}" && timeout $((HOLD + 6)) wine companion_demo.exe mxbmrp3_test.dlo "${HOLD}" "${@:3}" ) \
    >/tmp/mxbmrp3-companion.log 2>&1 &
WINE_PID=$!
sleep 6
# Capture, then PROVE the frame has content. `import` exits 0 for an all-black
# grab -- the window had not mapped yet, or the plugin failed to open it -- so a
# bare `import && echo wrote` reports success for a blank PNG, and a pixel diff
# of two blanks reports "identical" just as cheerfully. Standard deviation over
# the image is the cheap discriminator: a real HUD frame is nowhere near uniform.
# Retry rather than fail on the first blank; window mapping under Xvfb is racy.
capture_ok=0
for attempt in 1 2 3; do
    import -window root "${OUT}" 2>/dev/null || true
    if [[ -s "${OUT}" ]]; then
        sd=$(identify -format "%[standard-deviation]" "${OUT}" 2>/dev/null || echo 0)
        # Integer-compare via awk: ImageMagick reports this as a float.
        if awk -v v="${sd}" 'BEGIN { exit !(v > 1.0) }'; then
            capture_ok=1
            break
        fi
    fi
    echo "==> capture ${attempt} was blank (stddev ${sd:-n/a}); retrying"
    sleep 3
done

wait "${WINE_PID}" 2>/dev/null || true
kill "${XVFB_PID}" 2>/dev/null || true

if [[ "${capture_ok}" -ne 1 ]]; then
    rm -f "${OUT}"   # never leave a blank behind for someone to diff against
    echo "ERROR: captured only blank frames — the companion window never rendered." >&2
    echo "       See /tmp/mxbmrp3-companion.log and /tmp/mxbmrp3-companion-xvfb.log." >&2
    exit 1
fi
echo "==> wrote ${OUT}"
echo "==> done"
