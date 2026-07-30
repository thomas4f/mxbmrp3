#!/usr/bin/env bash
# ============================================================================
# tests/run_cppcheck.sh
# cppcheck static analysis over the first-party sources, with the exact flags CI
# uses. BLOCKING: the committed baseline is at zero findings, so any finding
# fails this script (and the build).
#
# WHY IT IS A SCRIPT. The invocation is long and its flags are load-bearing —
# `--enable=warning` in particular, because `.cppcheck-suppressions` is curated
# for that severity ONLY. Reproducing locally with a wider `--enable` surfaces
# classes CI doesn't gate (memsetClassFloat on the POD Unified:: structs,
# uselessCallsSubstr, ...) which look like new findings but are just the wider
# net. It used to be copy-pasted into the workflow and into TESTING.md, i.e. two
# places to drift from each other; now both call this.
#
# To land a legitimate new finding: fix it, add an inline
# `// cppcheck-suppress <id>` with a reason, or — last resort — a documented
# entry in .cppcheck-suppressions (project-wide intentional patterns only).
#
#   ./tests/run_cppcheck.sh          # fail on any finding (what CI does)
#   ./tests/run_cppcheck.sh --report # print findings, always exit 0
# ============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"
cd "${ROOT}"

REPORT_ONLY=0
[ "${1:-}" = "--report" ] && REPORT_ONLY=1

command -v cppcheck >/dev/null || { echo "ERROR: cppcheck not found (apt-get install cppcheck)"; exit 1; }
cppcheck --version

OUT="$(mktemp)"
trap 'rm -f "${OUT}"' EXIT

# --platform=win64 gives the shipping target's type sizes; --max-configs=1 plus a
# fixed game define avoids the combinatorial #ifdef explosion; vendored code is
# excluded from the check list (and by path in the suppressions, since our
# includes still pull it in for analysis).
set +e
cppcheck --quiet --enable=warning --inline-suppr --std=c++17 --language=c++ \
    --platform=win64 --max-configs=1 -DGAME_MXBIKES=1 -D_WIN32=1 \
    -I mxbmrp3 -i mxbmrp3/vendor \
    --suppress=missingInclude --suppress=missingIncludeSystem \
    --suppressions-list=.cppcheck-suppressions \
    --template='{file}:{line}: {severity}: {message} [{id}]' \
    -j"$(nproc)" mxbmrp3 2>"${OUT}"
rc=$?

# A nonzero exit from cppcheck ITSELF (crash / misconfiguration) must be
# distinguishable from findings: without --error-exitcode, findings never affect
# the exit code, so a broken tool would otherwise look exactly like "clean".
if [ "${rc}" -ne 0 ]; then
    echo "ERROR: cppcheck itself failed (exit ${rc}) — tool crash or misconfiguration, not findings" >&2
    cat "${OUT}" >&2
    exit "${rc}"
fi

# GitHub Actions surfacing. Kept HERE rather than in the workflow step so CI and a
# local run share one source: the inline step this replaced wrote the findings to
# the job summary and emitted a ::error:: annotation, and moving the invocation
# into a script silently dropped both — findings would still fail the job, but
# only as raw step output you had to expand to see. Both env vars are unset
# outside Actions, so this is a no-op locally.
gh_summary() {   # $1 = markdown to append to the run summary, if we're in Actions
    [ -n "${GITHUB_STEP_SUMMARY:-}" ] && printf '%s\n' "$1" >> "${GITHUB_STEP_SUMMARY}"
    return 0
}

if [ -s "${OUT}" ]; then
    n=$(grep -c . "${OUT}")
    cat "${OUT}"
    gh_summary "## cppcheck findings ($n)"
    gh_summary '```'
    gh_summary "$(cat "${OUT}")"
    gh_summary '```'
    if [ "${REPORT_ONLY}" -eq 1 ]; then
        echo "cppcheck: ${n} finding(s) (report-only mode)"
        exit 0
    fi
    # ::error:: puts it on the run's annotation list, so it's visible without
    # expanding the step.
    echo "::error::cppcheck reported ${n} finding(s) — the baseline is zero; see the job summary"
    echo "== CPPCHECK FAIL: ${n} finding(s) — the baseline is zero ==" >&2
    exit 1
fi

echo "cppcheck: no findings."
gh_summary "cppcheck: no findings."
exit 0
