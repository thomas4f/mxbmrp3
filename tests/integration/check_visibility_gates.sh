#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_visibility_gates.sh
# Visibility-gate lint for HUD sources (no compiler needed, pure grep/awk).
#
# THE INVARIANT (CLAUDE.md "Maintenance Invariants"): a HUD that skips its
# rebuild/tick when hidden must gate on isVisibleAnySurface() — visible
# in-game OR on the companion window — never isVisible()/m_bVisible alone.
# A HUD enabled only on the companion otherwise renders STALE (the reported
# "gap bar / map / telemetry don't update on the companion" bug class). The
# gate has several shapes (early-out, inverted, compound, member-form), which
# is exactly why new HUDs keep reintroducing it — so this check flags EVERY
# conditional read of isVisible()/m_bVisible in the scanned set (see SCOPE below) and requires
# each legitimate one to carry an explicit `vis-gate:` annotation (same line
# or the line above) stating WHY the game-surface flag alone is correct, e.g.:
#
#     if (!m_bVisible) return;  // vis-gate: menu is active-surface-only
#
# Legitimate cases are rare and deliberate: the settings menu and mouse
# pointer render only on the ACTIVE surface (special-cased in
# HudManager::collectSurface), setVisible() overrides doing transition
# detection on the game toggle itself, and BaseHud::isVisibleAnySurface()'s
# own implementation. Everything else should use isVisibleAnySurface().
#
# Not matched (by design): plain assignments (`m_bVisible = false;`),
# constructor inits (`m_bVisible(false)`), setVisible(...) calls,
# isVisibleAnySurface() itself, comment-only mentions, and cross-HUD reads
# (`otherHud->isVisible()` — the settings UI showing/toggling ANOTHER HUD's
# game flag is what that flag is for; the invariant is about a HUD gating its
# OWN rebuild/tick).
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

fail=0
# SCOPE: the HUD sources, plus the DATA-PRODUCTION sites in core/. The invariant
# has two halves and both must ask the same question — a HUD gating its rebuild
# on isVisibleAnySurface() still renders empty if whatever FILLS its buffers
# gates on the game flag alone (exactly what HudManager::isTelemetryHistoryNeeded()
# did: companion-only telemetry rebuilt from buffers nobody filled).
#
# Deliberately NOT scanned: hud_manager_input.cpp / hud_manager_render.cpp /
# settings_manager_global.cpp / test_hooks.cpp. Their ~54 isVisible() reads are
# hotkey toggles, per-surface render routing, persistence and test accessors —
# all correct uses of the game flag, and annotating every one would bury the
# signal this check exists to raise. If a new file starts gating WORK on a HUD's
# visibility, add it here.
for f in mxbmrp3/hud/*.cpp mxbmrp3/core/hud_manager.cpp mxbmrp3/core/plugin_data*.cpp; do
    # Cross-HUD reads (`otherHud->isVisible()`) are exempt in HUD sources — the
    # settings UI showing/toggling ANOTHER HUD's game flag is what that flag is
    # for. They are NOT exempt in the data-production files, where reading some
    # HUD's game flag to decide whether to produce its data is precisely the bug
    # (isTelemetryHistoryNeeded). Without this split the widened scope would be
    # decorative: the exemption would swallow the very call it was widened for.
    crosshud=1
    [[ "$f" == mxbmrp3/core/* ]] && crosshud=0
    out=$(awk -v crosshud="$crosshud" '
        {
            raw = $0
            line = $0
            sub(/\/\/.*/, "", line)              # ignore comment text
            # A vis-gate: annotation covers its own line and carries across a
            # consecutive comment block to the first code line that follows.
            if (raw ~ /vis-gate:/) pending = 1
            iscomment = (line ~ /^[ \t]*$/ && raw ~ /\/\//)
            annotated = pending
            if (!iscomment) pending = 0
            if (line !~ /isVisible\(\)|m_bVisible/) next
            # strip the allowed forms, then see if a bare read remains
            gsub(/isVisibleAnySurface\(\)/, "", line)
            gsub(/setVisible\(/, "", line)
            if (crosshud) {
                gsub(/->[ \t]*isVisible\(\)/, "", line)   # cross-HUD read (settings UI
                gsub(/\.[ \t]*isVisible\(\)/, "", line)   # showing/toggling another HUD)
            }
            gsub(/m_bVisible[ \t]*=[^=]/, "", line)   # assignment (not ==)
            gsub(/m_bVisible\(/, "", line)            # ctor init list
            if (line !~ /isVisible\(\)|m_bVisible/) next
            if (annotated) next
            printf "  %s:%d: %s\n", FILENAME, FNR, raw
        }
    ' "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

VISIBILITY-GATE LINT FAILED.
Each line above reads isVisible()/m_bVisible directly. If it
gates a rebuild/tick, use isVisibleAnySurface() instead (a HUD enabled only on
the companion window must still update — see CLAUDE.md Maintenance Invariants).
If the game-surface flag really is correct there (active-surface-only element,
toggle-transition logic), say why with a `// vis-gate: <reason>` annotation on
the same line or the line above.
EOF
    exit 1
fi
echo "Visibility gates clean ($(ls mxbmrp3/hud/*.cpp mxbmrp3/core/hud_manager.cpp mxbmrp3/core/plugin_data*.cpp | wc -l) sources checked)."
