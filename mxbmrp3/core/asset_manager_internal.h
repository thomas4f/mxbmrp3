// ============================================================================
// core/asset_manager_internal.h
// Shared internal helpers for the AssetManager translation units
// (asset_manager*.cpp). Extracted verbatim from asset_manager.cpp when it was
// split into focused TUs; the values and logic are unchanged. Header-inline
// (were file-local `static` in the single TU) so every AssetManager TU sees
// one definition without ODR conflicts.
// ============================================================================
#pragma once

#include "asset_manager.h"
#include "layout_config.h"
#include "../diagnostics/logger.h"
#include <cstring>
#include <string>
// A theme asked for a title band or a body card and has no card slice set to draw
// one with. Silence here is the same failure the unknown-key warning exists for, one
// level down: the key is spelled right, it parses, it is applied -- and the screen
// does not change, because hasThemedTitleBand()/hasThemedContentCard() both require
// hasCard(). Measured before this existed: a theme with `title-band = 1` and
// `content = 1` but no card_center.tga produced not one line of output.
//
// Gated on cardKeysSet, not on the values: titleBand DEFAULTS to true, so warning on
// the value would fire for every deliberately plain-framed theme, on every load.
inline void warnIfCardWithoutInner(const ThemeAsset& theme) {
    if (!theme.cardKeysSet || theme.hasCard()) return;
    DEBUG_WARN_F("Theme '%s': [card] keys are set but the theme has no card slices, so "
                 "no title band or content card is drawn - add the card_ slice set "
                 "(card_center.tga plus card_corner_*.tga / card_edge_*.tga)",
                 theme.name.c_str());
}

// Peek ONE string key out of a pack ini before the pack is built -- discovery
// needs to know whether a directory is a standalone pack or a skin before it
// decides which phase handles it, and building the whole asset to learn one
// key would run the geometry mapping twice for every pack.
inline std::string readPackStringKey(const std::string& iniPath, const char* fullKey) {
    struct Ctx { const char* key; std::string value; };
    Ctx ctx{ fullKey, {} };
    try {
        layoutForEachIniPairRaw(iniPath, [](const char* key, float, const char* rawValue,
                                            bool, void* raw) -> bool {
            Ctx& c = *static_cast<Ctx*>(raw);
            if (std::strcmp(key, c.key) == 0 && rawValue && *rawValue) c.value = rawValue;
            return true;
        }, &ctx);
    } catch (...) {
        // A malformed ini is the pack's own problem, answered (with a warning)
        // when the full reader runs; the peek just reports "no base".
    }
    return ctx.value;
}
