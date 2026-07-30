#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_session_hook.sh
# Behavioural gate for .claude/hooks/session-start.sh — the file that decides
# whether a web session can build and test at all.
#
# WHY THIS EXISTS. The hook's job is to make an UNPROVISIONED box impossible to
# mistake for a healthy one: a gate whose tool is absent exits 3, CTest prints
# SKIPPED, and a suite full of SKIPPED reads exactly like a suite full of
# passes. That makes the hook the detector for this project's worst quiet
# failure — and it was the only file with real branching and no coverage at all.
#
# Three consecutive commits proved the cost. Fixing a STALE hand-kept tool list
# introduced a SILENT one (a failed parse verified nothing and still announced
# "toolchain ready"); fixing the silence introduced a HARD one (a misresolved
# REPO_ROOT ran a missing installer, failed, and exited non-zero, bricking the
# session); fixing that introduced a DUPLICATE complaint. None of the three was
# visible to the other 23 gates, and none was findable by reading the diff —
# each surfaced only by executing the failure path. Hence a fixture.
#
# WHAT IT ASSERTS — PROPERTIES, NOT PROSE. rc, how many times the parse
# complaint appears, ready-vs-NOT-ready, whether the installer ran, whether
# missing tools were named. Deliberately not a golden of the output: these
# messages were reworded in four of the last five commits touching the file, and
# a fixture that fails on a better sentence would be turned off. The five
# ANCHORS below are the contract; the prose around them is free to change.
#
# HERMETIC BY CONSTRUCTION. Every case runs against a fixture repo with a stub
# install_deps.sh (never apt), a stub sudo (the hook rewrites /usr/bin/wine),
# a pre-created WINEPREFIX (never wineboot), and `env -i` (see below). The
# toolchain is stubbed too, so the outcome does not depend on what this machine
# has installed — which is what lets this gate declare NO tools and therefore
# never skip. That is the point: a gate that can skip cannot be the thing that
# protects the skip detector.
#
# `env -i`, not `env -u CLAUDE_CODE_REMOTE`: inside a web session that variable
# is ambient and true, so a "local session" case that merely unsets one variable
# still inherits the rest of the environment. An empty environment is the only
# way to test the local path from within a remote one. That mistake was made in
# a terminal re-derivation of this matrix, which is half the argument for having
# it committed.
#
# NOTE: the hook logs its install to a hardcoded /tmp/mxbmrp3-toolchain.log, so
# running this overwrites that file with the stub's output. It is scratch.
# ============================================================================
set -uo pipefail
cd "$(dirname "$0")/../.."
REPO="$PWD"
HOOK="${REPO}/.claude/hooks/session-start.sh"

[ -f "${HOOK}" ] || { echo "ERROR: no hook at ${HOOK}"; exit 1; }

# The contract. Each anchor is a load-bearing OUTCOME the hook reports; the
# sentence carrying it may be rewritten freely. Changing one of these means the
# hook stopped saying something it is required to say — update both together,
# deliberately.
A_PARSE='could not read gate tools'   # the derivation verified nothing
A_WRONGROOT='no installer at'         # REPO_ROOT resolved somewhere wrong
A_READY='toolchain ready'             # cross-compiler present
A_NOTREADY='toolchain NOT ready'      # cross-compiler absent
A_MISSING='MISSING tools:'            # a gate's tool is unavailable here

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
OUT="${WORK}/out.txt"
mkdir -p "${WORK}/home" "${WORK}/prefix"

# ---------------------------------------------------------------------------
# A minimal PATH: the utilities the hook actually invokes, and nothing else.
# Everything the hook probes for is absent here, which is the "bare container"
# state. type -P, not command -v: several of these are shell builtins too, and
# command -v would hand back the builtin name instead of a path to symlink.
# ---------------------------------------------------------------------------
BARE="${WORK}/bin-bare"
mkdir -p "${BARE}"
for t in bash sh grep sed tr sort awk head tail cat dirname env printf tee; do
    p="$(type -P "$t")" || { echo "ERROR: this box lacks '$t', needed to run the hook"; exit 1; }
    ln -s "$p" "${BARE}/$t"
done

# The hook repairs the packaged wine launcher with `sudo tee /usr/bin/wine`.
# A no-op sudo keeps that from touching the machine running the tests.
printf '#!/bin/sh\nexit 0\n' > "${BARE}/sudo"
chmod +x "${BARE}/sudo"

# ---------------------------------------------------------------------------
# Fixture repos. The hook derives REPO_ROOT from its own path, so each variant
# is a directory tree with a copy of the real hook in it.
# ---------------------------------------------------------------------------
new_root() {  # <dir> — a tree with the real hook and a stub installer
    mkdir -p "$1/.claude/hooks" "$1/tools"
    cp "${HOOK}" "$1/.claude/hooks/session-start.sh"
    # Records that it ran, and installs nothing. Its stdout is redirected by the
    # hook, so the sentinel file is the only reliable signal. `: >` rather than
    # touch: this runs under the deliberately minimal PATH below, where any
    # external binary the stub reaches for is one the hook itself never needed.
    { printf '#!/bin/sh\n'
      printf 'echo "STUB INSTALLER: $*"\n'
      printf ': > %s\n' "$1/installer-ran"
    } > "$1/tools/install_deps.sh"
    chmod +x "$1/tools/install_deps.sh"
}

GOOD="${WORK}/good"; new_root "${GOOD}"
cp "${REPO}/CMakeLists.txt" "${GOOD}/CMakeLists.txt"

# Same tree, but a CMakeLists.txt the mxb_gate() parser cannot read anything
# from — the "line format changed" half of a failed parse.
FMT="${WORK}/fmt"; new_root "${FMT}"
printf 'project(x)\n# deliberately contains no mxb_gate() lines\n' > "${FMT}/CMakeLists.txt"

# No CMakeLists.txt AND no tools/ — the "REPO_ROOT resolved somewhere wrong"
# half. The absent installer is the whole point: this is the case that used to
# fail the hook and take the session down with it.
GONE="${WORK}/gone"
mkdir -p "${GONE}/.claude/hooks"
cp "${HOOK}" "${GONE}/.claude/hooks/session-start.sh"

# ---------------------------------------------------------------------------
run_hook() {  # <root> <remote|local> <path>
    rm -f "$1/installer-ran"
    : > "${WORK}/env"
    local remote=()
    [ "$2" = remote ] && remote=(CLAUDE_CODE_REMOTE=true)
    env -i HOME="${WORK}/home" PATH="$3" WINEPREFIX="${WORK}/prefix" \
        CLAUDE_ENV_FILE="${WORK}/env" "${remote[@]}" \
        bash "$1/.claude/hooks/session-start.sh" >"${OUT}" 2>&1
    RC=$?
}

count() { grep -c -- "$1" "${OUT}" 2>/dev/null || true; }
ran()   { [ -f "$1/installer-ran" ] && echo yes || echo no; }

FAILED=0     # any case failed — drives the exit status
CASE_BAD=0   # THIS case failed — drives the output dump, so a single failure
             # does not make every later case print its (correct) output too
CASE=""
check() {  # <what> <expected> <actual>
    if [ "$2" = "$3" ]; then
        printf '    ok   %s = %s\n' "$1" "$3"
    else
        printf '    FAIL %s: expected %s, got %s\n' "$1" "$2" "$3"
        FAILED=1; CASE_BAD=1
    fi
}
begin() { CASE="$1"; CASE_BAD=0; printf '  %s\n' "$1"; }
dump()  { [ "${CASE_BAD}" = 1 ] && { echo "    --- what the hook printed (${CASE}) ---"; sed 's/^/    | /' "${OUT}"; }; return 0; }

echo "session-start hook — behaviour matrix"

# ---------------------------------------------------------------------------
# Probe: learn the gate tools FROM THE HOOK rather than re-deriving them here.
# A second copy of the mxb_gate() regex is exactly the hand-kept duplicate whose
# drift caused the original bug — so the fixture asks the hook what it wants.
# Local + bare PATH is the configuration that reports without installing.
# ---------------------------------------------------------------------------
begin "probe: the hook names the tools it needs (local, bare PATH)"
run_hook "${GOOD}" local "${BARE}"
TOOLS="$(sed -n "s/.*${A_MISSING}//p" "${OUT}" | tr -s ' ' '\n' | grep -v '^$')"
check "rc" 0 "${RC}"
check "installer ran" no "$(ran "${GOOD}")"
check "missing-tools reported" 1 "$(count "${A_MISSING}")"
if [ -z "${TOOLS}" ]; then
    echo "    FAIL the hook derived NO tools from a real CMakeLists.txt —"
    echo "         the mxb_gate() parse is broken, which is the bug this gate exists for."
    FAILED=1
fi
dump

# Every derived tool, present but inert. Gives a deterministic "fully
# provisioned" PATH regardless of what this machine actually has.
FULL="${WORK}/bin-full"
cp -a "${BARE}" "${FULL}"
for t in ${TOOLS}; do
    printf '#!/bin/sh\necho "stub %s 0.0"\n' "$t" > "${FULL}/$t"
    chmod +x "${FULL}/$t"
done

# ---------------------------------------------------------------------------
begin "1. real repo, remote, provisioned — silent success"
run_hook "${GOOD}" remote "${FULL}"
check "rc" 0 "${RC}"
check "parse complaints" 0 "$(count "${A_PARSE}")"
check "wrong-root reports" 0 "$(count "${A_WRONGROOT}")"
check "ready" 1 "$(count "${A_READY}")"
check "missing-tools reported" 0 "$(count "${A_MISSING}")"
check "installer ran" no "$(ran "${GOOD}")"
dump

begin "2. mxb_gate() format changed — complains ONCE, still provisions"
run_hook "${FMT}" remote "${FULL}"
check "rc" 0 "${RC}"
check "parse complaints" 1 "$(count "${A_PARSE}")"
check "wrong-root reports" 0 "$(count "${A_WRONGROOT}")"
check "installer ran" yes "$(ran "${FMT}")"
check "ready" 1 "$(count "${A_READY}")"
dump

begin "3. REPO_ROOT wrong, tools present — reports, does NOT fail the session"
run_hook "${GONE}" remote "${FULL}"
check "rc" 0 "${RC}"
check "parse complaints" 1 "$(count "${A_PARSE}")"
check "wrong-root reports" 1 "$(count "${A_WRONGROOT}")"
check "ready" 1 "$(count "${A_READY}")"
dump

begin "4. REPO_ROOT wrong, bare box — says NOT ready, still rc 0"
run_hook "${GONE}" remote "${BARE}"
check "rc" 0 "${RC}"
check "parse complaints" 1 "$(count "${A_PARSE}")"
check "wrong-root reports" 1 "$(count "${A_WRONGROOT}")"
check "ready" 0 "$(count "${A_READY}")"
check "not-ready" 1 "$(count "${A_NOTREADY}")"
dump

begin "5. REPO_ROOT wrong, LOCAL session — complains once, returns early"
run_hook "${GONE}" local "${FULL}"
check "rc" 0 "${RC}"
check "parse complaints" 1 "$(count "${A_PARSE}")"
check "wrong-root reports" 0 "$(count "${A_WRONGROOT}")"
check "ready" 0 "$(count "${A_READY}")"
check "not-ready" 0 "$(count "${A_NOTREADY}")"
dump

begin "6. real repo, LOCAL, provisioned — says nothing at all"
run_hook "${GOOD}" local "${FULL}"
check "rc" 0 "${RC}"
check "lines of output" 0 "$(wc -l < "${OUT}" | tr -d ' ')"
check "installer ran" no "$(ran "${GOOD}")"
dump

begin "7. real repo, LOCAL, bare box — names what is missing, installs nothing"
run_hook "${GOOD}" local "${BARE}"
check "rc" 0 "${RC}"
check "parse complaints" 0 "$(count "${A_PARSE}")"
check "missing-tools reported" 1 "$(count "${A_MISSING}")"
check "installer ran" no "$(ran "${GOOD}")"
dump

begin "8. real repo, remote, bare box — provisions, then reports what is still absent"
run_hook "${GOOD}" remote "${BARE}"
check "rc" 0 "${RC}"
check "parse complaints" 0 "$(count "${A_PARSE}")"
check "installer ran" yes "$(ran "${GOOD}")"
check "missing-tools reported" 1 "$(count "${A_MISSING}")"
check "not-ready" 1 "$(count "${A_NOTREADY}")"
dump

# ---------------------------------------------------------------------------
if [ "${FAILED}" = 1 ]; then
    echo
    echo "session-start hook: FAILED — a session-provisioning behaviour changed."
    echo "A broken hook leaves a box unprovisioned, and an unprovisioned box"
    echo "reports SKIPPED, which reads like a passing suite. Fix the hook, or if"
    echo "the change is deliberate, update the expectation AND the anchor above."
    exit 1
fi
echo "session-start hook clean (8 configurations: rc, complaint count, ready state, provisioning)."
