#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_card_anchor_coverage.sh
# Card-anchor sweep COVERAGE lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT: a HUD that anchors content on its CARD box -- any user of the
# PanelPlan::sectionBox* accessors -- is swept by card_anchor_sweep_test.
#
# WHY THIS EXISTS. CLAUDE.md carried the rule as prose ("a new HUD joins its
# target list") explicitly labelled CONVENTION, and the convention silently
# failed: four panels used the accessors and were never swept. One of them was
# VersionWidget, which turned out to have a real anchor defect of its own --
# found by hand on graph paper, months later, because no sweep could see it.
#
# The list is hand-maintained by necessity (each entry needs a harness HudId),
# so this closes the loop the only way that scales: the SOURCE decides who
# belongs, and a panel that anchors on its card without joining the sweep fails
# here with the name to add.
#
# Exit 3 = skip (a missing input), matching the CTest convention.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
SWEEP="${ROOT}/tests/integration/tests/card_anchor_sweep_test.cpp"
HUDDIR="${ROOT}/mxbmrp3/hud"

[ -f "${SWEEP}" ] || { echo "SKIP: ${SWEEP} not found" >&2; exit 3; }
[ -d "${HUDDIR}" ] || { echo "SKIP: ${HUDDIR} not found" >&2; exit 3; }

# The sweep's target names, lowercased with separators stripped, so
# "gap_bar_hud.cpp" and the target "gapbar" compare equal.
norm() { tr -d '_' | tr '[:upper:]' '[:lower:]'; }
targets="$(grep -oE '\{PluginHost::HUD_[A-Z_]+, *"[a-z_]+"\}' "${SWEEP}" \
           | grep -oE '"[a-z_]+"' | tr -d '"' | norm | sort -u)"
[ -n "${targets}" ] || { echo "ERROR: parsed no targets from card_anchor_sweep_test.cpp." >&2
                         echo "       Target table reshaped? This lint is now blind." >&2; exit 1; }

rc=0
for f in "${HUDDIR}"/*.cpp; do
    grep -q "sectionBox" "$f" || continue
    base="$(basename "$f" .cpp)"
    # settings_hud_render.cpp -> settings; standings_hud_build.cpp -> standings
    stem="$(echo "${base}" | sed -E 's/_(hud|widget)(_[a-z]+)?$//' | norm)"
    if ! echo "${targets}" | grep -qx "${stem}"; then
        echo "ERROR: ${base}.cpp anchors on its card box (PanelPlan::sectionBox*)" >&2
        echo "       but '${stem}' is not swept by card_anchor_sweep_test.cpp." >&2
        echo "       Add a target row (and a harness HudId if it has none)." >&2
        rc=1
    fi
done
[ $rc -eq 0 ] && echo "Card-anchor coverage: every sectionBox* user is swept."
exit $rc
