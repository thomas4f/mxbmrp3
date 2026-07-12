#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_installer_test.sh
# NSIS installer/uninstaller mechanics test. Builds packaging/mxbmrp3.nsi with
# makensis, then drives the produced Setup.exe and its uninstaller HEADLESS under
# Wine and asserts the on-disk + registry outcomes:
#
#   - elevated-child install lays files down and registers under HKLM
#   - the HKLM write-probe leaves no stray key; HKCU stays clean
#   - fresh install (/FRESH=1) wipes the savepath data folder first
#   - multi-game install writes one path key per game
#   - partial uninstall removes one game, keeps the others, repoints the entry
#   - full uninstall deletes the key entirely
#   - remove-all-data uninstall (/UDATA=1) deletes the savepath data folder
#
# WHY the /ELEVATED command-line path: the wizard collects the game selection on
# nsDialogs pages, which a headless run can't drive deterministically. The elevated
# CHILD (the process the on-demand relaunch spawns) takes every choice on the
# command line and runs the SAME install/uninstall Section + registry + data-wipe
# code — so driving the child directly exercises all the mechanics without a GUI.
#
# WHAT THIS DOES NOT COVER (Wine has no UAC and doesn't enforce ACLs for a normal
# user, so these stay a manual Windows check — see packaging/mxbmrp3.nsi and the
# P1 matrix in TESTING.md):
#   - the writability probe actually TRIGGERING the relaunch (Wine dirs are writable)
#   - the real UAC prompt / cross-account (standard user -> admin creds) elevation
#   - the per-user HKCU hive branch (Wine always permits the HKLM write, so
#     useMachineReg is always 1 here) — same WRITE_UNINSTALL_REG macro, other root
#   - the interactive pages themselves (rendered once by hand; see the PR notes)
#
# Requires: makensis (nsis), wine64.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
NSI="${REPO}/packaging/mxbmrp3.nsi"

WORK="$(mktemp -d /tmp/mxbmrp3-installer.XXXXXX)"
STAGE="${WORK}/staging"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3-installer}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"

# The shipped installer is a 64-bit (amd64) NSIS build (`Target AMD64-Unicode`), so
# Setup.exe writes the 64-bit registry view. On a multiarch runner Debian's /usr/bin/wine
# wrapper prefers the 32-bit loader whenever wine32:i386 is installed (CI installs it), and
# a 32-bit reg.exe reads the WOW6432Node-redirected view — it can NOT see the installer's
# 64-bit keys. That made every HKLM assertion fail while HKCU looked "clean" (the whole
# reason this test was red in CI but green on a wine64-only box). Wine's reg.exe ignores
# /reg:64, so pin the 64-bit loader for every wine call: the test then drives Setup and
# queries the registry in the one view the installer actually uses. Falls back to plain
# `wine` when no 64-bit loader is found (e.g. a wine64-only host, where it's already 64-bit).
WINE=wine
for c in /usr/lib/wine/wine64 /usr/lib/x86_64-linux-gnu/wine/wine64 "$(command -v wine64 2>/dev/null)"; do
  [ -n "$c" ] && [ -x "$c" ] && { WINE="$c"; break; }
done

REG_KEY='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\MXBMRP3'
REG_KEY_HKCU='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\MXBMRP3'

command -v makensis >/dev/null || { echo "ERROR: makensis not found (apt-get install nsis)"; exit 1; }
command -v wine     >/dev/null || { echo "ERROR: wine not found"; exit 1; }
echo "wine launcher: ${WINE}  ($("${WINE}" --version 2>/dev/null))"

FAILS=0
pass() { echo "  PASS: $1"; }
fail() { echo "  FAIL: $1"; FAILS=$((FAILS+1)); }

# Windows path (Z:\...) for a Linux path under the Wine Z: drive mapping (/ -> Z:\)
winpath() { printf 'Z:%s' "${1//\//\\}"; }

assert_file()    { [ -f "$1" ] && pass "$2" || fail "$2 (missing: $1)"; }
assert_no_file() { [ ! -e "$1" ] && pass "$2" || fail "$2 (still present: $1)"; }
assert_no_dir()  { [ ! -d "$1" ] && pass "$2" || fail "$2 (dir still present: $1)"; }
# Like assert_no_file, but tolerates an ASYNC removal. A self-deleting NSIS
# uninstaller copies itself to $TEMP and relaunches; the temp copy then deletes
# the original game-folder exe. That delete can lose a race with the original
# process still holding its own image handle, and NSIS's plain Delete has no
# retry — so under Wine the exe can linger briefly past `wineserver -w`. Poll up
# to ~10s so a genuinely-never-removed exe still fails, without the flake.
assert_no_file_eventually() {
  local i
  for i in $(seq 1 40); do
    [ ! -e "$1" ] && { pass "$2"; return; }
    sleep 0.25
  done
  fail "$2 (still present after wait: $1)"
}

# reg_has KEY VALUENAME EXPECTED_SUBSTR DESC
reg_has() {
  local out; out="$("${WINE}" reg query "$1" /v "$2" 2>/dev/null)"
  if echo "$out" | grep -qi -- "$3"; then pass "$4"; else fail "$4 (want '$2'~'$3' in $1)"; fi
}
# reg_absent_key KEY DESC
reg_absent_key() {
  if "${WINE}" reg query "$1" >/dev/null 2>&1; then fail "$2 (key still present: $1)"; else pass "$2"; fi
}
# reg_absent_value KEY VALUENAME DESC
reg_absent_value() {
  if "${WINE}" reg query "$1" /v "$2" >/dev/null 2>&1; then fail "$3 (value still present: $2)"; else pass "$3"; fi
}

cleanup() { wineserver -k 2>/dev/null || true; rm -rf "${WORK}"; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
echo "== Building staging tree =="
mkdir -p "${STAGE}/mxbmrp3_data/fonts" "${STAGE}/mxbmrp3_data/textures" \
         "${STAGE}/mxbmrp3_data/icons" "${STAGE}/mxbmrp3_data/web/js" \
         "${STAGE}/mxbmrp3_data/web/fonts" "${STAGE}/mxbmrp3_data/web/icons" \
         "${STAGE}/mxbmrp3_data/web/logos"
echo DLO-MXB > "${STAGE}/mxbmrp3.dlo"
echo DLO-GPB > "${STAGE}/mxbmrp3_gpb.dlo"
echo DLO-KRP > "${STAGE}/mxbmrp3_krp.dlo"
echo f > "${STAGE}/mxbmrp3_data/fonts/RobotoMono.fnt"
echo t > "${STAGE}/mxbmrp3_data/textures/helmet.tga"
echo i > "${STAGE}/mxbmrp3_data/icons/chip.tga"
echo h > "${STAGE}/mxbmrp3_data/web/index.html"
echo j > "${STAGE}/mxbmrp3_data/web/js/overlay-render.js"
echo w > "${STAGE}/mxbmrp3_data/web/fonts/Tiny5.ttf"
echo s > "${STAGE}/mxbmrp3_data/web/icons/gear.svg"
echo p > "${STAGE}/mxbmrp3_data/web/logos/logo1.png"

echo "== Compiling installer (makensis) =="
makensis -V1 -DPLUGIN_VERSION=9.9.9.0 -DPLUGIN_SOURCE_PATH="${STAGE}" \
         -DOUTPUT_DIR="${WORK}" "${NSI}" \
  || { echo "ERROR: makensis failed"; exit 1; }
SETUP="${WORK}/mxbmrp3-Setup.exe"
[ -f "${SETUP}" ] || { echo "ERROR: Setup.exe not produced"; exit 1; }

echo "== Booting Wine prefix =="
[ -d "${WINEPREFIX}" ] || "${WINE}" wineboot -i >/dev/null 2>&1
wineserver -w 2>/dev/null || true
# Start clean: no leftover registration from a previous run
"${WINE}" reg delete "${REG_KEY}" /f >/dev/null 2>&1 || true
"${WINE}" reg delete "${REG_KEY_HKCU}" /f >/dev/null 2>&1 || true

# The installer resolves its data folder from $DOCUMENTS -> the real logged-in user's
# Documents (C:\users\<user>\Documents), never the shared "Public" profile. `find | head`
# is unordered, so on some runners (CI) it returned users\Public\Documents first while the
# installer wiped users\<user>\Documents — the data-wipe assertions then failed on a
# path the installer never touched. Exclude Public so the test's data folder is the same
# one the installer targets.
DOCS="$(find "${WINEPREFIX}/drive_c/users" -maxdepth 2 -iname Documents -type d 2>/dev/null | grep -v '/Public/Documents$' | head -1)"
[ -n "${DOCS}" ] || { echo "ERROR: could not locate Wine Documents folder"; exit 1; }

# Fake game trees (with the game exe so any real detection also matches)
MXB="${WORK}/game_mxb/plugins"; GPB="${WORK}/game_gpb/plugins"; KRP="${WORK}/game_krp/plugins"
mkdir -p "${MXB}" "${GPB}" "${KRP}"
echo x > "${WORK}/game_mxb/mxbikes.exe"
echo x > "${WORK}/game_gpb/gpbikes.exe"
echo x > "${WORK}/game_krp/kart.exe"

# ---------------------------------------------------------------------------
echo ""
echo "== Case 1: elevated-child install (MX Bikes) =="
"${WINE}" "${SETUP}" /ELEVATED /S "/MXB=$(winpath "${MXB}")" >/dev/null 2>&1
wineserver -w
assert_file "${MXB}/mxbmrp3.dlo"                         "dlo installed"
assert_file "${MXB}/mxbmrp3_data/web/js/overlay-render.js" "mxbmrp3_data tree installed"
assert_file "${MXB}/mxbmrp3_uninstall.exe"              "uninstaller written"
[ "$(cat "${MXB}/mxbmrp3.dlo" 2>/dev/null)" = "DLO-MXB" ] && pass "correct dlo payload" || fail "wrong dlo payload"
reg_has    "${REG_KEY}" "DisplayName"    "MXBMRP3"       "HKLM DisplayName written"
reg_has    "${REG_KEY}" "MXBikesPath"    "game_mxb"      "HKLM MXBikesPath written"
reg_absent_key   "${REG_KEY_HKCU}"                       "HKCU stays clean (had admin)"
reg_absent_key   'HKLM\Software\MXBMRP3'                 "write-probe key cleaned up"

echo ""
echo "== Case 2: uninstall (elevated-child) removes files + key =="
"${WINE}" "${MXB}/mxbmrp3_uninstall.exe" /ELEVATED /S "/UMXB=$(winpath "${MXB}")" /UDATA=0 >/dev/null 2>&1
wineserver -w
assert_no_file "${MXB}/mxbmrp3.dlo"                      "dlo removed"
assert_no_dir  "${MXB}/mxbmrp3_data"                     "mxbmrp3_data removed"
assert_no_file_eventually "${MXB}/mxbmrp3_uninstall.exe"  "uninstaller removed"
reg_absent_key "${REG_KEY}"                              "HKLM key removed"

echo ""
echo "== Case 3: fresh install (/FRESH=1) wipes savepath data first =="
DATA="${DOCS}/PiBoSo/MX Bikes/mxbmrp3"
mkdir -p "${DATA}/crashes" "${DATA}/benchmarks"
echo s > "${DATA}/mxbmrp3_settings.ini"
echo d > "${DATA}/crashes/c.dmp"
echo b > "${DATA}/benchmarks/bench.txt"
"${WINE}" "${SETUP}" /ELEVATED /S "/MXB=$(winpath "${MXB}")" /FRESH=1 >/dev/null 2>&1
wineserver -w
assert_no_dir "${DATA}"                                  "savepath data wiped on fresh install"
assert_file   "${MXB}/mxbmrp3.dlo"                       "plugin reinstalled after wipe"

echo ""
echo "== Case 4: multi-game install writes one path key per game =="
"${WINE}" "${MXB}/mxbmrp3_uninstall.exe" /ELEVATED /S "/UMXB=$(winpath "${MXB}")" /UDATA=0 >/dev/null 2>&1
wineserver -w
"${WINE}" "${SETUP}" /ELEVATED /S "/MXB=$(winpath "${MXB}")" "/GPB=$(winpath "${GPB}")" "/KRP=$(winpath "${KRP}")" >/dev/null 2>&1
wineserver -w
assert_file "${MXB}/mxbmrp3.dlo"                         "MX dlo installed"
assert_file "${GPB}/mxbmrp3_gpb.dlo"                     "GP dlo installed"
assert_file "${KRP}/mxbmrp3_krp.dlo"                     "KRP dlo installed"
reg_has "${REG_KEY}" "MXBikesPath" "game_mxb"            "MXBikesPath key"
reg_has "${REG_KEY}" "GPBikesPath" "game_gpb"            "GPBikesPath key"
reg_has "${REG_KEY}" "KRPPath"     "game_krp"            "KRPPath key"

echo ""
echo "== Case 5: partial uninstall (GP only) keeps others, repoints entry =="
"${WINE}" "${MXB}/mxbmrp3_uninstall.exe" /ELEVATED /S "/UGPB=$(winpath "${GPB}")" /UDATA=0 >/dev/null 2>&1
wineserver -w
assert_no_file "${GPB}/mxbmrp3_gpb.dlo"                  "GP dlo removed"
assert_file    "${MXB}/mxbmrp3.dlo"                      "MX dlo kept"
assert_file    "${KRP}/mxbmrp3_krp.dlo"                  "KRP dlo kept"
reg_absent_value "${REG_KEY}" "GPBikesPath"             "GPBikesPath key cleared"
reg_has "${REG_KEY}" "MXBikesPath" "game_mxb"            "MXBikesPath key retained"
reg_has "${REG_KEY}" "UninstallString" "MXBMRP3"        "entry still registered (repointed)"

echo ""
echo "== Case 6: full uninstall of the remainder deletes the key =="
"${WINE}" "${MXB}/mxbmrp3_uninstall.exe" /ELEVATED /S "/UMXB=$(winpath "${MXB}")" "/UKRP=$(winpath "${KRP}")" /UDATA=0 >/dev/null 2>&1
wineserver -w
assert_no_file "${MXB}/mxbmrp3.dlo"                      "MX dlo removed"
assert_no_file "${KRP}/mxbmrp3_krp.dlo"                  "KRP dlo removed"
reg_absent_key "${REG_KEY}"                              "HKLM key fully removed"

echo ""
echo "== Case 7: remove-all-data uninstall (/UDATA=1) deletes savepath data =="
"${WINE}" "${SETUP}" /ELEVATED /S "/MXB=$(winpath "${MXB}")" >/dev/null 2>&1
wineserver -w
DATA="${DOCS}/PiBoSo/MX Bikes/mxbmrp3"
mkdir -p "${DATA}/crashes"
echo s > "${DATA}/mxbmrp3_settings.ini"
echo d > "${DATA}/crashes/c.dmp"
"${WINE}" "${MXB}/mxbmrp3_uninstall.exe" /ELEVATED /S "/UMXB=$(winpath "${MXB}")" /UDATA=1 >/dev/null 2>&1
wineserver -w
assert_no_dir  "${DATA}"                                 "savepath data removed on opt-in"
assert_no_file "${MXB}/mxbmrp3.dlo"                      "plugin removed"
reg_absent_key "${REG_KEY}"                              "HKLM key removed"

# ---------------------------------------------------------------------------
echo ""
if [ "${FAILS}" -eq 0 ]; then
  echo "== INSTALLER TEST PASS =="
  exit 0
else
  echo "== INSTALLER TEST FAIL (${FAILS} assertion(s)) =="
  exit 1
fi
