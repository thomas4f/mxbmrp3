// ============================================================================
// core/layout_config.h
// Owns the one global LayoutMetrics, and the sectioned-ini walk a theme is read
// with.
//
// THERE IS NO LAYOUT FILE ANY MORE. There used to be two -- a defaults ini under
// the themes folder, and each theme's own ini overriding it -- and the second half
// never earned its keep: across every theme a user would actually pick, the number
// of layout keys set was zero.
// The metrics are compiled constants now, except the two the user may still tune
// from [Advanced] (see layoutSetFontSize / layoutSetLineHeight).
//
// What survives is this singleton, so those two settings have one home that every
// HUD reads, and layoutForEachIniPairRaw(), which is how AssetManager reads a
// theme's [frame]/[card]/[button]/[colors]/[fonts] sections. That walk stays here
// rather than moving into the theme reader because layout_ini.h owns the LINE
// rules and the unit suite exercises them there; this is just the file loop.
// ============================================================================
#pragma once

#include "ui_config.h"   // mxbBumpThemeGeneration()

#include <string>

#include "layout_ini.h"
#include "layout_metrics.h"

class LayoutConfig {
public:
    static LayoutConfig& getInstance() {
        static LayoutConfig instance;
        return instance;
    }

    // The global metrics. Mutable because [Advanced] sets two of the roots at
    // settings-load time; every other field is a compiled constant.
    // BUMPS THE GENERATION, because handing out a mutable reference is the only
    // way these change and every memo keyed on "has the layout moved" has to see
    // it -- BaseHud's plan memo above all, which would otherwise serve geometry
    // computed from the old box terms. Doing it here rather than at the ten
    // settings-load sites and two test hooks is the difference between a rule and
    // an invariant; none of them is a hot path, so an unconditional bump costs
    // nothing real.
    LayoutMetrics& mutableDefaults() { mxbBumpThemeGeneration(); return m_defaults; }
    const LayoutMetrics& defaults() const { return m_defaults; }

private:
    LayoutConfig() { m_defaults.derive(); }
    LayoutMetrics m_defaults;
};

// Split a hand-edited sectioned ini, handing each pair to `apply` as the SCOPED key
// "section.property". `raw` carries the value as written and `numeric` says whether
// it parsed as a number -- a theme names colours and fonts as well as numbers, and a
// numeric-only walk would hand its author a silently ignored key.
//
// Parsed defensively because it is hand-edited -- see layout_ini.h, which owns the
// line rules.
//
// Returns false when the file could not be opened, which is NOT an error: a theme
// without an ini simply keeps the built-in look.
using LayoutIniRawPairFn = bool (*)(const char* key, float value, const char* raw,
                                    bool numeric, void* ctx);
bool layoutForEachIniPairRaw(const std::string& path, LayoutIniRawPairFn apply, void* ctx);

// The global metrics, for a call site with no HUD in scope: a constructor's
// member-initialiser list, a struct's default member initialiser, a file-local
// helper. BaseHud::layout() returns exactly this -- the distinction the two names
// used to carry (defaults vs the active theme's own metrics) is gone with per-theme
// layout, and layout() is kept only because several hundred call sites read better
// with it.
inline const LayoutMetrics& layoutDefaults() { return LayoutConfig::getInstance().defaults(); }
