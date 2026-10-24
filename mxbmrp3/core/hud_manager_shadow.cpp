// ============================================================================
// core/hud_manager_shadow.cpp
// DROP-SHADOWED ICONS. With drop shadow on, an icon beside shadowed text gets
// the text's shadow too: the title icon (BaseHud::addPlanTitle) and every icon
// a HUD marks with BaseHud::shadowLastIcon - the Event Log's event icons, the
// Standings carets, the Dash strip, the Toggles strip (bare over the scene).
// The settings sidebar's icons sit on the menu's own panel and stay unmarked.
//
// The shadow is one quad inserted in front of its icon, so every local quad
// index after it moves up by one: m_iconShifts lists the shadowed indices for
// motionFinishHud and recordLayerSpan, which map the HUD's ranges through it.
//
// Pinned by tests/integration/tests/drop_shadow_test.cpp.
// ============================================================================
#include "hud_manager.h"

#include <algorithm>
#include <cmath>

#include "layout_config.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "../hud/base_hud.h"

size_t HudManager::iconShiftsUpTo(const std::vector<int>* shifts, int local) {
    if (!shifts || shifts->empty()) return 0;
    return static_cast<size_t>(std::upper_bound(shifts->begin(), shifts->end(), local) - shifts->begin());
}

void HudManager::appendHudQuads(const BaseHud& hud, std::vector<SPluginQuad_t>& outQuads,
                                bool shadowTitleIcon, bool hudShadow) {
    const auto& hudQuads = hud.getQuads();
    const int quadCount = static_cast<int>(hudQuads.size());
    const int titleIdx = shadowTitleIcon ? hud.m_titleIconQuadIndex : -1;

    // Which quads get a shadow, ascending and once each. A handful per HUD,
    // into a reused buffer: no allocation once warm.
    m_iconShifts.clear();
    if (titleIdx >= 0) m_iconShifts.push_back(titleIdx);
    if (hudShadow)
        for (int idx : hud.m_shadowIcons)
            if (idx >= 0 && idx < quadCount) m_iconShifts.push_back(idx);
    if (m_iconShifts.empty()) {
        outQuads.insert(outQuads.end(), hudQuads.begin(), hudQuads.end());
        return;
    }
    std::sort(m_iconShifts.begin(), m_iconShifts.end());
    m_iconShifts.erase(std::unique(m_iconShifts.begin(), m_iconShifts.end()), m_iconShifts.end());

    const UiConfig& ui = UiConfig::getInstance();
    const float offX = ui.getDropShadowOffsetX(), offY = ui.getDropShadowOffsetY();
    const unsigned long shadowColor = ui.getDropShadowColor();
    // Bulk-copy the quads up to each shadowed icon, then its tinted, offset copy
    // (renders behind), then the rest.
    int copied = 0;
    for (int idx : m_iconShifts) {
        outQuads.insert(outQuads.end(), hudQuads.begin() + copied, hudQuads.begin() + idx);
        copied = idx;
        // Offset proportional to the icon's height and capped at EXTRA_LARGE,
        // matching the string formula. fabs: a rotated sprite (the Standings'
        // down caret) runs its corners the other way up.
        const SPluginQuad_t& icon = hudQuads[idx];
        SPluginQuad_t shadow = icon;
        const float size = std::min(std::fabs(icon.m_aafPos[1][1] - icon.m_aafPos[0][1]),
                                    layoutDefaults().fontSizeExtraLarge);
        for (int c = 0; c < 4; ++c) {
            shadow.m_aafPos[c][0] += size * offX;
            shadow.m_aafPos[c][1] += size * offY;
        }
        // A body icon's shadow takes the icon's own alpha, as a string's does: a
        // HUD dims and fades its icons. The title icon keeps the verbatim colour.
        shadow.m_ulColor = idx == titleIdx ? shadowColor : PluginUtils::modulateAlpha(shadowColor, icon.m_ulColor);
        outQuads.push_back(shadow);
    }
    outQuads.insert(outQuads.end(), hudQuads.begin() + copied, hudQuads.end());
}
