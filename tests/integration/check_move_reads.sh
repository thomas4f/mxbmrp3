#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_move_reads.sh
# Argument-evaluation-order lint: no call may pass `std::move(x)` and also read
# a member off `x` in the SAME call.
#
# THE BUG THIS EXISTS FOR, in full, because it is invisible three ways over:
#
#     emitCue(key,
#             std::string("Up ") + v.positionsChanged + ".",   // reads v
#             cat, std::move(v), nowMs);                       // and guts v
#
# emitCue takes its Vars BY VALUE, so constructing that parameter moves out of
# `v`. The order in which a compiler evaluates the arguments of a call is
# UNSPECIFIED (C++17 makes them indeterminately sequenced, not ordered), so
# whether the text sees the number or a moved-from empty string is the
# compiler's choice. gcc happened to evaluate left to right and produce the
# right answer; MSVC evaluates right to left and shipped
# `SPOTTER SAY [position_gained] Up .` to a real player.
#
# WHY A LINT AND NOT A TEST. Every gate in this repo compiles with gcc or
# clang, where this code is correct — so a behavioural test asserting the right
# text passes with the bug present, and would keep passing forever. The only
# build that gets it wrong is the shipping one, which no gate here runs. That
# leaves the source itself as the only place the mistake is visible.
#
# NOR A COMPILER WARNING. It is not undefined behaviour and not an unsequenced
# modification, so -Wsequence-point and clang's -Wunsequenced both stay quiet:
# each argument IS sequenced, just in an order nobody promised.
#
# THE OFF-THE-SHELF CANDIDATE (CLAUDE.md's rule): clang-tidy's
# bugprone-use-after-move flags this shape — it treats a move and a use in the
# same call as use-after-move precisely because the order is indeterminate.
# Evaluated and not adopted: the project runs no clang-tidy today, so adopting
# the check means standing up tidy (a compile database from the mingw
# cross-build, per-header noise triage) for one rule this grep-sized script
# already enforces at zero false positives. The day tidy joins the gates for
# other reasons, this script is the first thing to delete.
#
# THE FIX IS ALWAYS THE SAME: compute into a local first, and pass the local.
#
#     const std::string changed = ...;
#     v.positionsChanged = changed;
#     emitCue(key, "Up " + changed + ".", cat, std::move(v), nowMs);
#
# SCOPE. Statement-level and deliberately simple: for each `std::move(ident)`,
# the enclosing statement (previous `;`/`{` to the next `;`) must not also
# mention `ident.` or `ident->`. That is narrow enough to stay at zero false
# positives on this tree — a read in a LATER statement is properly sequenced
# and is not flagged — and it catches the shape that actually bit.
#
#   ./tests/integration/check_move_reads.sh
#   ./tests/integration/check_move_reads.sh --self-test
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# The lint's own must-catch / must-spare fixtures. A matcher that quietly stops
# matching is the failure mode of every source-scanning check, and this one has
# no live offender left to notice it by.
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d)
    trap 'rm -rf "${tmp}"' EXIT
    mkdir -p "${tmp}/src"
    cat > "${tmp}/src/must_catch.cpp" <<'EOF'
void f() {
    emitCue(key, std::string("Up ") + v.positionsChanged + ".",
            cat, std::move(v), nowMs);
}
EOF
    cat > "${tmp}/src/must_pass.cpp" <<'EOF'
void f() {
    const std::string changed = words;
    v.positionsChanged = changed;
    emitCue(key, "Up " + changed + ".", cat, std::move(v), nowMs);
    // A read in a LATER statement is sequenced after the call, so it is fine.
    sink(std::move(other));
    other.clear();
}
EOF
    if MXB_SCAN_ROOT="${tmp}/src" "$0" >/dev/null 2>&1; then
        echo "SELF-TEST FAIL: the must-catch fixture was not flagged"
        exit 1
    fi
    rm "${tmp}/src/must_catch.cpp"
    if ! MXB_SCAN_ROOT="${tmp}/src" "$0" >/dev/null 2>&1; then
        echo "SELF-TEST FAIL: the must-spare fixture was flagged"
        MXB_SCAN_ROOT="${tmp}/src" "$0" || true
        exit 1
    fi
    echo "Move-read lint self-test passed (catches the shape, spares the fix)."
    exit 0
fi

ROOT="${MXB_SCAN_ROOT:-mxbmrp3}"

python3 - "${ROOT}" <<'PY'
import os, re, sys

root = sys.argv[1]
move_re = re.compile(r'std::move\(\s*([A-Za-z_]\w*)\s*\)')
bad = []
scanned = 0

for dirpath, dirnames, filenames in os.walk(root):
    if 'vendor' in dirpath.split(os.sep):
        continue
    for name in sorted(filenames):
        if not name.endswith(('.cpp', '.h')):
            continue
        path = os.path.join(dirpath, name)
        src = open(path, encoding='utf-8', errors='replace').read()
        scanned += 1
        for m in move_re.finditer(src):
            ident = m.group(1)
            # The enclosing statement: back to the previous ; { or }, forward
            # to the next ;. Comments are stripped so prose about a move does
            # not read as code.
            start = max(src.rfind(c, 0, m.start()) for c in ';{}') + 1
            end = src.find(';', m.end())
            if end == -1:
                end = len(src)
            stmt = src[start:end]
            stmt = re.sub(r'//[^\n]*', '', stmt)
            if re.search(r'\b' + re.escape(ident) + r'\s*(\.|->)', stmt):
                line = src.count('\n', 0, m.start()) + 1
                bad.append((path, line, ident))

for path, line, ident in bad:
    print(f"{path}:{line}: passes std::move({ident}) and reads {ident}. "
          f"in the same call")

if bad:
    print("""
ARGUMENT-ORDER LINT FAILED.
Each call above both moves out of a variable and reads a member off it. The
order arguments are evaluated in is unspecified, so the read may see a
moved-from value — and which one you get depends on the compiler, not on the
code. gcc and MSVC disagree here, and only MSVC ships.

Compute into a local first and pass the local:

    const std::string changed = ...;
    v.positionsChanged = changed;
    emitCue(key, "Up " + changed + ".", cat, std::move(v), nowMs);
""")
    sys.exit(1)

print(f"Move-then-read clean ({scanned} files checked).")
PY
