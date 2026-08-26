#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_title_tier.sh
# A full HUD's caption is the LARGE tier; a widget's is Normal.
#
# WHY A LINT. PanelWant::tier is opt-in and defaults to Normal, which is right
# for the fifteen gauges and wrong for the twelve panels with tables in them --
# and it has to be said TWICE, once to planTitleWidth() so the caption is
# measured at the right width and once as want.tier so it is drawn at it.
# GapBarHud, TimingHud and NoticesHud had said neither since the plan chain
# landed, and wore the gauge caption next to siblings that had. Nothing could
# see it: the panel lays out consistently at whichever size it picked, so every
# geometry assertion passes either way. It is only wrong NEXT TO something else.
#
# The rule is by FILE NAME, which is the same signal HudManager uses to decide
# what a thing is: *_hud.cpp is a panel, *_widget.cpp is a gauge.
# ============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fail=0

for f in "${ROOT}"/mxbmrp3/hud/*_hud.cpp "${ROOT}"/mxbmrp3/hud/*_hud_render.cpp; do
    [ -e "$f" ] || continue
    grep -q "want\.captionW" "$f" || continue          # not a plan panel
    name="$(basename "$f")"
    # base_hud_render.cpp is the plan itself, not a panel that uses it.
    [ "$name" = "base_hud_render.cpp" ] && continue
    if ! grep -q "want\.tier *= *TitleTier::Large" "$f"; then
        echo "FAIL: ${name} is a HUD but never asks for TitleTier::Large --"
        echo "      its caption draws at the widget size (PanelWant::tier defaults to Normal)."
        fail=1
    fi
    if grep -q "planTitleWidth([^)]*)" "$f" && \
       ! grep -q "planTitleWidth([^)]*TitleTier::Large" "$f"; then
        echo "FAIL: ${name} measures its caption without TitleTier::Large --"
        echo "      the width is measured at one size and drawn at another."
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "title tiers OK: every plan HUD asks for Large, and measures at it"
fi
exit "$fail"
