#!/bin/bash
# ============================================================================
# SessionStart hook — provision the headless test toolchain for Claude Code on
# the web, so a session can build + run the tests immediately.
#
# The shippable .dlo is MSVC-only (Windows). On Linux the tests cross-compile the
# plugin to a Windows DLL with mingw-w64 and run it under Wine. The package
# list lives in tools/install_deps.sh (one table, shared with CI):
#   ctest --test-dir build/tests -L fast  -> lints + pure-logic unit tests (no mingw)
#   ./tests/integration/run_tests.sh   -> mingw cross-build + Wine integration
# Idempotent: the container is cached after the first run, so later sessions skip
# the install and just re-export the env.
# ============================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# The tools a CTest gate needs, READ OFF the gates themselves. Absence is
# otherwise invisible: the gate exits 3, CTest prints SKIPPED, and that reads
# like a deliberate pass rather than "this check did not run".
#
# Derived, not hand-kept, because a hand-kept copy is what caused the bug this
# guard is for. The previous list drifted from the installer (cppcheck in
# neither) and, once fixed, drifted from the GATES too: it probed ccache, which
# no gate requires — so the "their gates report SKIPPED" line was false for it —
# while missing g++, which three gates require. mxb_gate()'s 4th argument is the
# real source; ^[[:space:]]* because three gates are indented inside an if().
mxb_gate_tools() {
  grep -oE '^[[:space:]]*mxb_gate\([^ ]+ +[a-z]+ +[0-9]+ +"[^"]*"' \
       "${REPO_ROOT}/CMakeLists.txt" 2>/dev/null \
    | sed 's/.*"\(.*\)"/\1/' | tr ' ' '\n' | grep -vE '^-?$' | sort -u
}

mxb_missing_tools() {
  local out="" t
  for t in $(mxb_gate_tools); do
    command -v "$t" >/dev/null 2>&1 || out="${out} ${t}"
  done
  printf '%s' "${out}"
}

# Deriving the list traded a stale copy for a SILENT one: if CMakeLists.txt moves,
# is unreadable, or the mxb_gate() line format changes, the grep yields nothing,
# "missing" is empty, and this hook cheerfully prints "toolchain ready" having
# checked NOTHING — restoring exactly the silence the derivation was meant to end.
# check_docs.py guards its own parse the same way ("parsed nothing — did the
# format change?"); that guard was dropped on the way here. Verified: with
# REPO_ROOT pointed at a missing path, the parse returns 0 tools and the report
# stays quiet without this.
#
# Complains AT MOST ONCE per run, via the memo below: two callers ask (the
# install guard, then the closing report), and both are on the failure path
# together, so the naive version printed the same two lines twice. Suppressing
# it at the second caller instead would be wrong — on a LOCAL session the report
# is the ONLY caller, so it has to be the one that speaks. A memo lets whoever
# asks first do the complaining.
MXB_PARSE_COMPLAINED=0
mxb_gate_tools_ok() {
  [ -n "$(mxb_gate_tools)" ] && return 0
  if [ "${MXB_PARSE_COMPLAINED}" = 0 ]; then
    MXB_PARSE_COMPLAINED=1
    echo "session-start: could not read gate tools from ${REPO_ROOT}/CMakeLists.txt"
    echo "session-start: NOTHING was verified — did the file move, or the mxb_gate() line format change?"
  fi
  return 1
}

mxb_report_missing() {
  mxb_gate_tools_ok || return 0
  local missing; missing="$(mxb_missing_tools)"
  [ -z "${missing}" ] && return 0
  echo "session-start: MISSING tools:${missing}"
  echo "session-start: their gates report SKIPPED, which is NOT a pass — the check did not run."
  echo "session-start: install with ./tools/install_deps.sh --list  (then the group name)"
}

# Web (remote) sessions only — a local machine already has its own toolchain, and
# apt-installing onto someone's own box uninvited is not this hook's business.
# It still REPORTS, though: silence here is what let cppcheck's gate skip in
# every session unnoticed, and a local agent has the same blind spot.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  mxb_report_missing
  exit 0
fi

LOG=/tmp/mxbmrp3-toolchain.log

# `wine`, not `wine64`: Ubuntu 24.04's wine64 PACKAGE ships no binary of that
# name, so testing for it made this guard always true and re-ran apt every
# session. The binaries here are the same ones mxb_gate checks for.
# Every binary a gate needs is probed, because a MISSING one is invisible: the
# gate exits 3 and CTest reports SKIPPED, which reads like a deliberate pass and
# gets scrolled past. cppcheck was absent from this list AND from the install
# below, so its gate skipped in every session the container was cached for --
# nobody was ignoring it, it was never installable here. A gate whose tool is
# genuinely unavailable should say so once, at session start, not silently every
# run.
# A failed parse REPORTS (via the guard) and then provisions anyway: unable to
# verify is not the same as nothing missing, and skipping the install on an
# unreadable CMakeLists.txt would leave a bare container silently unequipped.
#
# Two DIFFERENT things make the parse fail, and they want opposite handling:
#   - the mxb_gate() line FORMAT changed -> the repo is here, the installer is
#     here, provisioning is still exactly right. This is the case above.
#   - REPO_ROOT resolved somewhere wrong -> install_deps.sh is missing too, so
#     running it can only fail, and failing it here EXITS THE HOOK NON-ZERO and
#     takes the whole session with it. That turns a misresolved path into a
#     bricked session, which is a far worse outcome than the silence this guard
#     replaced. So: probe for the installer, and if it isn't there say so and
#     carry on. A session that starts with a loud, accurate complaint beats one
#     that does not start.
if ! mxb_gate_tools_ok || [ -n "$(mxb_missing_tools)" ]; then
  # The package list, the 64-bit-Wine rationale and the posix-mingw
  # update-alternatives live in ONE place — tools/install_deps.sh — shared with
  # the CI workflows and DEVELOPMENT.md. This hook used to carry its own copy.
  if [ ! -x "${REPO_ROOT}/tools/install_deps.sh" ]; then
    echo "session-start: no installer at ${REPO_ROOT}/tools/install_deps.sh — cannot provision."
    echo "session-start: REPO_ROOT looks wrong; the toolchain is whatever the container already had."
  elif ! "${REPO_ROOT}/tools/install_deps.sh" \
         build python mingw wine nsis cppcheck clang node coverage lint >"$LOG" 2>&1; then
    echo "session-start: toolchain install failed"; tail -25 "$LOG"; exit 1
  fi
fi

# Wine launcher fix: the packaged /usr/bin/wine resolves its loader from argv[0]
# and fails with "could not exec the wine loader". Replace it with a wrapper that
# execs the real loader by absolute path.
if [ -x /usr/lib/wine/wine64 ] && ! head -1 /usr/bin/wine 2>/dev/null | grep -q '^#!/bin/sh'; then
  printf '#!/bin/sh\nexec /usr/lib/wine/wine64 "$@"\n' | sudo tee /usr/bin/wine >/dev/null
  sudo chmod +x /usr/bin/wine
fi

# Persist the Wine env for the whole session (WINELOADER is the load-bearing one).
# These are WRITTEN as text into the session env file, so this is the one place
# that cannot source tests/integration/wine_env.sh — keep the values in step
# with it by hand. (The test scripts all set their own env anyway; this only
# makes an interactive shell behave like they do.)
{
  echo 'export WINELOADER=/usr/lib/wine/wine64'
  echo 'export WINEARCH=win64'
  echo 'export WINEDEBUG=-all'
  echo "export WINEPREFIX=\"\${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}\""
} >> "$CLAUDE_ENV_FILE"

# Warm the Wine prefix once so the first integration test isn't racing its init.
export WINELOADER=/usr/lib/wine/wine64 WINEARCH=win64 WINEDEBUG=-all
export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
if [ ! -d "$WINEPREFIX" ]; then
  wineboot -i >/dev/null 2>&1 || true
  wineserver -w >/dev/null 2>&1 || true
fi

# Name anything still missing. Without this the only signal is a SKIPPED gate
# mid-suite, which is easy to read as "not applicable here" rather than "this
# check did not run".
#
# "ready" is only claimed when the cross-compiler is actually callable. Since the
# install can now be SKIPPED (missing installer, above), this line is reachable on
# a bare container — and printing "toolchain ready ( ; wine unknown)" over a
# `command not found` is the same false all-clear the reporting exists to end.
if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "session-start: toolchain ready ($(x86_64-w64-mingw32-g++ --version | head -1 | awk '{print $1,$NF}'); $(wine --version 2>/dev/null || echo 'wine unknown'))"
else
  echo "session-start: toolchain NOT ready — no x86_64-w64-mingw32-g++; the cross-build and every Wine gate will fail or skip."
fi
mxb_report_missing
