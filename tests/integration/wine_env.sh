#!/usr/bin/env bash
# ============================================================================
# tests/integration/wine_env.sh — the Wine environment every Wine-driving test
# script needs. SOURCED, never executed.
#
# This was three identical export lines copy-pasted into eight scripts. Nothing
# kept them in step, and the consequence of drift is quiet rather than loud: a
# script with a different WINEPREFIX silently boots its own prefix (slow, and it
# sees none of the state the others set up), while a missing WINEARCH gets you a
# 32-bit prefix whose reg.exe reads the WOW6432Node-redirected view — the exact
# failure mode that once broke the installer test's HKLM assertions.
#
#   . "${HERE}/wine_env.sh"      # HERE = the sourcing script's own directory
#   mxb_wine_env                 # shared prefix
#   mxb_wine_env installer       # own prefix (see below)
#   mxb_wine_no_crash_debugger   # crashes exit instead of hanging (see below)
#
# Every value stays overridable from the caller's environment, so
# `WINEDEBUG=+relay ./run_tests.sh` still works.
# ============================================================================

# shellcheck shell=bash
mxb_wine_env() {
    # ${1:+-$1} appends "-<suffix>" only when a suffix was passed, and is safe
    # under `set -u` (a bare $1 would not be).
    local suffix="${1:+-$1}"
    # The base prefix: an INHERITED WINEPREFIX still wins, so
    # `WINEPREFIX=/tmp/p ./run_tests.sh` works as documented above.
    local base="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
    # ...but the suffix is applied to that base rather than defaulted around it.
    # This used to be one `${WINEPREFIX:-$HOME/...${1:+-$1}}`, where an inherited
    # WINEPREFIX won the whole expression and SILENTLY DISCARDED the suffix — so a
    # caller asking for its own prefix quietly got the shared one. That is not a
    # theoretical hole: WINEPREFIX is exported by some dev/CI environments, and it
    # put the installer test (the only suffixed caller, and the one whose
    # assertions are "is this path gone?") back in the shared prefix, where the
    # concurrently-running integration suite recreates the very savepath directory
    # it just checked was deleted. Failure looked like a broken uninstaller.
    # Stripping first keeps a repeat call in one shell idempotent.
    base="${base%"${suffix}"}"
    export WINEPREFIX="${base}${suffix}"
    # win64 is load-bearing, not a default: everything here is x86_64, and a
    # 32-bit prefix hides the amd64 installer's 64-bit registry keys.
    export WINEARCH=win64
    export WINEDEBUG="${WINEDEBUG:--all}"
}

# Stop a crashing test binary from launching winedbg.
#
# WHY THIS IS NOT COSMETIC. On an unhandled exception Wine consults
# HKLM\...\AeDebug\Debugger and spawns `winedbg --auto`, which under a headless
# runner blocks instead of dumping and exiting. That debugger is a SEPARATE
# process, so run_tests.sh's per-test `timeout` kills the test's `wine` but
# leaves winedbg behind, still attached to the prefix — one was found running 27
# minutes after the test that spawned it. The visible cost is the per-test cap
# (120s) burned on every crash instead of ~1s, plus a straggler slowing every
# later test in the same prefix; that is how ONE crashing binary pushes the whole
# integration gate towards its 2400s limit rather than just failing.
#
# Idempotent and cheap: the marker means the `wine reg add` runs once per prefix,
# not once per script. Callers invoke it right after mxb_wine_env, which only
# exports variables — so on a fresh box THIS `wine reg add` is what boots the
# prefix, and that is fine (it just pays the boot here instead of at the first
# test). A failure is non-fatal: it only restores today's behaviour.
mxb_wine_no_crash_debugger() {
    local marker="${WINEPREFIX}/.mxb-no-crash-debugger"
    [ -f "${marker}" ] && return 0
    wine reg add 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\AeDebug' \
        /v Debugger /t REG_SZ /d '' /f >/dev/null 2>&1 || return 0
    touch "${marker}" 2>/dev/null || true
}
