// ============================================================================
// hud/settings/settings_tab_widgets.cpp
// Tab renderer for Widgets settings (multi-widget table)
//
// GROUPS, PAGED. Each widget sits in a group named for what it shows (the bike,
// the race, the plugin's own controls, ...), and a page holds whole groups --
// the Achievements tab's rule, "related rows read together". Seven headings and
// every row on one page ran past the panel, and the panel is as tall as its
// tallest tab, so one long tab would have stretched every other.
//
// ORDER: groups, and the rows inside each, run most-used first by the usage
// survey's widget adoption (usage_survey/REPORT.md), the way the sidebar orders
// its tabs. A group is ranked by its most-used row, so the order is a fair
// reading of the survey rather than an exact one -- Fuel rides with the bike
// ahead of the Race group's Time. Pointer (always on) is not measured; Prestige
// is EARNED, so its survey figure says nothing about use and it goes last.
//
// The pager is the Achievements tab's: "< Page x/y >" at the same height on
// every page -- but under the cards, on the panel, rather than in the last
// one. A page jumps when the panel is asked to show a row on another one: a
// Find result, or a "New" row (landWidgetsPage).
// ============================================================================
#include "settings_layout.h"
#include "whats_new.h"
#include "../settings_hud.h"
#include "../../game/game_config.h"
#include "../../core/hud_manager.h"
#include "../../core/i18n.h"
#include "../bars_widget.h"
#include "../clock_widget.h"
#include "../compass_widget.h"
#include "../crash_widget.h"
#include "../now_playing_widget.h"
#include "../rpm_widget.h"
#include "../toggle_strip_widget.h"
#include "../fuel_widget.h"
#include "../gamepad_widget.h"
#include "../gear_widget.h"
#include "../gforce_widget.h"
#include "../prestige_widget.h"
#include "../lap_widget.h"
#include "../lean_widget.h"
#include "../slide_widget.h"
#include "../wheelie_widget.h"
#include "../pointer_widget.h"
#include "../position_widget.h"
#include "../speed_widget.h"
#include "../speedo_widget.h"
#include "../tacho_widget.h"
#include "../time_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../ecu_widget.h"
#endif

#include <algorithm>
#include <cstring>

namespace {

// A group: its heading and the page it is on. "Movement", not "Motion": that
// word is already the Appearance tab's animation level, which some packs
// translate as "Animations".
struct WidgetGroup {
    const char* title;
    int page;
};

enum Group { BIKE, RACE, INTERFACE, INPUTS, EXTRAS, DIALS, MOVEMENT };

// A page's first heading shares the name column with the table's column
// labels, so it is held to that column (tests/fixtures/i18n_limits.json).
constexpr WidgetGroup kGroups[] = {
    { N_("Bike"),      0 },   // Speed 95%, Gear 94%, RPM 44%, Fuel 10%
    { N_("Race"),      0 },   // Lap 88%, Position 88%, Time 15%, Crashes 1%
    { N_("Interface"), 0 },   // Toggles 86%, Pointer (always on)
    { N_("Inputs"),    0 },   // Gamepad 9%, Bars 4%
    { N_("Extras"),    1 },   // Clock 3%, Now Playing 1%, Prestige (earned)
    { N_("Dials"),     1 },   // Tacho 1%, Speedo <1%
    { N_("Movement"),  1 },   // Lean 1%, G-Force <1%, Compass <1%, Slide + Wheelie (new)
};
constexpr int GROUP_COUNT = static_cast<int>(sizeof(kGroups) / sizeof(kGroups[0]));
constexpr int PAGE_COUNT = 2;

// One row: the widget, or nullptr when this game or this player has none to
// show (Prestige until it is earned).
struct WidgetRow {
    int group;
    const char* name;
    const char* tooltipId;
    BaseHud* (*get)(const SettingsHud&);
    // Pointer only: the Visible column toggles the menu-only-cursor mode (On =
    // shown while racing, Off = only in the settings menu). It can't toggle the
    // widget's real visibility because the pointer must stay drawable to appear
    // in the menu; and it has no opacity.
    bool pointer = false;
};

// The Title column is greyed on Speedo, Tacho, Gamepad, Pointer and Settings,
// and that is the WIDGET's statement, not this list's: each sets
// m_titleSupported = false because a TEXTURE is its panel, so a band would land
// on the artwork rather than above it -- or, for the last three, because a
// cursor and a button have nothing to caption. A bool per row could offer a
// toggle the widget does not honour. Compass is the one gauge that BUILDS a
// title (through the caption path), so it keeps the column.
//
// The Gamepad's Texture column picks the PAD (gamepads/<name>/) rather than a
// texture variant; addWidgetRow routes on BaseHud::m_packKind, so there is no
// flag to pass here and no way for this list to disagree with it.
//
// Prestige is the one row that is EARNED. Left out entirely rather than greyed:
// a greyed row is a control you have not found yet, and this one you have not
// got. Its Texture column is the ordinary variant cycle: the badge artwork IS
// the widget (prestige_widget.h).
const WidgetRow kRows[] = {
    { BIKE, N_("Speed"), "widgets.speed", [](const SettingsHud& s) -> BaseHud* { return s.getSpeedWidget(); } },
    { BIKE, N_("Gear"),  "widgets.gear",  [](const SettingsHud& s) -> BaseHud* { return s.getGearWidget(); } },
    { BIKE, N_("RPM"),   "widgets.rpm",   [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getRpmWidget(); } },
    { BIKE, N_("Fuel"),  "widgets.fuel",  [](const SettingsHud& s) -> BaseHud* { return s.getFuelWidget(); } },
#if GAME_HAS_TYRE_TEMP
    { BIKE, N_("Tyre Temp"), "widgets.tyre_temp", [](const SettingsHud& s) -> BaseHud* { return s.getTyreTempWidget(); } },
#endif
#if GAME_HAS_ECU
    { BIKE, N_("ECU"),   "widgets.ecu",   [](const SettingsHud& s) -> BaseHud* { return s.getEcuWidget(); } },
#endif
    { RACE, N_("Lap"),      "widgets.lap",      [](const SettingsHud& s) -> BaseHud* { return s.getLapWidget(); } },
    { RACE, N_("Position"), "widgets.position", [](const SettingsHud& s) -> BaseHud* { return s.getPositionWidget(); } },
    { RACE, N_("Time"),     "widgets.time",     [](const SettingsHud& s) -> BaseHud* { return s.getTimeWidget(); } },
    { RACE, N_("Crashes"),  "widgets.crashes",  [](const SettingsHud& s) -> BaseHud* { return s.getCrashWidget(); } },
    { INTERFACE, N_("Toggles"), "widgets.toggles", [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getToggleStripWidget(); } },
    { INTERFACE, N_("Pointer"), "widgets.pointer", [](const SettingsHud& s) -> BaseHud* { return s.getPointerWidget(); }, true },
    { INPUTS, N_("Gamepad"), "widgets.gamepad", [](const SettingsHud& s) -> BaseHud* { return s.getGamepadWidget(); } },
    { INPUTS, N_("Bars"),    "widgets.bars",    [](const SettingsHud& s) -> BaseHud* { return s.getBarsWidget(); } },
    { EXTRAS, N_("Clock"),       "widgets.clock",       [](const SettingsHud& s) -> BaseHud* { return s.getClockWidget(); } },
    { EXTRAS, N_("Now Playing"), "widgets.now_playing", [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getNowPlayingWidget(); } },
    { EXTRAS, N_("Prestige"),    "widgets.prestige",    [](const SettingsHud&) -> BaseHud* {
          PrestigeWidget* badge = HudManager::getInstance().getPrestigeWidget();
          return (badge && PrestigeWidget::isUnlocked()) ? badge : nullptr; } },
    { DIALS, N_("Tacho"),  "widgets.tacho",  [](const SettingsHud& s) -> BaseHud* { return s.getTachoWidget(); } },
    { DIALS, N_("Speedo"), "widgets.speedo", [](const SettingsHud& s) -> BaseHud* { return s.getSpeedoWidget(); } },
    { MOVEMENT, N_("Lean"),    "widgets.lean",    [](const SettingsHud& s) -> BaseHud* { return s.getLeanWidget(); } },
    { MOVEMENT, N_("Slide"),   "widgets.slide",   [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getSlideWidget(); } },
    { MOVEMENT, N_("Wheelie"), "widgets.wheelie", [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getWheelieWidget(); } },
    { MOVEMENT, N_("G-Force"), "widgets.gforce",  [](const SettingsHud& s) -> BaseHud* { return s.getGForceWidget(); } },
    { MOVEMENT, N_("Compass"), "widgets.compass", [](const SettingsHud& s) -> BaseHud* { return s.getCompassWidget(); } },
};

// The page holding the row with this tooltip id, or -1.
int pageOfRow(const char* tooltipId) {
    if (!tooltipId || !tooltipId[0]) return -1;
    for (const WidgetRow& row : kRows) {
        if (std::strcmp(row.tooltipId, tooltipId) == 0) return kGroups[row.group].page;
    }
    return -1;
}

}  // namespace

bool SettingsHud::handleClickTabWidgets(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::WIDGETS_PAGE_PREV:
            if (m_widgetsPage > 0) {
                m_widgetsPage--;
                rebuildRenderData();
            }
            return true;
        case ClickRegion::WIDGETS_PAGE_NEXT:
            if (m_widgetsPage + 1 < PAGE_COUNT) {
                m_widgetsPage++;
                rebuildRenderData();
            }
            return true;
        default:
            return false;
    }
}

// THE PAGE A TAB OPENING SHOULD LAND ON, decided when the tab opens rather than
// when it is drawn: a drawing that edits what it draws would also pull the page
// back every time it was rebuilt. A Find result names its row (landTooltipId,
// set just before the tab opens); otherwise a "New" row the player has not
// hovered yet, whose band would be no use on a page nobody turns to. Neither:
// the page the player left it on.
void SettingsHud::landWidgetsPage(int tabId) {
    if (tabId != TAB_WIDGETS) return;
    int page = pageOfRow(m_find.landTooltipId.c_str());
    for (int i = 0; page < 0 && i < static_cast<int>(sizeof(kRows) / sizeof(kRows[0])); ++i) {
        if (WhatsNew::liveForRow(TAB_WIDGETS, kRows[i].tooltipId)) page = kGroups[kRows[i].group].page;
    }
    if (page >= 0) m_widgetsPage = page;
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabWidgets(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("widgets");

    SettingsHud& hud = *ctx.parent;
    // The Find index takes EVERY page: a widget is found by name wherever it is,
    // and the result opens its page (landWidgetsPage).
    const bool allPages = hud.m_find.indexing;
    int& page = hud.m_widgetsPage;
    page = std::clamp(page, 0, PAGE_COUNT - 1);

    // What each page holds on this game, for this player: drawn rows and the
    // groups that have one. Counted before anything is drawn, so every page can
    // be padded to the fullest one's height (below).
    int rowsOn[PAGE_COUNT] = {};
    int groupsOn[PAGE_COUNT] = {};
    bool groupHasRow[GROUP_COUNT] = {};
    for (const WidgetRow& row : kRows) {
        if (!row.get(hud)) continue;
        const int p = kGroups[row.group].page;
        rowsOn[p]++;
        if (!groupHasRow[row.group]) groupsOn[p]++;
        groupHasRow[row.group] = true;
    }

    // Column positions -- the same ones addWidgetRow draws its cells at.
    const SettingsLayoutContext::WidgetColumns cols = ctx.widgetColumns();
    bool labelled = false;
    for (int g = 0; g < GROUP_COUNT; ++g) {
        if (!groupHasRow[g] || (!allPages && kGroups[g].page != page)) continue;
        const float headingY = ctx.addSectionHeading(I18n::tr(kGroups[g].title));
        // The column labels ride on the page's FIRST heading row, in the columns
        // they caption -- the same shape settings_tab_hotkeys.cpp uses. A bare row
        // drawn before any section is opened would sit in the gap between the
        // description block and the first card: outside both, on the description
        // card's bottom edge. A label with no surface behind it reads as a
        // rendering fault. No "Widget" label: the group's name stands in that
        // column and captions it better than the word would.
        if (!labelled) {
            labelled = true;
            const auto columnLabel = [&](const char* text, float x) {
                hud.addString(text, x, headingY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::getStrong(),
                    ColorConfig::getInstance().getPrimary(), ctx.fontSize);
            };
            columnLabel(TR("Visible"), cols.visX);
            columnLabel(TR("Title"), cols.titleX);
            columnLabel(TR("Texture"), cols.texX);
            columnLabel(TR("Opacity"), cols.opacityX);
            columnLabel(TR("Scale"), cols.scaleX);
        }
        for (const WidgetRow& row : kRows) {
            if (row.group != g) continue;
            BaseHud* widget = row.get(hud);
            if (!widget) continue;
            ctx.addWidgetRow(I18n::tr(row.name), widget, !row.pointer, true, !row.pointer, true,
                             row.tooltipId, row.pointer);
        }
    }
    if (allPages) return nullptr;

    // The pager, UNDER the content in a section of its own with no card (the
    // More tabs' Back row's shape; the panel skips its card for this tab in
    // rebuildRenderData), at the foot of a FULL page whatever this page holds --
    // the Achievements tab's rule, and for its reason: a button that moves
    // between pages has to be chased. The shortfall is the air above it, priced
    // in the theme's own units: a row is a row, and a group is its heading row
    // plus the seam the box engine puts between two section cards (the one
    // openSectionCard spells when measuring).
    const float sectionCost = ctx.lineHeightNormal + hud.cardPadBotY() + hud.contentGapY() + hud.cardPadTopY();
    const auto bodyOf = [&](int p) {
        return static_cast<float>(rowsOn[p]) * ctx.lineHeightNormal + static_cast<float>(groupsOn[p]) * sectionCost;
    };
    float fullBody = 0.0f;
    for (int p = 0; p < PAGE_COUNT; ++p) fullBody = std::max(fullBody, bodyOf(p));
    ctx.beginUntitledSection();
    ctx.currentY += fullBody - bodyOf(page);
    ctx.addPager(page, PAGE_COUNT, ClickRegion::WIDGETS_PAGE_PREV, ClickRegion::WIDGETS_PAGE_NEXT);

    // No active HUD for multi-widget tab
    return nullptr;
}
