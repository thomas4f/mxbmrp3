#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_codeql.sh — run GitHub's CodeQL security queries over
# the C++ tree locally, so a finding is caught before it ships.
#
# WHY THIS EXISTS. codeql.yml can only run on the PUBLIC mirror (code scanning
# needs Advanced Security, which the private repo doesn't have), and the mirror
# only receives code at RELEASE time. So the first CodeQL scan of any change was
# also its release: v1.28.0 shipped, then three alerts appeared against code that
# had been in main for weeks. This gate closes that window — same queries, same
# database-from-the-cross-build recipe, runnable before the tag exists.
#
# NOT off-the-shelf-able further: this IS the off-the-shelf tool (the CodeQL CLI
# and its own query packs). The only bespoke part is the ~20 lines of glue that
# point it at build.sh and filter vendored code, mirroring what codeql.yml does.
#
# COST. ~10-15 min: a clean cross-build under the extractor, then query
# evaluation. That is why it is `slow`, excluded from -L fast, and not something
# to put in an edit-compile-test loop. Run it before a release, or after touching
# anything security-shaped (a parser, a boundary, a new dependency).
#
#   ./tests/integration/run_codeql.sh            # full scan, security queries
#   ./tests/integration/run_codeql.sh --keep-db  # reuse/keep the database
#
# The GitHub scan stays authoritative: it runs the full default query suite on
# every mirror push and weekly, and it is what files the alerts. This is the
# early-warning copy.
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
DB_DIR="${ROOT}/build/codeql-db"
OUT="${ROOT}/build/codeql-results.sarif"

if ! command -v codeql >/dev/null 2>&1; then
    echo "ERROR: codeql not found. Install with:" >&2
    echo "  ./tools/install_deps.sh codeql" >&2
    exit 1
fi
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "ERROR: mingw-w64 not found (the database is built from the cross-build). Install with:" >&2
    echo "  ./tools/install_deps.sh mingw" >&2
    exit 1
fi

KEEP_DB=0
[ "${1:-}" = "--keep-db" ] && KEEP_DB=1

# The extractor observes compiler invocations, so it needs a CLEAN build — an
# incremental one compiles nothing and yields an empty database that reports zero
# findings and looks like a pass. -B is not optional here.
if [ "${KEEP_DB}" -eq 0 ] || [ ! -d "${DB_DIR}" ]; then
    rm -rf "${DB_DIR}"
    echo "==> building CodeQL database (clean cross-build, several minutes)"
    # CCACHE_DISABLE is NOT optional. CMakeLists.txt wires ccache in as
    # CMAKE_CXX_COMPILER_LAUNCHER, and CodeQL extracts by tracing real compiler
    # executions — so every TU ccache serves from cache is a TU CodeQL never
    # sees. With a warm cache (any prior build.sh, incl. the one the persist gate
    # runs) even `-B` extracts NOTHING: the build prints a full set of
    # "Building CXX object" lines, exits 0, and leaves an empty database that
    # analyzes to zero findings. That is a false PASS, which is worse than no
    # gate at all — it is how this script passed on its first real run.
    CCACHE_DISABLE=1 codeql database create "${DB_DIR}" \
        --language=cpp \
        --source-root="${ROOT}" \
        --command="${ROOT}/tests/integration/build.sh -B"
fi

# Belt-and-braces for the same failure: a database with no extracted C++ has no
# db-cpp/ and reports zero findings indistinguishably from a clean tree. Fail
# loudly instead. (CodeQL prints "could not process any of it" and still exits 0.)
if [ ! -d "${DB_DIR}/db-cpp" ]; then
    echo "ERROR: CodeQL extracted no C++ - the database is empty, so a 'no findings'" >&2
    echo "       result would be meaningless. Usually the build compiled nothing the" >&2
    echo "       tracer could see (ccache serving cached objects, or an up-to-date" >&2
    echo "       tree). Check ${DB_DIR}/log/ for the extractor's own diagnosis." >&2
    exit 1
fi

# cpp-code-scanning.qls is the suite the codeql-action runs when a workflow names
# no `queries:` — which codeql.yml doesn't. Matching it is the point: a broader
# suite (security-extended) would fail this gate on findings that never appear in
# GitHub's code scanning, and a gate that disagrees with the authority gets muted.
echo "==> running security queries (same suite as codeql.yml)"
codeql database analyze "${DB_DIR}" \
    codeql/cpp-queries:codeql-suites/cpp-code-scanning.qls \
    --format=sarif-latest \
    --output="${OUT}" \
    --threads=0 2>&1 | tee "${ROOT}/build/codeql-analyze.log"

# The db-cpp check above catches a TOTALLY empty database; this catches a
# PARTIAL one, which is the failure that actually happened. A run that extracted
# 42 of 444 files reported "no findings" and exited 0, indistinguishable from a
# real pass - the code the finding lived in simply wasn't in the database. Any
# under-extraction (a build that mostly no-op'd, a compile error swallowed
# mid-run) reads as good news, which is the worst possible failure mode for a
# gate you consult before a release.
#
# The floor is deliberately blunt: a healthy run scans ~307 of 444 (the rest are
# headers and TUs the cross-build excludes), so anything below 250 means the
# build didn't do what this script assumes. Raise it if the tree grows; do NOT
# lower it to make a run pass.
# `|| true` is load-bearing under `set -euo pipefail`: with no matching line the
# grep pipeline exits 1, which fails the ASSIGNMENT and aborts the script right
# here — before the -z branch below can say why. The gate still failed, but
# silently and with no diagnosis, which is the opposite of what that branch is
# for. Swallow the pipeline's status so the empty-result path stays reachable.
SCANNED="$(grep -oE 'scanned [0-9]+ out of [0-9]+' "${ROOT}/build/codeql-analyze.log" \
           | grep -oE '^scanned [0-9]+' | grep -oE '[0-9]+' | head -1 || true)"
if [ -z "${SCANNED}" ]; then
    echo "ERROR: could not read CodeQL's scanned-file count - cannot confirm the" >&2
    echo "       database is complete, so a 'no findings' result is not trustworthy." >&2
    exit 1
fi
if [ "${SCANNED}" -lt 250 ]; then
    echo "ERROR: CodeQL scanned only ${SCANNED} files; a complete run scans ~307." >&2
    echo "       The database is PARTIAL, so 'no findings' would be a false pass." >&2
    echo "       Usual cause: the build compiled little or nothing under the tracer" >&2
    echo "       (stale build tree, or ccache serving objects despite CCACHE_DISABLE)." >&2
    echo "       Re-run without --keep-db; check ${DB_DIR}/log/ for the extractor log." >&2
    exit 1
fi
echo "  extraction ok: ${SCANNED} files in the database"

# Same vendored-code exclusion codeql.yml applies to the uploaded SARIF: we do
# not patch cpp-httplib, so its findings are permanent noise. Kept as a filter
# here rather than in the query set so both sides drop the SAME paths.
python3 - "${OUT}" "${HERE}/codeql_baseline.txt" <<'PY'
import json, os, sys

EXCLUDE = ("mxbmrp3/vendor/", "tests/integration/harness/doctest.h")

# Accepted findings, so the gate fails only on NEW ones. See the file's header.
#
# Third field (optional but strongly preferred) is a SOURCE path substring. A
# taint result is keyed on its SINK, and a sink like logger.cpp is the sink for
# every log line in the plugin — so a (rule, sink) entry would also swallow a
# future flow carrying something genuinely sensitive into an unrelated
# DEBUG_INFO_F. With a source given, the entry covers the result only while
# EVERY data-flow path in it starts somewhere matching; a new source appears as
# an extra codeFlow, fails to match, and the finding goes back to failing.
baseline = []
if os.path.exists(sys.argv[2]):
    for line in open(sys.argv[2], encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            parts = line.split()
            baseline.append((parts[0], parts[1], parts[2] if len(parts) > 2 else None))


def flow_sources(result):
    """Every data-flow path's first location — the taint sources of a result."""
    out = []
    for cf in result.get("codeFlows", []):
        for tf in cf.get("threadFlows", []):
            steps = tf.get("locations", [])
            if steps:
                out.append(steps[0]["location"]["physicalLocation"]
                           ["artifactLocation"]["uri"])
    return out


def baselined(result, rule, uri):
    for b_rule, b_sink, b_src in baseline:
        if rule != b_rule or not uri.startswith(b_sink):
            continue
        if b_src is None:
            return True
        srcs = flow_sources(result)
        # No paths at all (a non-taint query) can't be narrowed by source, so a
        # source-qualified entry deliberately does NOT cover it.
        if srcs and all(b_src in s for s in srcs):
            return True
    return False

with open(sys.argv[1]) as f:
    sarif = json.load(f)

kept, dropped, known = [], 0, []
for run in sarif.get("runs", []):
    for r in run.get("results", []):
        locs = r.get("locations", [])
        uri = (locs[0]["physicalLocation"]["artifactLocation"]["uri"]
               if locs else "")
        if uri.startswith(EXCLUDE):
            dropped += 1
            continue
        rule = r.get("ruleId", "?")
        line = locs[0]["physicalLocation"].get("region", {}).get("startLine", "?") if locs else "?"
        entry = (rule, uri, line, r.get("message", {}).get("text", ""))
        if baselined(r, rule, uri):
            known.append(entry)
            continue
        kept.append(entry)

if dropped:
    print(f"  ({dropped} finding(s) in vendored code filtered out)")

# Named, never silently swallowed: a baseline you cannot see is a baseline that
# quietly grows stale and hides a finding it was never meant to cover.
for rule, uri, line, _ in known:
    print(f"  KNOWN (baselined): [{rule}] {uri}:{line}")

if not kept:
    print(f"==> PASS: no new findings ({len(known)} baselined, {dropped} vendored)")
    sys.exit(0)

print(f"==> FAIL: {len(kept)} finding(s)\n")
for rule, uri, line, msg in kept:
    print(f"  [{rule}] {uri}:{line}")
    print(f"      {msg.splitlines()[0][:160]}\n")
print("Full SARIF (with data-flow paths): " + sys.argv[1])
sys.exit(1)
PY
