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
# Requires: makensis (nsis), wine64, python3 (Case 0's version-info assertions).
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${HERE}/../.." && pwd)"
NSI="${REPO}/packaging/mxbmrp3.nsi"

WORK="$(mktemp -d /tmp/mxbmrp3-installer.XXXXXX)"
STAGE="${WORK}/staging"
. "${HERE}/wine_env.sh"
mxb_wine_env installer

# This test's assertions are "is this path/key GONE?", so it can only run in a prefix
# nothing else is writing. Every other Wine gate shares the unsuffixed prefix, and under
# `ctest -j` the integration suite recreates the default savepath directory continuously —
# a shared prefix here reports a broken uninstaller when the uninstaller worked fine.
# CMakeLists.txt leaves this gate OUT of the mxbmrp3_test_dll RESOURCE_LOCK on exactly
# that basis, so the isolation is load-bearing, not belt-and-braces. Assert it rather than
# assume it: an inherited WINEPREFIX silently defeated the suffix once already.
case "${WINEPREFIX}" in
    *-installer) ;;
    *) echo "ERROR: installer test needs its own WINEPREFIX, got '${WINEPREFIX}'"
       echo "       (unset WINEPREFIX, or point it at a base that isn't shared)"
       exit 1 ;;
esac

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

# The uninstaller's SELF-delete can legitimately end two ways: the exe is gone,
# or Delete /REBOOTOK lost the handle race and registered the file in
# PendingFileRenameOperations for deletion at reboot (the .nsi's comment at the
# MX Bikes Delete has the mechanism). Both honor the contract "no orphan
# uninstaller"; only present-AND-unregistered is a failure. The 2026-08-26 CI
# flake was exactly the lost race on a cold runner starting six jobs at once.
assert_self_delete() {
  local i
  for i in $(seq 1 40); do
    [ ! -e "$1" ] && { pass "$2"; return; }
    sleep 0.25
  done
  # Still on disk: accept it if the pending-delete fallback was registered.
  # MoveFileEx(DELAY_UNTIL_REBOOT) stores source paths in this multi-sz value
  # (Wine implements it the same way), each as \??\C:\... — match the basename.
  if "${WINE}" reg query 'HKLM\SYSTEM\CurrentControlSet\Control\Session Manager' \
       /v PendingFileRenameOperations 2>/dev/null | grep -qi "$(basename "$1")"; then
    pass "$2 (pending delete at reboot: self-delete race lost, /REBOOTOK fallback held)"
  else
    fail "$2 (still present after wait, and no pending delete registered: $1)"
  fi
}

# The registry assertions POLL, for the same reason assert_no_file_eventually
# does: the installer's elevated child writes these keys, and its exit can lose
# a race with `wineserver -w` — the very lingering the comment above describes.
# A single-shot `reg query` then reads a half-written key, which is exactly how
# this failed under `ctest -j`: within one install, DisplayName read as missing
# while MXBikesPath beside it read fine, and both passed on a serial re-run.
# Polling makes a genuinely-missing key still fail, just ~10s later.
#
# reg_wait CONDITION_CMD... — true as soon as the condition holds, else after ~10s.
reg_wait() {
  local i
  for i in $(seq 1 40); do
    if "$@" >/dev/null 2>&1; then return 0; fi
    sleep 0.25
  done
  return 1
}
# reg_value_matches KEY VALUENAME EXPECTED_SUBSTR — the predicate reg_has polls on.
reg_value_matches() {
  "${WINE}" reg query "$1" /v "$2" 2>/dev/null | grep -qi -- "$3"
}

# reg_has KEY VALUENAME EXPECTED_SUBSTR DESC
reg_has() {
  if reg_wait reg_value_matches "$1" "$2" "$3"; then pass "$4"; else fail "$4 (want '$2'~'$3' in $1)"; fi
}
# reg_absent_key KEY DESC — polls for the DELETE to land, mirroring reg_has.
reg_absent_key() {
  if reg_wait_gone "${WINE}" reg query "$1"; then pass "$2"; else fail "$2 (key still present: $1)"; fi
}
# reg_absent_value KEY VALUENAME DESC
reg_absent_value() {
  if reg_wait_gone "${WINE}" reg query "$1" /v "$2"; then pass "$3"; else fail "$3 (value still present: $2)"; fi
}
# reg_wait_gone CMD... — true as soon as CMD FAILS (i.e. the key/value is gone).
reg_wait_gone() {
  local i
  for i in $(seq 1 40); do
    if ! "$@" >/dev/null 2>&1; then return 0; fi
    sleep 0.25
  done
  return 1
}

cleanup() { wineserver -k 2>/dev/null || true; rm -rf "${WORK}"; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
echo "== Building staging tree =="
mkdir -p "${STAGE}/mxbmrp3_data/fonts" "${STAGE}/mxbmrp3_data/textures" \
         "${STAGE}/mxbmrp3_data/icons" "${STAGE}/mxbmrp3_data/web/js" \
         "${STAGE}/mxbmrp3_data/web/fonts" "${STAGE}/mxbmrp3_data/web/icons" \
         "${STAGE}/mxbmrp3_data/web/logos" \
         "${STAGE}/mxbmrp3_data/themes/testtheme" \
         "${STAGE}/mxbmrp3_data/gamepads/testpad" \
         "${STAGE}/mxbmrp3_data/pitboards/testboard" \
         "${STAGE}/mxbmrp3_data/spotters/testvoice"
# The two licence notices the installer lays into mxbmrp3_data\. Release staging
# puts them at the staging ROOT (both zip builders copy them there), which is where
# the .nsi File-s them from.
echo LICENCE > "${STAGE}/LICENSE"
echo THIRD > "${STAGE}/THIRD_PARTY_LICENSES.md"
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
# A theme is a SUBDIRECTORY of themes/, never a loose file, so the installer's
# recursive File must find one -- makensis fails the whole build on an empty
# glob, which is how the missing themes/ line here surfaced: the .nsi shipped
# them, the staging tree never had any, and nothing complained until the
# installer gate ran.
echo c > "${STAGE}/mxbmrp3_data/themes/testtheme/center.tga"
echo n > "${STAGE}/mxbmrp3_data/themes/testtheme/testtheme.ini"
# A gamepad pack nests exactly like a theme (gamepads/<name>/*.tga + <name>.ini),
# so it needs its own staged subdirectory for the same reason: makensis fails the
# build on an empty recursive glob.
echo g > "${STAGE}/mxbmrp3_data/gamepads/testpad/background.tga"
echo n > "${STAGE}/mxbmrp3_data/gamepads/testpad/testpad.ini"
# Pit board packs nest identically; a third recursive File line, a third empty-glob
# risk, so a third staged subdirectory.
echo b > "${STAGE}/mxbmrp3_data/pitboards/testboard/background.tga"
echo n > "${STAGE}/mxbmrp3_data/pitboards/testboard/testboard.ini"
# Spotter voice packs nest like themes (spotters/<voice>/*.wav + <voice>.ini);
# a fourth recursive File line, a fourth empty-glob risk, a fourth staged
# subdirectory.
echo a > "${STAGE}/mxbmrp3_data/spotters/testvoice/clear.wav"
echo n > "${STAGE}/mxbmrp3_data/spotters/testvoice/testvoice.ini"

echo "== Compiling installer (makensis) =="
makensis -V1 -DPLUGIN_VERSION=9.9.9.0 -DPLUGIN_SOURCE_PATH="${STAGE}" \
         -DOUTPUT_DIR="${WORK}" "${NSI}" \
  || { echo "ERROR: makensis failed"; exit 1; }
SETUP="${WORK}/mxbmrp3-Setup.exe"
[ -f "${SETUP}" ] || { echo "ERROR: Setup.exe not produced"; exit 1; }

# ---------------------------------------------------------------------------
echo ""
echo "== Case 0: Setup.exe carries complete, consistent version info =="
# Static assertion on the produced PE — no Wine needed. Two things are pinned:
#
#   1. All nine StringFileInfo keys exist. The installer shipped for months with
#      only five (no CompanyName/OriginalFilename/InternalName/Comments) while the
#      DLL's mxbmrp3.rc had the full set, and nothing caught the gap.
#   2. FileVersion / ProductVersion agree with each other AND with the binary
#      FIXEDFILEINFO. FileVersion used to be "${__DATE__} ${__TIME__}", so the two
#      disagreed on every single build.
#
# Both matter for antivirus reputation on an unsigned installer (see the block
# comment in packaging/mxbmrp3.nsi), which is why they are worth a test rather than
# a convention. Values are read by walking UTF-16LE runs: in a StringFileInfo block
# each key is immediately followed by its value, so adjacency is enough and we don't
# need a full VS_VERSIONINFO parser.
python3 - "${SETUP}" <<'PYEOF' || exit 1
import re, struct, sys

blob = open(sys.argv[1], 'rb').read()
runs = [m.group().decode('utf-16-le')
        for m in re.finditer(rb'(?:[\x20-\x7e]\x00){2,}', blob)]
idx = {}
for i, s in enumerate(runs):
    idx.setdefault(s, i)

EXPECTED_VER = "9.9.9.0"   # matches -DPLUGIN_VERSION above
required = ["CompanyName", "FileDescription", "FileVersion", "InternalName",
            "LegalCopyright", "OriginalFilename", "ProductName", "ProductVersion",
            "Comments"]

fail = 0
vals = {}
for key in required:
    if key not in idx or idx[key] + 1 >= len(runs):
        print(f"  FAIL: version key missing: {key}"); fail = 1; continue
    vals[key] = runs[idx[key] + 1]
    print(f"  PASS: {key} = {vals[key]}")

for key in ("FileVersion", "ProductVersion"):
    if vals.get(key) != EXPECTED_VER:
        print(f"  FAIL: {key} is {vals.get(key)!r}, expected {EXPECTED_VER!r}"); fail = 1

# The UAC consent prompt shows FileDescription as "Program name" — a bare URL there
# is the exact thing this test exists to prevent regressing to.
desc = vals.get("FileDescription", "")
if desc.startswith("http://") or desc.startswith("https://"):
    print(f"  FAIL: FileDescription is a bare URL ({desc!r}); belongs in Comments"); fail = 1
else:
    print("  PASS: FileDescription is a product name, not a URL")

# The UAC prompt truncates a long "Program name", so keep this short enough to read
# in full there. No lower bound and no required game names: the description names the
# PiBoSo engine family precisely so that adding a title cannot make it stale.
if len(desc) > 80:
    print(f"  FAIL: FileDescription is {len(desc)} chars; UAC truncates it: {desc!r}"); fail = 1
else:
    print(f"  PASS: FileDescription is {len(desc)} chars, short enough for the UAC prompt")

off = blob.find(struct.pack('<I', 0xFEEF04BD))
if off < 0:
    print("  FAIL: FIXEDFILEINFO signature not found"); fail = 1
else:
    _, _, fv_ms, fv_ls, pv_ms, pv_ls = struct.unpack_from('<IIIIII', blob, off)
    fmt = lambda ms, ls: f"{ms >> 16}.{ms & 0xffff}.{ls >> 16}.{ls & 0xffff}"
    for label, got in (("FILEVERSION", fmt(fv_ms, fv_ls)),
                       ("PRODUCTVERSION", fmt(pv_ms, pv_ls))):
        if got != EXPECTED_VER:
            print(f"  FAIL: binary {label} is {got}, expected {EXPECTED_VER}"); fail = 1
        else:
            print(f"  PASS: binary {label} = {got}")

sys.exit(fail)
PYEOF

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
# Shipping the OFL fonts and the MIT gamepad art without these is a licence
# violation, and nothing else would notice: the plugin never reads them, so the
# install works perfectly with them absent.
assert_file "${MXB}/mxbmrp3_data/LICENSE"                "LICENSE installed"
assert_file "${MXB}/mxbmrp3_data/THIRD_PARTY_LICENSES.md" "third-party notices installed"
# Themes nest one level deeper than every other asset dir (themes/<name>/*.tga),
# so a File line that forgets /r, or an INSTALL macro that lists the folder but
# not its contents, installs an empty themes/ and nothing else notices.
assert_file "${MXB}/mxbmrp3_data/themes/testtheme/center.tga" "theme subfolder installed"
# Gamepad packs nest the same way and were added later, so they get the same
# assertion rather than riding on the theme one -- a /r dropped from either File
# line has to fail on its own.
assert_file "${MXB}/mxbmrp3_data/gamepads/testpad/background.tga" "gamepad pack subfolder installed"
assert_file "${MXB}/mxbmrp3_data/pitboards/testboard/background.tga" "pitboard pack subfolder installed"
assert_file "${MXB}/mxbmrp3_data/spotters/testvoice/clear.wav" "spotter voice pack subfolder installed"
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
assert_self_delete "${MXB}/mxbmrp3_uninstall.exe"  "uninstaller removed"
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
# They live inside mxbmrp3_data\ so the existing RMDir /r covers them -- which is
# the point of putting them there, and worth pinning so a later move beside the
# DLO cannot leave them behind.
assert_no_file "${MXB}/mxbmrp3_data/THIRD_PARTY_LICENSES.md" "notices removed with the data tree"
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
