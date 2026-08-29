#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_file_budgets.sh
# File-size ratchet for first-party C++ sources (no compiler needed, pure wc).
#
# THE INVARIANT: a source file does not quietly regrow into a god file. This
# project splits overgrown files into focused TUs with byte-identical moves
# (plugin_data #276, settings_manager #268, http_server #294, and the
# spotter_manager / base_hud_render / asset_manager round) — and then nothing
# watched the results: base_hud_render.cpp was CREATED by such a split at
# ~660 lines and regrew to 2,402 before anyone measured again. Growth arrives
# a hundred well-reviewed lines at a time; no single diff ever looks like the
# problem. So the budget is enforced where the growth lands, not remembered.
#
# THE RULE: every mxbmrp3/**/*.{cpp,h} outside vendor/ stays under
# DEFAULT_BUDGET lines. A file that must exceed it carries, in its first 40
# lines, an annotation stating its own ceiling and why:
#
#     // file-budget: 1600 settings geometry tables; split planned with the tab rework
#
# and fails the moment it exceeds the number it states. Raising the number is
# a visible, reviewable edit to the file itself — the same shape as
# api-guard-exempt / vis-gate / mt-plain — so a god file can only happen as a
# conscious decision with a name on it, never as drift. An annotation on a
# file back under the default is itself a failure: the ledger stays true, and
# a successful split tightens the ratchet behind it.
#
# WHY BESPOKE (CLAUDE.md: prefer the off-the-shelf tool): no tool in this
# project's stack gates FILE length against a per-file budget — clang-tidy's
# readability-* checks bound function size, cppcheck has no length checks,
# and cpplint caps line WIDTH. The web overlay's JS does not need this script:
# eslint ships max-lines, and the eslint gate enforces it there (standard tool
# where one exists).
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

DEFAULT_BUDGET=1000
ANNOTATION_WINDOW=40   # annotation must sit in the file's header, where a reader lands
# A scan that matched nothing would pass every check vacuously — the failure
# mode of every source-scanning gate (the spotter census's `found >= 10` floor
# exists for the same reason). The tree has ~360 first-party sources today;
# well under half of that means the find below is looking at the wrong place.
MIN_SCANNED=100

fail=0
scanned=0
while IFS= read -r f; do
    scanned=$((scanned + 1))
    lines=$(wc -l < "$f")
    budget=$(head -n "${ANNOTATION_WINDOW}" "$f" \
             | sed -n 's|^.*// file-budget: *\([0-9][0-9]*\).*$|\1|p' | head -n 1)

    if [ -n "${budget}" ]; then
        if [ "${lines}" -le "${DEFAULT_BUDGET}" ]; then
            echo "FAIL: ${f} (${lines} lines) carries a file-budget annotation it no longer needs --"
            echo "      it fits the default ${DEFAULT_BUDGET}. Remove the annotation; the ratchet tightens."
            fail=1
        elif [ "${budget}" -le "${DEFAULT_BUDGET}" ]; then
            echo "FAIL: ${f} is ${lines} lines but states a budget of ${budget}, at or under the"
            echo "      default ${DEFAULT_BUDGET} -- a stated budget only means something above it."
            echo "      Split the file back under ${DEFAULT_BUDGET}, or state the real ceiling."
            fail=1
        elif [ "${lines}" -gt "${budget}" ]; then
            echo "FAIL: ${f} is ${lines} lines, over its own stated budget of ${budget}."
            fail=1
        fi
    elif [ "${lines}" -gt "${DEFAULT_BUDGET}" ]; then
        echo "FAIL: ${f} is ${lines} lines, over the ${DEFAULT_BUDGET}-line default with no budget stated."
        fail=1
    fi
done < <(find mxbmrp3 -path mxbmrp3/vendor -prune -o \
              \( -name '*.cpp' -o -name '*.h' \) -print | sort)

if [ "${scanned}" -lt "${MIN_SCANNED}" ]; then
    echo "FAIL: only ${scanned} source files scanned (expected at least ${MIN_SCANNED}) --"
    echo "      the source root moved or the find pattern rotted; this gate is not checking anything."
    exit 1
fi

if [ "${fail}" -ne 0 ]; then
    echo ""
    echo "  A file over budget wants a SPLIT, not a bigger number: move whole method"
    echo "  bodies verbatim into focused <file>_<topic>.cpp TUs (class definitions and"
    echo "  the export surface unchanged; shared file-locals promoted to a *_internal.h)."
    echo "  See the plugin_data / http_server / spotter_manager splits for the pattern."
    echo "  Genuinely can't split? State the ceiling and the reason in the header:"
    echo "      // file-budget: <N> <reason>"
    exit 1
fi
echo "file budgets OK: no first-party source over ${DEFAULT_BUDGET} lines without a stated ceiling, none over its own"
