// ============================================================================
// hud/base_hud_render.cpp
// BaseHud string emission and the title-band predicates. The rest of the
// render helpers regrew past 2.4k lines here and were split out again, same
// byte-identical-extraction pattern as before: base_hud_plan.cpp (box-model
// plan), base_hud_sections.cpp (content card/sections, buttons),
// base_hud_metrics.cpp (layout metrics/paddings), base_hud_theme.cpp (title
// icon, theme resolution, nine-slice emitters, themed fill), base_hud_primitives.cpp
// (arcs/dots/icons/lines/needles/charts, styled strings). The BaseHud class,
// members, and public API are unchanged — only where method bodies live moves.
// ============================================================================
#include "base_hud.h"
#include "../diagnostics/call_counters.h"
#include <cstring>


// ============================================================================
// Shared HUD Rendering Helpers (eliminates duplication across HUDs)
// ============================================================================

// Emits one render string. IMPORTANT: this always pushes an entry, even when `text` is
// empty — it never skips. Index-coordinated layout fast paths (e.g. StandingsHud /
// IdealLapHud / StatsHud rebuildLayout) reposition strings by index and assume a stable
// per-row string count/order across rebuilds. Callers therefore pass "" for a blank cell
// rather than skipping the call; don't "optimize" empties away here or the indices desync
// and rows scramble on drag/scale.
void BaseHud::addString(const char* text, float x, float y, int justify, int fontIndex,
                        unsigned long color, float fontSize, bool skipShadow) {
    MXB_COUNT_CALL(ADD_STRING);
    SPluginString_t stringEntry;

    strncpy_s(stringEntry.m_szString, sizeof(stringEntry.m_szString), text, sizeof(stringEntry.m_szString) - 1);
    stringEntry.m_szString[sizeof(stringEntry.m_szString) - 1] = '\0';

    applyOffset(x, y);
    stringEntry.m_afPos[0] = x;
    // Centred in its row, not flush with its top -- see rowCenterOffset().
    stringEntry.m_afPos[1] = y + rowCenterOffset(fontSize);
    stringEntry.m_iFont = fontIndex;
    stringEntry.m_fSize = fontSize;
    stringEntry.m_iJustify = justify;
    stringEntry.m_ulColor = color;

    m_strings.push_back(stringEntry);
    m_stringSkipShadow.push_back(skipShadow);  // Track shadow flag (shadow generated at collection time)
}

// Themed band behind the title row.
//
// This is the first theme element that is not a FRAME. A 9-slice frames a rect the
// HUD already has; a title band is NEW geometry, drawn where the HUD previously
// drew nothing -- which is why the "chevron" theme could not fake one from its top
// edge: an edge slice only ever occupies the corner inset (a fraction of a percent
// of screen height) and can never reach down behind the title text.
//
// Reuses the panel's own nine slices at a reduced inset, so it reads as an inner
// card in the theme's own language rather than needing a second sprite set. That is
// deliberately the FIRST thing to try; a theme that wants a distinct title
// treatment would need its own slices, and this is how we find out whether it does.
//
// Emitted from the caption path (after addBackgroundQuad, which every HUD calls
// first) so it lands on top of the panel background and behind all text -- the game
// draws every quad before every string, so text ordering takes care of itself.
// Whether a band would actually be drawn. Split out of the band emitter so a caller
// can RESERVE the band's height before emitting it -- the settings panel computes
// its panel height before it has laid anything out, and a predicate that drifted
// from the emitter's early-outs would reserve space for a band that never appears.
// Does the THEME draw a band for this panel kind -- regardless of whether this panel's
// caption is switched on. Width must not depend on the title toggle; see
// contentPaddingX().
bool BaseHud::themeDrawsTitleBandKind() const {
    const ThemeAsset* theme = activeTheme();
    return theme->titleBandKind(static_cast<int>(m_panelKind)) && theme->hasCard();
}

bool BaseHud::hasThemedTitleBand() const {
    const ThemeAsset* theme = activeTheme();
    // hasCard(): reusing the panel's own slices here was the first attempt and it
    // produced brackets-inside-brackets on any theme with a real corner motif, so a
    // theme without card_center simply has no band.
    return theme && theme->titleBandKind(static_cast<int>(m_panelKind)) && theme->hasCard();
}


