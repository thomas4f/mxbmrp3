#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_section_spacing.sh
# Settings-tab section-spacing lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT: SettingsLayoutContext::addSectionHeading() owns the gap above
# itself. A ctx.addSpacing() immediately before a ctx.addSectionHeading() adds a
# SECOND gap, so that boundary costs SECTION_GAP_LINES + SECTION_PAD_LINES +
# whatever the spacing was, instead of the first two.
#
# WHY A LINT AND NOT A CONSTRUCTION. addSectionHeading only ever sees the cursor
# AFTER the caller has moved it, so from in there a stray addSpacing and a
# legitimately tall preceding row are indistinguishable. Absorbing it would mean
# tracking a per-row bottom through every control helper -- a lot of surface for
# a rule one grep catches. (Tried the obvious shortcut first: snapping currentY
# to its value at function entry. That value already includes the stray spacing,
# so it is exactly equivalent to += and fixes nothing.)
#
# THE BUG IT PINS. The gap was added to addSectionHeading when section cards
# landed; the 29 addSpacing(0.5f) calls that already preceded section headers
# were left in place. Every section boundary silently cost 1.18 line heights
# instead of 0.5, which pushed the tallest tabs (Appearance, Widgets, Hotkeys)
# off the bottom of the settings panel. Nothing failed -- the panel just
# overflowed, and it took a user noticing to find it.
#
# Exit codes: 0 = clean, 1 = violations found, 3 = SKIPPED (nothing to scan).
# ============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCAN_DIR="${ROOT}/mxbmrp3/hud/settings"

if [ ! -d "${SCAN_DIR}" ]; then
    echo "SKIP: ${SCAN_DIR} not found"
    exit 3
fi

mapfile -t FILES < <(find "${SCAN_DIR}" -name 'settings_tab_*.cpp' | sort)
if [ "${#FILES[@]}" -eq 0 ]; then
    echo "SKIP: no settings_tab_*.cpp to scan"
    exit 3
fi

violations=0

for f in "${FILES[@]}"; do
    # An addSpacing() whose next NON-BLANK, NON-COMMENT line calls
    # addSectionHeading(). Blank lines and comments between the two are still a
    # violation -- they change nothing about the emitted geometry.
    awk -v file="$(realpath --relative-to="${ROOT}" "$f")" '
        # ONLY addSpacing() OPENS A PENDING GAP, and the boundary is deliberate.
        #
        # A hand-rolled gap (`ctx.currentY += ctx.lineHeightNormal * 0.3f;` on the Riders
        # tab) is the same defect and this rule cannot see it. Opening on any
        # `ctx.currentY +=` was tried and reverted: it flags 13 sites, and 12 are
        # legitimate -- an advance past a row the caller drew by hand, which is
        # indistinguishable from a gap without knowing whether anything was emitted into
        # that row. A rule needing twelve exemptions is one somebody deletes, which is the
        # failure recorded in the header of the sibling lint.
        #
        # So the hand-rolled shape stays a review item rather than a bad check. The one
        # instance in the tree is fixed; a second would have to be caught by eye.
        /addSpacing\(/ {
            pending = NR; pending_text = $0; next
        }
        pending {
            line = $0
            sub(/^[ \t]+/, "", line)
            # Skip anything that emits nothing: blanks, comments, and plain
            # statements (a local declaration between the two still leaves the
            # boundary double-spaced). Only a ctx.* call is real content.
            #
            # This hole was real: the Director tab had an addSpacing separated from
            # its addSectionHeading by a comment block AND a `const bool rotationOn
            # = ...`, so the first version walked past the comments, hit the
            # declaration and gave up.
            # EMITS, not merely "mentions ctx". The previous test was `line ~ /ctx\./`,
            # which is satisfied by a local DECLARATION that happens to read something off
            # the context -- `float rowWidth = ctx.rowSpanWidth();`. Such a line then fell
            # through to `pending = 0` and cancelled the check, so the rule went blind to
            # three real violations sitting in the tree (Rumble, Stats, Riders), and a
            # fourth added today would also have passed CI.
            #
            # That is this rule repeating its own history: the FIRST version gave up on any
            # non-ctx line, the fix was "only a ctx.* call is real content", and the fix
            # inherited the same shape of hole one step along. What actually ends a pending
            # gap is a call that PLACES something -- ctx.add*, or a direct write to the
            # cursor. Everything else (declarations, reads, comments, blanks) is transparent.
            if (line == "" || line ~ /^\/\//) next
            if (line !~ /ctx\.(add|currentY)/ && line !~ /parent->add/) next
            if (line ~ /addSectionHeading\(/) {
                printf "%s:%d: addSpacing() immediately before addSectionHeading()\n", file, pending
                printf "    %s\n", pending_text
                printf "  addSectionHeading owns the gap above itself -- delete the addSpacing.\n"
                bad++
            }
            pending = 0
        }
        END { exit (bad ? 1 : 0) }
    ' "$f" || violations=$((violations + 1))
done

# SECOND RULE: a hand-written copy of the row span.
#
# `panelWidth - (labelX - contentAreaStartX)` is the OLD, asymmetric row width -- it
# reaches the card's right BORDER instead of stopping one inset short of it, so a row
# using it highlights wider than its neighbours. SettingsLayoutContext::rowSpanWidth()
# is the definition.
#
# A grep because copies are what happened: the expression was fixed in the layout
# context and five tab files were still carrying their own, so most of the General
# tab was right, the Web Server row was not, and whole tabs were wrong. Nobody can
# see that in a diff of the file being edited.
span_violations=0
for f in "${FILES[@]}"; do
    rel="$(realpath --relative-to="${ROOT}" "$f")"
    if grep -n 'panelWidth[[:space:]]*-[[:space:]]*(.*labelX[[:space:]]*-.*contentAreaStartX' "$f"; then
        echo "  ^ ${rel}: hand-rolled row span -- use ctx.rowSpanWidth()"
        echo "    That expression stops at the card's right BORDER; rowSpanWidth() stops"
        echo "    one label-column inset short of it, matching the left."
        span_violations=$((span_violations + 1))
    fi
done
violations=$((violations + span_violations))

# THIRD RULE: a sectioned HUD pads its panel like an unsectioned one.
#
# A HUD that draws per-section cards (beginContentSection) still has to spend
# dims.paddingV at BOTH ends of its panel. Users tile HUDs side by side, and two
# panels line up only when their heights differ by whole rows -- which needs
# identical padding above and below. BaseHud::finishContentSections() is what makes
# the last card end where that padding expects.
#
# The bug: Performance and Session Charts briefly used a smaller bottom pad
# (sectionCardPaddingY + frameMargin) to close a gap under the last card. It closed the
# gap and left both panels ~19px -- 1.5 grid cells -- shorter than a Standings or a
# Stats holding the same content, so they could never line up with anything. The
# gap belonged to the CARD, not the panel.
mapfile -t HUD_FILES < <(grep -rl 'beginContentSection(' "${ROOT}/mxbmrp3/hud" --include='*.cpp' | sort)
pad_violations=0
for f in "${HUD_FILES[@]}"; do
    rel="$(realpath --relative-to="${ROOT}" "$f")"
    # Every backgroundHeight assignment must spend the padding at BOTH ends, in one
    # of two forms:
    #
    #   panelHeight(dim, content)   -- the helper, which IS `paddingV + x + paddingV`.
    #                                  Accepted outright: it cannot spend one end
    #                                  without the other, so it satisfies this rule by
    #                                  construction rather than by inspection.
    #   two literal paddingV terms  -- the longhand, for a statement the helper does
    #                                  not fit.
    #
    # The helper arm is not a loosening, it is the rule getting stronger where it
    # applies. It was added because the panel-box conversion routed every sectioned
    # HUD through panelHeight() and this lint failed them all: it was pinned to a
    # SPELLING of the invariant rather than to the invariant, which is the same fault
    # rule 7 in check_hud_helpers.sh had (it banned one spelling of the title row and
    # exempted another that turned out to be wrong). Counting literals is what a grep
    # can do when there is no helper; once there is one, naming it is better.
    #
    # Read the WHOLE statement, not the matched line: an assignment wrapped across
    # lines (this codebase wraps a lot of them, including the two HUDs this rule
    # exists for) would otherwise show one paddingV and false-positive. awk collects
    # from `backgroundHeight =` to the terminating semicolon.
    while IFS=$'\t' read -r lineno text; do
        [ -z "${lineno}" ] && continue
        if printf '%s' "${text}" | grep -q 'panelHeight('; then
            continue
        fi
        count="$(printf '%s' "${text}" | grep -o 'paddingV' | wc -l)"
        if [ "${count}" -lt 2 ]; then
            echo "${rel}:${lineno}: sectioned HUD panel height without paddingV at both ends"
            echo "    ${text}"
            echo "  A sectioned panel pads like every other HUD, or it cannot tile with one."
            echo "  The gap under the last card belongs to finishContentSections(), not here."
            pad_violations=$((pad_violations + 1))
        fi
    done < <(awk '
        /backgroundHeight[[:space:]]*=/ { buf = $0; start = NR; collecting = 1 }
        collecting {
            if (NR != start) buf = buf " " $0
            if (buf ~ /;/) { gsub(/\t/, " ", buf); print start "\t" buf; collecting = 0 }
        }
    ' "$f")
done
violations=$((violations + pad_violations))

if [ "${violations}" -ne 0 ]; then
    echo ""
    echo "FAIL: ${violations} problem(s)."
    echo "      See SettingsLayoutContext::addSectionHeading() / rowSpanWidth()"
    echo "      and BaseHud::finishContentSections()."
    exit 1
fi

echo "PASS: no section boundary is double-spaced, no hand-rolled row spans (${#FILES[@]} tab files),"
echo "      and ${#HUD_FILES[@]} sectioned HUD(s) pad their panel at both ends"
exit 0
