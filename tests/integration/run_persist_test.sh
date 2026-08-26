#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_persist_test.sh
# Settings persistence round-trip test. Perturbs a settings file (flips every
# boolean toggle — modelling a user changing settings in the plugin), then
# Startup loads it and Shutdown re-saves from live state. Asserts every changed
# value survived — i.e. user changes actually persist across a restart.
#
# Targets the highest-severity, least-covered area: a setting applied in memory
# but never written to disk (the hudOrder "third hardcoded list" trap) reverts
# silently on restart. See CLAUDE.md "Adding a New HUD" step 6.
#
# Requires: mingw-w64 (posix), wine64, python3.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

. "${HERE}/wine_env.sh"
mxb_wine_env
mxb_wine_no_crash_debugger   # a crash exits instead of hanging winedbg
SAVE=/tmp/mxbsave   # loader.exe's hardcoded Z:\tmp\mxbsave\
INI="${SAVE}/mxbmrp3/mxbmrp3_settings.ini"
# Wall-clock cap per loader run (startup+shutdown). Aborts a hang instead of burning CI
# minutes. Override: MXBMRP3_PERSIST_TIMEOUT=120 ./run_persist_test.sh
PERSIST_TIMEOUT="${MXBMRP3_PERSIST_TIMEOUT:-60}"

command -v wine >/dev/null    || { echo "ERROR: wine not found";    exit 1; }
command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 1; }

echo "== Building plugin DLL + loader host =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O1 "${HERE}/loader.cpp" -o "${BUILD}/loader.exe" \
    || { echo "ERROR: loader host build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Capturing default settings.ini =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
wineserver -w
[ -s "${INI}" ] || { echo "ERROR: no baseline settings.ini"; exit 1; }

echo "== Perturbing every boolean toggle =="
python3 "${HERE}/persist_gen.py" "${INI}" "${INI}" /tmp/persist_expect.txt

echo "== Startup (load) -> Shutdown (re-save from live state) =="
( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
wineserver -w

echo "== Checking every changed setting survived =="
python3 "${HERE}/persist_check.py" /tmp/persist_expect.txt "${INI}"
rc=$?

# --- Migration: a file written BEFORE the spotter's Proximity category was
# carved out of Opponents has no cat_proximity line, and its cat_opponents
# answers for both halves. Somebody who had muted the whole group must not get
# the spotting half back talking after an upgrade — so the old key seeds the
# new one. The round-trip above cannot see this: it perturbs keys that are
# already in the file, and the whole point here is a key that is NOT.
if [ $rc -eq 0 ]; then
    echo "== Checking cat_opponents=0 in a pre-split file mutes cat_proximity =="
    python3 - "${INI}" <<'PY'
import re, sys
path = sys.argv[1]
text = open(path, encoding='utf-8', errors='replace').read()
text = re.sub(r'^cat_proximity=.*\n', '', text, flags=re.M)   # pre-split file
text = re.sub(r'^cat_opponents=.*$', 'cat_opponents=0', text, flags=re.M)
open(path, 'w', encoding='utf-8').write(text)
PY
    ( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
    wineserver -w
    if grep -q '^cat_proximity=0' "${INI}"; then
        echo "  PASS: the pre-split mute carried over to the new category"
    else
        echo "  FAIL: cat_proximity did not inherit cat_opponents=0"
        grep -n '^cat_opponents=\|^cat_proximity=' "${INI}" || true
        rc=1
    fi
fi

# --- A ';' in a by-name asset value is DATA, not a comment.
#
# `;` is legal in a Windows folder name. The loader's inline-comment strip used
# to be unconditional, so a theme in `retro;90s` loaded as `retro`, degraded to
# unthemed, and then the SAVE wrote the truncated name back -- destroying the
# choice permanently, which is what by-name asset storage exists to prevent.
# Settings::isFolderNameValue() lists the keys that opt out.
#
# THIS IS THE CHECK THAT REPLACES A COMMENT. The rule those keys carry -- "the
# writer must never give them an inline comment, because it would be read as
# part of the name" -- was prose, and prose is what the first, WRONG version of
# this fix was justified by. A name that survives the round trip proves both
# halves at once: no comment on the line, and no strip on the value.
if [ $rc -eq 0 ]; then
    echo "== Checking a ';' in a by-name asset value survives the round-trip =="
    python3 - "${INI}" <<'SEMI'
import re, sys
path = sys.argv[1]
text = open(path, encoding='utf-8', errors='replace').read()
# Deliberately NOT installed packs: an unknown name must be KEPT rather than
# rewritten (the same invariant), so this reads back whatever was stored.
text = re.sub(r'^panelTheme=.*$', 'panelTheme=retro;90s', text, flags=re.M)
text = re.sub(r'^pack=.*$',       'pack=my;voice',        text, flags=re.M)
open(path, 'w', encoding='utf-8').write(text)
SEMI
    ( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
    wineserver -w
    bad=0
    grep -q '^panelTheme=retro;90s$' "${INI}" || { echo "  FAIL: panelTheme lost its ';'"; bad=1; }
    grep -q '^pack=my;voice$'        "${INI}" || { echo "  FAIL: spotter pack lost its ';'"; bad=1; }
    if [ $bad -eq 0 ]; then
        echo "  PASS: both by-name values round-tripped intact"
    else
        grep -n '^panelTheme=\|^pack=' "${INI}" || true
        rc=1
    fi
fi

[ $rc -eq 0 ] && echo "== PERSIST TEST PASS ==" || echo "== PERSIST TEST FAIL (settings did not persist) =="
exit $rc
