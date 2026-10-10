// ============================================================================
// hud/settings/settings_tab_more.cpp
// The More page: every tab a section's "More" group holds, one row each --
// its icon (the tab's on/off), name and a short line on what it is.
//
// NOT IN THE TAB LIST, like About: a section's More row in the sidebar opens
// it (SettingsHud::m_moreGroupRow says which section), and the sidebar never
// lists a group's tabs itself. That is the point of it -- the sidebar is one
// row per group whatever is open, so it no longer sets the panel's height by
// unfolding ten rows.
//
// A ROW IS A TAB ROW LAID FLAT. The icon switches the tab on and off through the
// same region type as a sidebar icon (tabToggleType), and the name opens the tab
// (an ordinary ClickRegion::TAB), so the common handlers do the work and the page
// needs no click handler of its own. Hovering shows the
// tab's own description in the tooltip area, and the sidebar keeps the More
// row selected while one of these tabs is open, so the way back is one click.
// ============================================================================
#include <algorithm>
#include <cstring>
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/color_config.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"

using namespace PluginConstants;

namespace {

// What each More tab is, in a few words. Keyed by tooltip id; the full sentence is the tab's tooltip.
struct MoreBlurb { const char* tooltipId; const char* text; };
constexpr MoreBlurb kBlurbs[] = {
    { "radar",          "Riders around you" },
    { "ideal_lap",      "Best sectors combined" },
    { "session",        "Track, server and players" },
    { "performance",    "Frame rate and plugin time" },
    { "stats",          "Laps, crashes, top speed" },
    { "telemetry",      "Throttle, brake, suspension" },
    { "records",        "Online lap records" },
    { "fmx",            "Trick scoring and combos" },
    { "event_log",      "Feed of race events" },
    { "session_charts", "Position and gap charts" },
};

const char* blurbFor(const char* tooltipId) {
    if (!tooltipId) return "";
    for (const MoreBlurb& b : kBlurbs) {
        if (std::strcmp(b.tooltipId, tooltipId) == 0) return b.text;
    }
    return "";
}

// Columns, in characters from the row's label column.
constexpr int NAME_COL   = 4;    // after the icon cell (the sidebar's checkbox width)
constexpr int BLURB_COL  = 17;   // "Performance" is the longest name, 11

}  // namespace

BaseHud* SettingsHud::renderTabMore(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("more");
    SettingsHud& self = *ctx.parent;
    ColorConfig& cc = ColorConfig::getInstance();
    const ScaledDimensions dim = self.getScaledDimensions();
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    int tabs[TAB_COUNT];
    const int count = std::min(self.moreGroupTabs(tabs, TAB_COUNT, nullptr), static_cast<int>(TAB_COUNT));

    ctx.addSectionHeading("More");
    for (int t = 0; t < count; ++t) {
        const TabDescriptor& row = *findTabDescriptor(tabs[t]);
        BaseHud* hud = row.hud ? row.hud(self) : nullptr;
        const float x = ctx.labelX;
        const float y = ctx.currentY;

        // Row-wide hover first: the band and the tab's description. Clicks skip
        // it, so the regions below still take them.
        self.m_clickRegions.push_back(ClickRegion(x, y, ctx.rowSpanWidth(), ctx.lineHeightNormal,
                                                  row.tooltipId));

        // The icon is the tab's on/off, as in the sidebar; one with nothing to
        // switch shows its identity icon.
        const float iconW = cw * static_cast<float>(NAME_COL);
        const char* icon = self.tabIconName(row.tabId, hud);
        bool on = false;
        if (self.tabToggleState(row.tabId, hud, &on)) {
            BaseHud* target = nullptr;
            const ClickRegion::Type type = self.tabToggleType(row.tabId, hud, &target);
            self.m_clickRegions.push_back(ClickRegion(x, y, iconW, ctx.lineHeightNormal, type, target));
            self.drawTabToggle(x, y, icon, on, /*onBand=*/false, dim, iconW);
        } else {
            self.drawTabIcon(x, y, icon ? icon : "", cc.getAccent(), dim, iconW);
        }

        // The name and blurb open the tab.
        ClickRegion open(x + iconW, y, ctx.rowSpanWidth() - iconW, ctx.lineHeightNormal,
                         ClickRegion::TAB);
        open.tabIndex = row.tabId;
        self.m_clickRegions.push_back(open);
        self.addString(row.name, x + iconW, y, Justify::LEFT,
                       Fonts::getNormal(), cc.getAccent(), ctx.fontSize);
        self.addString(blurbFor(row.tooltipId), x + cw * static_cast<float>(BLURB_COL), y,
                       Justify::LEFT, Fonts::getNormal(), cc.getMuted(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;
    }

    return nullptr;
}
