// ============================================================================
// hud/settings/settings_tab_widgets.cpp
// Tab renderer for Widgets settings (multi-widget table)
//
// GROUPS, IN ONE SCROLLING LIST. Each widget sits in a group named for what it
// shows (the bike, the race, the plugin's own controls, ...), each group's name
// a row of the list -- the Achievements tab's shape. Seven groups and every row
// at once run past the panel, and the panel is as tall as its tallest tab, so
// the list scrolls (scroll_view.h) in a card as tall as the old fullest page.
//
// ORDER: groups, and the rows inside each, run most-used first by the usage
// survey's widget adoption (usage_survey/REPORT.md), the way the sidebar orders
// its tabs. A group is ranked by its most-used row, so the order is a fair
// reading of the survey rather than an exact one -- Fuel rides with the bike
// ahead of the Race group's Time. Prestige is EARNED, so its survey figure says
// nothing about use and it goes last in its group. Interface is the exception,
// LAST despite Toggles' adoption: its rows are the plugin's own controls, not a
// readout of the ride, and set once -- and Pointer Off is the one switch that
// leaves the menu to its hotkey (its tooltip warns), so it sits out of the way.
//
// The list itself -- its cards, its column labels, the row it lands on (a Find
// result, a "New" row) -- is the grouped HUD list both this tab and Decks draw
// (hud_group_list.cpp); this file states the rows and how tall the list is.
// ============================================================================
#include "settings_layout.h"
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
#if GAME_HAS_RIDER_STANCE
#include "../stance_widget.h"
#endif
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

namespace {

// The groups, and the old pager's page each was on -- kept because the list is
// as tall as the fuller of those two pages was (the panel's height does not
// move). "Movement", not "Motion": that word is already the Appearance tab's
// animation level, which some packs translate as "Animations".
enum Group { BIKE, RACE, INPUTS, EXTRAS, DIALS, MOVEMENT, INTERFACE };

const char* const kGroups[] = {
    N_("Bike"),        // Speed 95%, Gear 94%, RPM 44%, Fuel 10%
    N_("Race"),        // Lap 88%, Position 88%, Time 15%, Crashes 1%
    N_("Inputs"),      // Gamepad 9%, Bars 4%
    N_("Extras"),      // Clock 3%, Now Playing 1%, Prestige (earned)
    N_("Dials"),       // Tacho 1%, Speedo <1%
    N_("Movement"),    // Lean 1%, G-Force <1%, Compass <1%, Slide + Wheelie (new), Stance (where sent)
    N_("Interface"),   // Toggles 86%, Pointer (always on): last, see ORDER
};
constexpr int GROUP_COUNT = static_cast<int>(sizeof(kGroups) / sizeof(kGroups[0]));
// Each group on the page the pager had it on (Interface: the first), whatever its place in the list
constexpr int kGroupPage[GROUP_COUNT] = { 0, 0, 0, 1, 1, 1, 0 };
constexpr int PAGE_COUNT = 2;

// One row (HudGroupList::Row): the widget, or nullptr when this game or this
// player has none to show (Prestige until it is earned). The Pointer row's
// Visible column toggles the menu-only-cursor mode (On = shown while racing,
// Off = only in the settings menu). It can't toggle the widget's real
// visibility because the pointer must stay drawable to appear in the menu; and
// it has no opacity.
//
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
const HudGroupList::Row kRows[] = {
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
#if GAME_HAS_WHEELIE
    { MOVEMENT, N_("Wheelie"), "widgets.wheelie", [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getWheelieWidget(); } },
#endif
#if GAME_HAS_RIDER_STANCE
    // Left out, like Prestige, until the game has sent a stance (stance_widget.h).
    { MOVEMENT, N_("Stance"),  "widgets.stance",  [](const SettingsHud&) -> BaseHud* {
          StanceWidget* stance = HudManager::getInstance().getStanceWidget();
          return (stance && StanceWidget::isAvailable()) ? stance : nullptr; } },
#endif
    { MOVEMENT, N_("G-Force"), "widgets.gforce",  [](const SettingsHud& s) -> BaseHud* { return s.getGForceWidget(); } },
    { MOVEMENT, N_("Compass"), "widgets.compass", [](const SettingsHud& s) -> BaseHud* { return s.getCompassWidget(); } },
    { INTERFACE, N_("Toggles"), "widgets.toggles", [](const SettingsHud&) -> BaseHud* { return &HudManager::getInstance().getToggleStripWidget(); } },
    { INTERFACE, N_("Pointer"), "widgets.pointer", [](const SettingsHud& s) -> BaseHud* { return s.getPointerWidget(); }, true },
};

constexpr int ROW_COUNT = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

}  // namespace

const HudGroupList::List& HudGroupList::widgets() {
    static const List list = { SettingsHud::TAB_WIDGETS, ScrollView::WIDGETS, N_("Widgets"),
                               kGroups, GROUP_COUNT, kRows, ROW_COUNT };
    return list;
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabWidgets(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("widgets");

    SettingsHud& hud = *ctx.parent;

    // What each old page held on this game, for this player: rows and the groups
    // that have one. The list is as tall as the fuller page's body was, so the
    // panel keeps the height the pager gave it.
    int rowsOn[PAGE_COUNT] = {};
    int groupsOn[PAGE_COUNT] = {};
    bool groupHasRow[GROUP_COUNT] = {};
    for (const HudGroupList::Row& row : kRows) {
        if (!row.get(hud)) continue;
        const int p = kGroupPage[row.group];
        rowsOn[p]++;
        if (!groupHasRow[row.group]) groupsOn[p]++;
        groupHasRow[row.group] = true;
    }
    // A group cost its heading row plus the seam between two section cards
    // (the one openSectionCard spells when measuring); the old pager sat in a
    // cardless section of its own under them, which the labels' row replaces.
    const float sectionCost = ctx.lineHeightNormal + hud.cardPadBotY() + hud.contentGapY() + hud.cardPadTopY();
    float fullBody = 0.0f;
    for (int p = 0; p < PAGE_COUNT; ++p) {
        fullBody = std::max(fullBody, static_cast<float>(rowsOn[p]) * ctx.lineHeightNormal +
                                      static_cast<float>(groupsOn[p]) * sectionCost);
    }
    // The labels' card and its seam come out of that body; the list takes the rest
    const float seam = sectionCost - ctx.lineHeightNormal;
    ctx.addHudGroupList(HudGroupList::widgets(), (fullBody - seam) / ctx.lineHeightNormal);

    // No active HUD for multi-widget tab
    return nullptr;
}
