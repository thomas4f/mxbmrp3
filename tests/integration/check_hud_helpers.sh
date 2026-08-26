#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_hud_helpers.sh
# HUD helper-bypass lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT: a HUD draws its panel background, its title and its grid-snapped
# origin through the BaseHud helpers -- addBackgroundQuad(), addTitleString() and
# snapEdgeX/Y() -- never by hand-rolling the same geometry locally.
#
# WHY THIS EXISTS. Hand-rolling one of these is not wrong on the day it is
# written; it is a copy that stops tracking the original. Every helper here has
# since gained the panel-theme nine-slice, and every hand-rolled copy silently
# did not:
#
#   GapBarHud       reimplemented addBackgroundQuad exactly -- offset, texture
#                   branch, opacity and all -- and rendered flat while every
#                   other panel was framed.
#   SessionHud      drew its title with addString, so it was the one full HUD
#                   with no themed title band.
#   SettingsHud     built its own copy of the title band. The clamp that stops a
#                   band sliding below its own title was fixed in BaseHud and
#                   the copy kept the broken version, so the heading floated
#                   above an empty strip.
#   Notices/Timing/ three private copies of "snap the centring anchor if grid
#   GapBar          snapping is on", plus a fourth variant in the drag path that
#                   snapped the OFFSET rather than the resulting EDGE -- which
#                   leaves an off-grid panel off-grid at every offset.
#
# None of those failed a test or a build. They were all found by a person
# looking at a screenshot, which is the expensive way.
#
# WHAT IS FLAGGED, and why these three signals specifically:
#
#   1. Reading m_bShowBackgroundTexture / m_iBackgroundTextureIndex outside the
#      helper. Those members exist to pick between the texture and the flat
#      fill, which is addBackgroundQuad's job -- reading them anywhere else
#      means that decision is being made a second time.
#
#   2. Passing the TITLE font to addString(). A title goes through
#      addTitleString(), which is what emits the themed band and places the
#      identity icon.
#
#   3. Snapping a panel's position from a HUD -- reaching for the lattice
#      (layoutDefaults().snap*) or re-reading the user's on/off gate
#      (UiConfig::getGridSnapping). Both go through snapEdgeX/Y(), which owns the
#      gate AND the choice of WHICH quantity to snap.
#
# All three have legitimate exceptions, so none is banned outright -- each needs
# an annotation on the line or the line above saying why:
#
#     // bg-quad-exempt: not a panel background -- picks the sprite cursor
#     if (m_bShowBackgroundTexture) {
#
#     addString("Lap", ...TITLE...);    // title-exempt: caption, not a panel title
#
#     // grid-snap-exempt: steps a marker along the lattice, not a panel origin
#     x = layoutDefaults().snapX(x);
#
# On the line itself, or anywhere in the comment block directly above it.
#
# The annotation is the point. It costs a sentence and it makes the next
# reviewer decide deliberately rather than by omission.
#
# Exit codes: 0 = clean, 1 = violations found, 3 = SKIPPED (nothing to scan).
# ============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HUD_DIR="${ROOT}/mxbmrp3/hud"

if [ ! -d "${HUD_DIR}" ]; then
    echo "SKIP: ${HUD_DIR} not found"
    exit 3
fi

# base_hud*.cpp IS the helper -- it necessarily touches all of this.
mapfile -t FILES < <(find "${HUD_DIR}" -name '*.cpp' ! -name 'base_hud*.cpp' | sort)
if [ "${#FILES[@]}" -eq 0 ]; then
    echo "SKIP: no HUD sources to scan"
    exit 3
fi

violations=0

# A rule that MATCHES exits 1. Any other non-zero is awk itself giving up -- a
# fatal regex, a missing interpreter -- and reporting that as "a HUD violated a
# rule" is how the escape bug below read in CI: "1 rule(s) violated", no file, no
# line, nothing to go and look at. Fail loudly as a broken check instead.
lint_broke() {
    echo "ERROR: the lint itself failed on $1 (awk exit $2)."
    echo "       This is a broken check, NOT a HUD violation -- fix the rule."
    echo "       awk: $(command -v awk)"
    exit 2
}

# Self-check the pattern plumbing before trusting anything below: a pattern with
# an escaped metacharacter has to survive the trip into awk intact. If it does
# not, every rule is quietly scanning something other than what it reads as, and
# a clean run means nothing. Ten milliseconds, and it is the check that would
# have caught the -v bug on the machine that wrote it rather than in CI.
probe="$(printf 'x(y\nxzy\n' | MXB_PROBE='x\(y' awk '
    BEGIN { pat = ENVIRON["MXB_PROBE"] }
    $0 ~ pat { n++ }
    END { print n + 0 }
' 2>/dev/null)" || probe="fatal"
if [ "${probe}" != "1" ]; then
    echo "ERROR: this awk mangles patterns on the way in (probe matched '${probe}',"
    echo "       expected exactly 1 of 2 lines). Every rule below would scan the"
    echo "       wrong thing, so a PASS here would be meaningless."
    echo "       awk: $(command -v awk)"
    exit 2
fi

scan() {
    local pattern="$1" annotation="$2" message="$3" hint="$4"
    local found=0
    for f in "${FILES[@]}"; do
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        # The pattern travels in the ENVIRONMENT, never through -v. An awk -v
        # assignment runs ESCAPE PROCESSING over the value, so a grep-style
        # pattern reaches the regex engine already chewed: \. becomes . (any
        # char) and \( becomes ( (a group open). mawk leaves unknown escapes
        # alone, so this read clean for whoever wrote it; gawk strips them, and
        # rule 3's pattern became an unmatched ( -- fatal, on every file. ENVIRON
        # is handed over verbatim by both, so a pattern means what it says.
        MXB_FILE="${rel}" MXB_PAT="${pattern}" MXB_ANN="${annotation}" \
        MXB_MSG="${message}" MXB_HINT="${hint}" \
        awk '
            BEGIN {
                file = ENVIRON["MXB_FILE"]; pat  = ENVIRON["MXB_PAT"]
                ann  = ENVIRON["MXB_ANN"];  msg  = ENVIRON["MXB_MSG"]
                hint = ENVIRON["MXB_HINT"]
            }
            { line[NR] = $0 }
            $0 ~ pat {
                if ($0 ~ ann) next
                # Or anywhere in the comment block directly above -- a reason worth
                # writing is usually a sentence, and a sentence wraps.
                seen = 0
                for (i = NR - 1; i > 0; i--) {
                    probe = line[i]
                    sub(/^[ \t]+/, "", probe)
                    if (probe !~ /^\/\//) break
                    if (probe ~ ann) { seen = 1; break }
                }
                if (seen) next
                printf "%s:%d: %s\n", file, NR, msg
                gsub(/^[ \t]+/, "", $0)
                printf "    %s\n", $0
                printf "  %s\n", hint
                bad++
            }
            END { exit (bad ? 1 : 0) }
        ' "$f"
        awk_rc=$?
        case "${awk_rc}" in
            0) ;;
            1) found=1 ;;
            *) lint_broke "${rel}" "${awk_rc}" ;;
        esac
    done
    return $found
}

scan 'm_bShowBackgroundTexture|m_iBackgroundTextureIndex' \
     'bg-quad-exempt:' \
     'reads the background-texture members outside addBackgroundQuad()' \
     'Call addBackgroundQuad() -- it owns the texture-vs-fill choice AND the themed nine-slice. If this genuinely is not a panel background, annotate: // bg-quad-exempt: <reason>' \
     || violations=$((violations + 1))

# Rule 2 needs its own pass: the signal is not a font, it is a BLOCK.
#
# The first draft flagged any addString() carrying the TITLE font, and that was
# wrong -- the title font is legitimately used about forty times for things that
# are not titles (FMX scores, notice banners, gauge captions, the speed readout).
# A lint needing forty suppressions is a lint somebody deletes.
#
# What actually identifies a panel title is m_bShowTitle: that flag exists to gate
# the title and nothing else. SessionHud's bug was exactly `if (m_bShowTitle) {
# addString(...) }`, and this catches that shape with almost no false positives.
# HEURISTIC, and here is its hole: a block is only entered when the '{' is on the
# same line as `if (... m_bShowTitle`. Brace-on-next-line, or a braceless if, leaves
# depth <= 0 and the scan exits at once -- so a title drawn that way is not checked.
# Both are out of style here and the rule has caught what it was written for, but a
# clean run is not proof that every title went through addTitleString().
title_block_scan() {
    local found=0
    for f in "${FILES[@]}"; do
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        awk -v file="${rel}" '
            { line[NR] = $0 }
            # Enter a block gated on m_bShowTitle; track braces to find its end.
            /if[ \t]*\([^)]*m_bShowTitle/ { inblock = 1; depth = 0 }
            inblock {
                n = gsub(/\{/, "{"); depth += n
                n = gsub(/\}/, "}"); depth -= n
                if (/addString[ \t]*\(/ && !/addTitleString/) {
                    seen = ($0 ~ /title-exempt:/)
                    for (i = NR - 1; i > 0 && !seen; i--) {
                        probe = line[i]
                        sub(/^[ \t]+/, "", probe)
                        if (probe !~ /^\/\//) break
                        if (probe ~ /title-exempt:/) seen = 1
                    }
                    if (!seen) {
                        printf "%s:%d: title drawn with addString() inside an m_bShowTitle block\n", file, NR
                        probe = $0; gsub(/^[ \t]+/, "", probe)
                        printf "    %s\n", probe
                        printf "  Use addTitleString() -- it emits the themed title band and places the identity icon. If this is a caption rather than a panel title, annotate: // title-exempt: <reason>\n"
                        bad++
                    }
                }
                if (depth <= 0 && NR > 1) inblock = 0
            }
            END { exit (bad ? 1 : 0) }
        ' "$f"
        awk_rc=$?
        case "${awk_rc}" in
            0) ;;
            1) found=1 ;;
            *) lint_broke "${rel}" "${awk_rc}" ;;
        esac
    done
    return $found
}
title_block_scan || violations=$((violations + 1))

# Rule 3: grid-snapping a panel's position by hand.
#
# Same failure shape as the other two, found the same expensive way -- on a
# screenshot with the grid overlay on. Three HUDs had each written their own
# "quantise the centring anchor if snapping is on" gate, and the drag path had a
# fourth variant that snapped the OFFSET instead of the resulting EDGE, which
# does not put an off-grid panel on the grid at all. Notices and Timing agreed
# with each other and with nothing else.
#
# BaseHud::snapEdgeX/Y is the one helper: it reads the user's setting and snaps
# the edge. A HUD reaching for SNAP_TO_GRID_* directly is either re-deriving that
# gate or snapping the wrong quantity.
#
# Note the exemption is real and expected: the snap helpers are also the right
# tool for things that are not a panel origin (a marker stepping along a lattice,
# the grid overlay itself). Those annotate and move on.
#
# RETARGETED. This matched `HudGrid::SNAP_TO_GRID_[XY]` until that namespace was
# deleted -- the lattice became data (layoutDefaults()), so the pattern named a
# symbol that no longer existed anywhere in the tree and the rule could not fire.
# It was live enforcement in the docs and a no-op in the file, which is worse than
# no rule: the invariant still held, so nothing ever looked. Both current spellings
# of a bypass are matched now -- and the object is not pinned to layoutDefaults(),
# because `layout().snapX(x)` is the same bypass through a theme's metrics and the
# first spelling of the pattern missed it.
scan '\.snap[XY]\(|\.snapDelta[XY]\(|getGridSnapping\(\)' \
     'grid-snap-exempt:' \
     'snaps to the grid directly instead of through BaseHud::snapEdgeX/Y' \
     'Use snapEdgeX()/snapEdgeY() -- they carry the user'"'"'s grid-snapping gate and snap the EDGE, not the offset (snapping an offset leaves an off-grid panel off-grid). If this is not a panel origin, annotate: // grid-snap-exempt: <reason>' \
     || violations=$((violations + 1))

# Rule 4 was here: it banned a WIDGET from opting into the themed body card.
#
# LIFTED, because the thing it guarded no longer exists. The ban was written when a
# widget's card was a SHRINK-WRAP mode that hugged the quads a gauge drew, so a widget
# carried two inner cards of DIFFERENT widths -- a panel-flush header over a
# content-hugging body -- where a HUD carries two flush ones. That mode is gone;
# emitContentCard() draws at the panel's frame margin, the same X span as the title
# band, so a widget now gets exactly the HUD treatment. Verified by render under a theme
# before lifting: band on top, value in its own well, flush with each other.
#
# What replaced the ban is a CHOICE rather than a prohibition: [card] widget-content
# turns the family off for a theme whose inner slices are too loud around a gauge,
# which is the real content of the old rule's second objection ("too much furniture").
# Bracket is the theme that motivated it and the one most likely to want it off.
#
# Not re-adding a lint for the new key: there is nothing to get silently wrong. A
# theme either sets it or does not, and both readings are visible on screen.

# Rule 5: addTitleString() called INSIDE an `if (m_bShowTitle)` block.
#
# It must be unconditional. addTitleString already handles the hidden case -- it
# emits an empty string (keeping string index 0 stable for the layout fast paths
# that reposition by index) and, crucially, emits the BODY CARD either way. Gating
# the call means switching a HUD's title off ALSO removes its card.
#
# Three rounds of the same bug: SessionHud first, then Event Log, Pitboard, Session
# Charts and Rumble found by a user toggling titles. It is invisible unless you
# happen to turn a title off under a theme, which is why it needs a grep rather than
# a review note.
#
# WIDGETS TOO, since rule 4 was lifted. This used to skip *_widget.cpp on rule 4's
# premise -- "a widget has no card to lose" -- and when rule 4 went, the premise went
# with it while the skip stayed. Eleven widgets opt into a body card today, seven of
# them ship with the title OFF, and all seven drew a frame around bare content: the
# fourth round of this exact bug, and the one round this lint could not see because it
# was told not to look. Reported from the game as compass not drawing the white content
# panel that position and lap do.
#
# TWO SHAPES PASS, because the rule is about the CARD, not about the call:
#   1. addTitleString() unconditional -- the original, and the right shape for a HUD.
#   2. gated, with `else { emitContentCard(0.0f); }` -- what the eleven widgets carry.
#
# Shape 2 exists because a widget's rebuildLayout() walks a RUNNING string index that
# only advances when the title shows (see LapWidget). Making the call unconditional
# there emits the empty title string either way, so that index lands one short and the
# widget's value is positioned at the caption's slot -- a drag-path bug across eleven
# files, traded for a bug that is already fixed. The card is what matters; both shapes
# keep it.
#
# Reuses rule 2's block scanner, so it inherits the same brace heuristic and hole.
title_gate_scan() {
    local bad=0
    for f in "${FILES[@]}"; do
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        awk -v file="${rel}" '
            /if[ \t]*\([^)]*m_bShowTitle/ { inblock = 1; depth = 0 }
            inblock {
                n = gsub(/\{/, "{"); depth += n
                n = gsub(/\}/, "}"); depth -= n
                if (/addTitleString[ \t]*\(/) { saw_title = 1; title_line = NR }
                if (/emitContentCard[ \t]*\(/) { saw_card = 1 }
                if (depth <= 0 && saw_title && !saw_card) {
                    printf "%s:%d: addTitleString() gated on m_bShowTitle, and no else-branch card\n", file, title_line
                    printf "    It handles the hidden case itself -- and emits the BODY CARD\n"
                    printf "    either way, so gating it removes the card with the title.\n"
                    printf "    Either call it unconditionally, or add:\n"
                    printf "        } else { emitContentCard(0.0f); }\n"
                    bad++
                }
                if (depth <= 0) { saw_title = 0; saw_card = 0 }
                if (depth <= 0) inblock = 0
            }
            END { exit (bad ? 1 : 0) }
        ' "$f" || bad=$((bad + 1))
    done
    [ "${bad}" -eq 0 ]
}
title_gate_scan || violations=$((violations + 1))

# Rule 6 IS GONE, and this note is what stops it coming back.
#
# It compared two lists: settings_tab_widgets.cpp carried an enableTitle flag per row,
# and the widget itself either called addTitleString or did not. They had drifted in
# both directions -- CompassWidget builds a full title while its row said false, so the
# compass was the only captionable panel a user could not caption; the reverse gives a
# switch that does nothing.
#
# THERE IS NOW ONE LIST. BaseHud::m_titleSupported is the single statement, both the
# per-HUD tabs and the Widgets table read it, and setShowTitle() refuses when it is
# false -- so the disagreement this rule existed to find cannot be written. That is the
# escalation this project prefers (impossible by construction beats a scanner), and the
# rule went with the bug.
#
# A REPLACEMENT WAS WRITTEN AND THROWN AWAY, which is the part worth recording. It
# asked "does this class call addTitleString, and if not does it set the flag" -- and a
# class is not a file here. StandingsHud is four .cpp; SettingsHud is twenty-odd, since
# every settings_tab_*.cpp defines one of its methods. Reconstructing class membership
# from file names produced a false positive on the Standings render file and then a
# false NEGATIVE on SettingsHud, whose group happened to contain the word
# m_titleSupported in a comment. Two bugs in the lint before it caught anything, for a
# residual whose whole cost is one dead toggle on one tab, visible the first time
# anyone opens it.

# RULE 7: the title ROW is reserved with titleRowHeight(), never a bare row height.
#
# Under a theme the title BAND is the caption's font size plus twice [title] padding-y
# -- 34px against a 25px normal row at the shipped defaults. A panel that reserves the
# ROW gets two faults at once: its content starts inside the bottom of its own band,
# and the panel comes out ~9px shorter than one that reserved the band. That is
# exactly how Position/Lap/Clock/Time/Gear/Speed ended up a different height from
# Bars/Compass/Fuel/G-force/Lean, which sit beside them and are meant to tile.
#
# lineHeightLarge USED TO BE EXEMPT here, on the grounds that "at 38px it already
# exceeds the band, so those panels over-reserve rather than clip". That was measured
# and it is only half true. The Large tier's reserved row is:
#
#     max(lineHeightLarge, ceilY(max(band, bandBottom + cardPadY - contentTop)))
#
# and the second term wins when NO BODY CARD is drawn, because contentTop then loses
# the card's border. At the shipped metrics (12.672px cell, frame 2, card 1):
#
#     body card ON   -> 4 cells (50.69px) == lineHeightLarge      exempt was right
#     body card OFF  -> 5 cells (63.36px) vs lineHeightLarge      12.67px SHORT
#
# and "off" is a theme's DEFAULT: ThemeAsset::titleBand defaults true while
# contentCard defaults false, so any theme shipping card slices without an explicit
# `[card] hud-content = 1` gets a band with no card underneath it -- the clipping
# case. All three shipped themes write that key, which is why the exemption survived:
# thirteen HUDs were one theme key away from starting their first row inside their own
# title band, and nothing on the shipped configuration could show it.
#
# So both rows are banned now and the tier is a parameter --
# reservedTitleHeight(dim, TitleTier::Large) -- which also makes the OTHER half of the
# mistake unspellable: a site can no longer pair one tier's font with the other's row.
#
# Deliberate exceptions carry `// title-row-exempt: <reason>` on the line or in the
# comment block above it. PitboardHud is the only one and its reason is real: it pairs
# a normal-size caption with a large row on purpose, because the board ARTWORK has a
# printed header area that the row is sized to rather than to the caption.
# Through the shared scan(), so the exemption is found anywhere in the comment block
# above the line rather than only on it -- this rule's one exception needs a paragraph,
# not a trailing clause.
# THREE SPELLINGS, not one, and the third is why this pattern is as loose as it is.
# The first pass banned only the ternary `m_bShowTitle ? dim.lineHeightLarge : 0.0f`
# and converted twenty of those. The panel HEIGHT then moved while the content CURSOR
# did not, because five sites advance past the title in a different shape entirely:
#
#     if (m_bShowTitle) currentY += dim.lineHeightLarge;      // EventLog, Standings x2, FMX
#     out.titleHeight = m_bShowTitle ? out.dim.lineHeightLarge : 0.0f;   // Stats (out.dim)
#
# StandingsHud carried one of each, so a partial conversion left its panel reserving
# five cells while its rows advanced four -- measured as a 13px change in the panel's
# bottom edge with the content not moving at all, which is how it was caught (a
# companion-window screenshot diff, not a test).
#
# So: any m_bShowTitle on the same line as a lineHeight tier, whatever the operator
# and whatever the struct is called. The helper already returns 0 for a hidden title,
# so the `if (m_bShowTitle)` gate around it is redundant and should go with it.
title_row_scan() {
    scan 'm_bShowTitle.*lineHeight(Normal|Large)' \
         'title-row-exempt:' \
         'reserves a bare ROW for the title, which a themed title BAND can overflow' \
         'Use reservedTitleHeight(dim, TitleTier::Normal) or (dim, TitleTier::Large) -- it is max(row, band), so it is identical when no band is drawn and correct when one is, and it returns 0 when the title is hidden (so drop the if). Deliberate? Annotate: // title-row-exempt: <reason>'
}
title_row_scan || violations=$((violations + 1))

# RULE 7b: the same quantity, spent across TWO LINES.
#
# Rule 7 matches m_bShowTitle and the row on ONE line, which is the ternary shape. It
# cannot see the block shape, and that is not a theoretical gap -- EventLog's layout
# fast path had it:
#
#     if (m_bShowTitle && stringIndex < m_strings.size()) {
#         positionString(stringIndex++, contentStartX, currentY);
#         currentY += dim.lineHeightLarge;        <-- rule 7 never looked here
#     }
#
# while its full rebuild advanced by reservedTitleHeight(). Under a band-without-card
# theme the two disagreed by a cell, which is the Standings desync one HUD over -- found
# by a reviewer, not by this lint, because the lint was written to the shape of the sites
# that existed rather than to the quantity.
#
# Reuses rule 2's block scanner (enter on `if (... m_bShowTitle`, track braces), so it
# inherits the same brace-on-same-line heuristic and hole.
title_advance_scan() {
    local found=0
    for f in "${FILES[@]}"; do
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        awk -v file="${rel}" '
            { line[NR] = $0 }
            /if[ \t]*\([^)]*m_bShowTitle/ { inblock = 1; depth = 0; start = NR }
            inblock {
                n = gsub(/\{/, "{"); depth += n
                n = gsub(/\}/, "}"); depth -= n
                if ($0 ~ /(\+=|=)[^;]*\.lineHeight(Normal|Large|ExtraLarge)/ &&
                    $0 !~ /reservedTitleHeight|titleRowHeight/) {
                    seen = ($0 ~ /title-row-exempt:/)
                    for (i = NR - 1; i > 0 && !seen; i--) {
                        probe = line[i]; sub(/^[ \t]+/, "", probe)
                        if (probe !~ /^\/\//) break
                        if (probe ~ /title-row-exempt:/) seen = 1
                    }
                    if (!seen) {
                        printf "%s:%d: advances past the title by a bare ROW inside an m_bShowTitle block\n", file, NR
                        probe = $0; gsub(/^[ \t]+/, "", probe)
                        printf "    %s\n", probe
                        printf "  Use reservedTitleHeight(dim, TitleTier::...) -- it returns 0 when the title is hidden, so the advance can leave the block, and it agrees with the full rebuild under a themed band.\n"
                        bad++
                    }
                }
                if (depth <= 0 && NR > start) inblock = 0
            }
            END { exit (bad ? 1 : 0) }
        ' "$f"
        awk_rc=$?
        case "${awk_rc}" in
            0) ;;
            1) found=1 ;;
            *) lint_broke "${rel}" "${awk_rc}" ;;
        esac
    done
    return $found
}
title_advance_scan || violations=$((violations + 1))

# RULE 8: a string's Y is set through addString() or positionString(), never by hand.
#
# Those two are the only places BaseHud::rowCenterOffset() is applied -- the offset that
# centres a glyph box in its row instead of leaving it flush with the row's top. A HUD
# that writes m_afPos[1] itself skips it, and the failure is invisible until the panel
# MOVES: the full rebuild goes through addString and centres, the drag/scale fast path
# writes raw and does not, so the text jumps by the offset and back.
#
# Exactly what shipped on the first attempt. StandingsHud and IdealLapHud repositioned
# four and two strings by hand, and standings_layout_test caught the race number sliding
# inside its plate on drag -- a test that exists because this same class of bug (two paths
# to place one thing, only one of them updated) had already happened once.
#
# Deliberate exceptions carry `// string-y-exempt: <reason>` -- VersionWidget's minigame
# places text absolutely in its own field, where there is no row to centre in.
string_y_scan() {
    local bad=0
    for f in "${FILES[@]}"; do
        case "$f" in *base_hud_render.cpp) continue ;; esac
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        # Any write to a string's Y, whether repositioning m_strings[i] or filling a
        # local SPluginString_t before pushing it -- both skip the centring. An
        # exemption is the line ABOVE carrying the annotation.
        awk -v file="${rel}" '
            # The annotation may wrap, so it counts for the next few lines rather than
            # only the one directly below it -- a reason worth writing is often two lines.
            /string-y-exempt:/ { exempt = NR; next }
            /^[ \t]*\/\// { if (exempt && NR - exempt <= 3) exempt = NR; next }
            /m_afPos\[1\][ \t]*=/ {
                if (exempt && NR - exempt <= 3) { next }
                printf "%s:%d:%s\n", file, NR, $0
                bad++
            }
            END { exit (bad ? 1 : 0) }
        ' "$f" || bad=$((bad + 1))
    done
    [ "${bad}" -eq 0 ] && return 0
    echo "  ^ sets a string's Y by hand, bypassing rowCenterOffset()."
    echo "    Use positionString(index, x, y) -- it applies the offset and applyOffset()."
    echo "    Deliberate? Annotate the line with '// string-y-exempt: <reason>'."
    return 1
}
string_y_scan || violations=$((violations + 1))

# Rule 9: a HUD calling validateAllHudPositions() directly.
#
# THE BUG, and it shipped: BaseHud::validatePosition() calls update() on a HUD that is
# dirty. So validating from anywhere reachable out of a HUD's update() -- an input
# handler above all -- re-enters the very update that dispatched it. The click edge
# stays true for the whole frame, so the same button dispatches again, and again. The
# Appearance tab's theme-cycle button did this and recursed ~1400 frames into a stack
# overflow (0xC00000FD), twice, in the field.
#
# HUD code REQUESTS: HudManager::requestPositionValidation() sets a flag that
# HudManager flushes at the end of updateHuds(), once every update() has returned.
# InputManager may still call it directly -- it runs outside the HUD update pass.
hud_validate_scan() {
    local bad=0
    for f in "${FILES[@]}"; do
        # Skip comment lines: this rule is quoted BY NAME in the comment that
        # explains it, three files over, and a lint that trips on its own
        # documentation teaches people to delete the documentation.
        grep -v '^[[:space:]]*//' "$f" \
            | grep -q 'validateAllHudPositions[[:space:]]*(' || continue
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        echo "${rel}: a HUD calls validateAllHudPositions() directly"
        echo "    validatePosition() calls update(), so this re-enters the update that"
        echo "    dispatched it -- a click handler then re-dispatches itself forever."
        echo "    Use HudManager::requestPositionValidation() instead; it is flushed"
        echo "    at the end of updateHuds(), outside every handler."
        bad=$((bad + 1))
    done
    [ "${bad}" -eq 0 ]
}
hud_validate_scan || violations=$((violations + 1))

# Rule 10: a declared body card has to be EMITTED.
#
# m_bContentCard is read by two different things: emitContentCard() draws the card,
# and contentPaddingX() reserves its clearance. Only the first needs a call site --
# the padding follows from the flag alone. So a widget that sets the flag and never
# reaches emitContentCard (directly, or through addTitleString, which calls it on both
# branches) pays a cell of clearance per side for a card nobody draws, and reads as a
# themed frame around bare content with unexplained padding inside it.
#
# THE FIVE THAT SHIPPED THAT WAY: Ecu, Gamepad, Speedo, Tacho and TyreTemp. All five
# have no caption at all -- not a title toggle switched off, no title -- so nothing
# ever called addTitleString, and rule 5 could not see them: it fires on a GATED call,
# and there was no call to gate. Same bug as rule 5's, one step further along.
#
# Deliberate? `// content-card-exempt: <reason>` on the flag or the comment above it.
# PER CLASS, not per file: a big HUD is split across several .cpp (StandingsHud sets
# the flag in its constructor in one file and titles itself in another), so the call
# is looked for anywhere that file's class is defined.
#
# RETARGETED for the box-model port, per rule 3's lesson (a rule whose accepted
# calls go stale flags the whole tree, and the reflex fix is deleting the rule): a
# migrated HUD emits its card through addPlanBackground(), which draws the frame,
# the band and one card PER SECTION at the plan's coordinates -- emitContentCard
# and addTitleString survive as the deliberate own-geometry holdouts' vocabulary.
# All three shapes emit the declared card; a class reaching none of them is the
# same bug as ever (clearance reserved, card never drawn -- Ecu/Gamepad/Speedo/
# Tacho/TyreTemp shipped that way).
content_card_scan() {
    local bad=0
    for f in "${FILES[@]}"; do
        grep -q 'm_bContentCard *= *true' "$f" || continue
        grep -q 'content-card-exempt:' "$f" && continue
        local cls
        cls="$(sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\)::\1(.*/\1/p' "$f" | head -1)"
        local family=("$f")
        if [ -n "${cls}" ]; then
            # Unanchored: only the CONSTRUCTOR starts a line with the class name; every
            # other definition is preceded by its return type.
            mapfile -t family < <(grep -rl "${cls}::" "${HUD_DIR}" --include='*.cpp' 2>/dev/null)
        fi
        grep -qE 'addTitleString[ \t]*\(|emitContentCard[ \t]*\(|addPlanBackground[ \t]*\(' "${family[@]}" && continue
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        echo "${rel}: sets m_bContentCard = true but never emits the card"
        echo "    Nothing here reaches emitContentCard() -- directly or through"
        echo "    addTitleString(), which emits it on both branches -- so the card is"
        echo "    never drawn while its clearance is still reserved. Add"
        echo "    'emitContentCard(0.0f);' after addBackgroundQuad(), or drop the flag."
        echo "    Deliberate? Annotate: // content-card-exempt: <reason>"
        bad=$((bad + 1))
    done
    [ "${bad}" -eq 0 ]
}
content_card_scan || violations=$((violations + 1))

# RULE 11: a panel's height and its content origin spend dim.paddingV, through
# panelHeight() / panelContentY() -- never a locally spelled equivalent.
#
# THE SYNONYM TRAP. dim.paddingV is contentPaddingY() -- the base padding widened by the theme's borders -- and
# at the shipped grid its first term is exactly two cells -- which is also exactly
# dim.lineHeightNormal. So `dim.lineHeightNormal * 1.0f` is a perfect synonym for the
# padding UNTHEMED, and drops that widening the moment a theme is on. That term
# is what pushes content clear of the frame's edge slices, so the panel's rows end up
# inside its own frame:
#
#     a themed panel (frame 2, card 1)    12.67px short per side
#     debug           (frame 4, card 3)   50.69px short per side
#
# PitboardHud spent it that way in BOTH its height and its content origin, so it was
# internally consistent and uniformly wrong -- which is why no amount of looking at
# that panel alone would show it. It only appears next to a panel built the other way.
#
# Rule 7's lesson applied one level up: the bug is not the arithmetic, it is that the
# quantity has more than one spelling. This bans the others.
#
# Panels NOT composed as padding + content + padding are exempt and say why: sized
# from ART (GamepadWidget), from a CONTROL (SettingsButtonWidget), or carrying their
# own vertical structure (SettingsHud's band + section cards).
panel_pad_scan() {
    scan '(backgroundHeight|panelHeight|contentStartY|currentY|boxHeight)[^;]*=[^;]*\.(lineHeight(Normal|Large)|cellH)[^;]*\*[^;]*1\.0f' \
         'panel-pad-exempt:' \
         'spells the panel padding locally instead of dim.paddingV' \
         'Use panelHeight(dim, content) and panelContentY(dim, top) -- dim.paddingV is contentPaddingY(), whose theme widening a locally spelled row height silently drops (12.67px per side on the shipped themes). Deliberate? Annotate: // panel-pad-exempt: <reason>'
}
panel_pad_scan || violations=$((violations + 1))

# RULE 12: a title caption that is NOT left-justified says why.
#
# A grep for Justify:: at the addTitleString sites reads as "28 HUDs left-align, 3
# are outliers", and acting on that reading is the trap: the rule the tree actually
# follows is that A CAPTION MATCHES ITS OWN CONTENT. Position and Lap left-align
# because their values are left-set; Gear and Speed centre because the digit
# underneath is centred. VersionWidget was the real outlier -- a centred caption over
# a left-set version string -- and looked identical to the other two from a grep.
#
# So the annotation is the point: it forces the author to name the content the
# caption is following, which is exactly the check a grep cannot do. It also catches
# the second cost of a non-left caption, which is invisible at the call site:
# addTitleString emits the HUD identity ICON only for LEFT (see its body), so a
# centred title silently drops it.
# Joins the CALL, not the line: every one of these sites wraps its arguments, and
# Version's -- the buggy one -- carried Justify::CENTER on the second line. A
# one-line pattern would have passed it, which is rule 7b's lesson (write the lint
# to the quantity, not to the shape of today's call sites).
title_align_scan() {
    local found=0
    for f in "${FILES[@]}"; do
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        awk -v file="${rel}" '
            { line[NR] = $0 }
            /addTitleString[ \t]*\(/ { incall = 1; start = NR; call = "" }
            incall {
                call = call " " $0
                if ($0 ~ /\)[ \t]*;/) {
                    incall = 0
                    if (call ~ /Justify::(CENTER|RIGHT)/) {
                        seen = (call ~ /title-align:/)
                        for (i = start - 1; i > 0 && !seen; i--) {
                            probe = line[i]; sub(/^[ \t]+/, "", probe)
                            if (probe !~ /^\/\//) break
                            if (probe ~ /title-align:/) seen = 1
                        }
                        if (!seen) {
                            printf "%s:%d: centres or right-aligns a title without saying what content it is following\n", file, start
                            probe = line[start]; gsub(/^[ \t]+/, "", probe)
                            printf "    %s\n", probe
                            printf "  A caption matches its own content -- name that content. A non-LEFT title also drops the HUD identity icon (addTitleString emits one only for LEFT). Deliberate? Annotate: // title-align: <reason>\n"
                            bad++
                        }
                    }
                }
            }
            END { exit (bad ? 1 : 0) }
        ' "$f"
        awk_rc=$?
        case "${awk_rc}" in
            0) ;;
            1) found=1 ;;
            *) lint_broke "${rel}" "${awk_rc}" ;;
        esac
    done
    return $found
}
title_align_scan || violations=$((violations + 1))

# RULE 13: inside the SETTINGS PANEL, a filled rectangle goes through a BaseHud
# helper -- addButtonQuad(), addButtonBackground() or addBackgroundQuad().
#
# Scoped to the settings panel deliberately. Across hud/ there are ~60 hand-built
# SOLID_COLOR quads and nearly all are right: graph bars, gauge arcs, map ribbon,
# the helmet. A lint needing fifty suppressions is a lint somebody deletes (rule 2's
# lesson). Inside the settings panel the population is the opposite -- everything
# filled is a button, a card or a swatch -- so the rule is cheap and the exceptions
# are countable.
#
# It has now bitten twice, both times the same way: a hand-rolled copy drew a FLAT
# rectangle while every button beside it picked up the theme's 9-slice. First the
# Reset button (recorded in addButtonBackground's header, which is where the second
# one should have been caught), then the footer's "vX.Y.Z available!" chip -- the
# fifth button on that row and the only one that did not look like one. Neither is
# visible without a theme installed, which is why both shipped.
settings_solid_quad_scan() {
    local found=0
    for f in "${FILES[@]}"; do
        case "$f" in
            */settings_hud*.cpp | */settings/*.cpp) ;;
            *) continue ;;
        esac
        local rel
        rel="$(realpath --relative-to="${ROOT}" "$f")"
        awk -v file="${rel}" '
            { line[NR] = $0 }
            /m_iSprite[ \t]*=[ \t]*(PluginConstants::)?SpriteIndex::SOLID_COLOR/ {
                seen = ($0 ~ /solid-quad-exempt:/)
                for (i = NR - 1; i > 0 && !seen; i--) {
                    probe = line[i]; sub(/^[ \t]+/, "", probe)
                    if (probe !~ /^\/\// && probe !~ /^$/) break
                    if (probe ~ /solid-quad-exempt:/) seen = 1
                }
                if (!seen) {
                    printf "%s:%d: hand-builds a filled quad inside the settings panel\n", file, NR
                    probe = $0; gsub(/^[ \t]+/, "", probe)
                    printf "    %s\n", probe
                    printf "  Use addButtonQuad() / addButtonBackground() / addBackgroundQuad() -- they carry the theme 9-slice and the opaque-fill rule a raw quad drops. Genuinely a flat rectangle? Annotate: // solid-quad-exempt: <reason>\n"
                    bad++
                }
            }
            END { exit (bad ? 1 : 0) }
        ' "$f"
        awk_rc=$?
        case "${awk_rc}" in
            0) ;;
            1) found=1 ;;
            *) lint_broke "${rel}" "${awk_rc}" ;;
        esac
    done
    return $found
}
settings_solid_quad_scan || violations=$((violations + 1))

if [ "${violations}" -ne 0 ]; then
    echo ""
    echo "FAIL: ${violations} rule(s) violated -- a HUD is re-implementing a BaseHud helper."
    echo "      See the header of this script for the three bugs that reached users this way."
    exit 1
fi

echo "PASS: no HUD bypasses addBackgroundQuad(), addTitleString(), snapEdgeX/Y() or a string's own Y, and none validates positions from inside a handler (${#FILES[@]} files scanned)"
exit 0

