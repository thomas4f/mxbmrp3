// ============================================================================
// core/test_hooks_find.cpp
// The MXBMRP3_Test_* exports for the settings Find box (settings_find_test,
// companion_demo's "find" mode): show the results for a query as if it had been
// typed and Enter pressed, and read the hits back -- and for the room each drawn
// settings text has (i18n_limits_test), which is what a translation must fit.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "plugin_utils.h"
#include "../hud/settings_hud.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {

// Open the Find page on `query` (empty: close it) and draw it; the hits come back
// as "Tab|label|value|tooltipId|section" lines, name hits first, into `out`. Returns the
// hit count, or -1 when the index is empty.
__declspec(dllexport) int MXBMRP3_Test_SettingsFind(const char* query, char* out, int cap) {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    settings.testFindQuery(query ? query : "");
    const SettingsFind::State& f = settings.testFind();
    std::string joined;
    for (int i : f.hits) {
        const SettingsFind::Entry& e = f.index[static_cast<size_t>(i)];
        joined += settings.testTabNameForIndex(e.tab);
        joined += '|'; joined += e.label;
        joined += '|'; joined += e.value;
        joined += '|'; joined += e.tooltipId;
        joined += '|'; joined += e.section;
        joined += '\n';
    }
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", joined.c_str());
    return f.index.empty() ? -1 : static_cast<int>(f.hits.size());
}

// The row a Find result opened, if it is still lit and the built tab carries it:
// its tooltip id into `out`, 1; else 0.
__declspec(dllexport) int MXBMRP3_Test_SettingsFindLanding(char* out, int cap) {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    const std::string& id = settings.testFind().landTooltipId;
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", id.c_str());
    return !id.empty() && settings.testHasRegionWithTooltip(id.c_str()) ? 1 : 0;
}

// The settings panel as drawn now, one line per text:
// "room<TAB>overlap<TAB>text<TAB>next<TAB>x,y". `room` is how many characters
// fit before the next text on that row starts (the monospace cell the grid lays
// out by) or, with nothing after it ("|"), before its column's edge; a centred
// text is held by the nearer edge on both sides. `overlap` is 1 when the text
// already runs past its room; x,y is where it starts (in 1/10000), which a
// longer translation of a left-aligned text does not move. A label's room is
// the most a translation of it may be; the caller reads it in English. A
// right-aligned text is listed with room -1 (see below).
__declspec(dllexport) int MXBMRP3_Test_SettingsTextRoom(char* out, int cap) {
    if (out && cap > 0) out[0] = '\0';
    const SettingsHud& hud = HudManager::getInstance().getSettingsHud();
    const auto& strings = hud.getStrings();
    auto leftOf = [](const auto& s) {
        const float w = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(std::strlen(s.m_szString)), s.m_fSize);
        return s.m_iJustify == 1 ? s.m_afPos[0] - w * 0.5f : s.m_iJustify == 2 ? s.m_afPos[0] - w : s.m_afPos[0];
    };
    // A column's edge: the content row's right edge (where right-aligned glyphs
    // end), or the sidebar card's right edge less the inset the content labels
    // keep from their own card.
    float sidebarRight = 0.0f, contentLeft = 0.0f, labelX = 0.0f, controlX = 0.0f, rowRight = 0.0f;
    hud.testCardEdgesX(sidebarRight, contentLeft);
    hud.testContentColumnX(labelX, controlX, rowRight);
    const float sidebarEdge = sidebarRight - (labelX - contentLeft);
    std::string joined;
    int lines = 0;
    for (const auto& s : strings) {
        if (!s.m_szString[0]) continue;
        const int len = static_cast<int>(std::strlen(s.m_szString));
        const int keyX = static_cast<int>(std::lround(leftOf(s) * 10000.0f));
        const int keyY = static_cast<int>(std::lround(s.m_afPos[1] * 10000.0f));
        char line[512];
        // A right-aligned text (the sidebar's New / Beta tags) grows LEFT, into
        // whatever precedes it: room -1, "no longer than it is now".
        if (s.m_iJustify == 2) {
            std::snprintf(line, sizeof(line), "-1\t0\t%s\t\t%d,%d\n", s.m_szString, keyX, keyY);
            joined += line;
            ++lines;
            continue;
        }
        const float cell = PluginUtils::calculateMonospaceTextWidth(1, s.m_fSize);
        if (cell <= 0.0f) continue;
        const bool inSidebar = s.m_afPos[0] < contentLeft;
        const float edge = inSidebar ? sidebarEdge : rowRight;
        float room = 0.0f;
        const char* nextText = "|";
        if (s.m_iJustify == 1) {
            const float left = inSidebar ? sidebarEdge - (sidebarRight - contentLeft) : labelX;
            room = 2.0f * std::fmin(s.m_afPos[0] - left, edge - s.m_afPos[0]);
        } else {
            float next = edge;
            for (const auto& o : strings) {
                if (&o == &s || !o.m_szString[0] || std::fabs(o.m_afPos[1] - s.m_afPos[1]) > s.m_fSize * 0.25f) continue;
                const float l = leftOf(o);
                if (l > s.m_afPos[0] + cell * 0.5f && l < next) { next = l; nextText = o.m_szString; }
            }
            room = next - s.m_afPos[0];
        }
        if (room <= 0.0f) continue;
        const int cells = static_cast<int>(std::floor(room / cell + 1e-3f));
        std::snprintf(line, sizeof(line), "%d\t%d\t%s\t%s\t%d,%d\n", cells, len > cells ? 1 : 0, s.m_szString, nextText, keyX, keyY);
        joined += line;
        ++lines;
    }
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", joined.c_str());
    return lines;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
