// ============================================================================
// hud/settings/settings_tab_dash.cpp
// Tab renderer for Dash settings: the standard HUD controls and which pages the
// card cycles through.
// ============================================================================
#include "settings_controls.h"
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../dash_hud.h"
#include "../../core/color_config.h"
#include "../../core/hud_manager.h"

// Static member function of SettingsHud - inherits friend access to DashHud
BaseHud* SettingsHud::renderTabDash(SettingsLayoutContext& ctx) {
    DashHud* hud = &HudManager::getInstance().getDashHud();

    ctx.addTabTooltip("dash");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeading("Appearance");
    ctx.addStandardHudControls(hud);
    // How the card behaves, apart from the page list below so they do not
    // read as pages to switch on.
    static const char* const kStripPlaces[] = { "Top", "Right", "Bottom", "Left", "Off" };
    ctx.addCycleControl("Page strip", kStripPlaces[static_cast<int>(hud->m_stripPlace)],
        SettingsHud::CycleControl::enumMember(hud, &DashHud::m_stripPlace,
                                              static_cast<int>(DashHud::StripPlace::COUNT), hud, kStripPlaces),
        hud, true, hud->m_stripPlace == DashHud::StripPlace::OFF, "dash.strip", /*tooltipOnArrows=*/false);
    ctx.addToggleControl("Page names", (hud->m_options & DashHud::OPT_PAGE_NAMES) != 0,
                         SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_options, DashHud::OPT_PAGE_NAMES,
                         true, "dash.page_names");
    ctx.addToggleControl("Notice focus", (hud->m_options & DashHud::OPT_NOTICE_TAKEOVER) != 0,
                         SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_options, DashHud::OPT_NOTICE_TAKEOVER,
                         true, "dash.notice_takeover");

    // === PAGES SECTION ===
    // The card's pages, in cycle order. The last one left on is locked,
    // so the card always has a page to show.
    ctx.addSectionHeading("Pages");
    struct PageRow { DashHud::Page page; const char* label; const char* tooltip; };
    static constexpr PageRow kRows[] = {
        { DashHud::Page::STANDINGS, "Standings", "dash.page_standings" },
        { DashHud::Page::TIMING,  "Timing",  "dash.page_timing" },
        { DashHud::Page::LAPS,    "Laps",    "dash.page_laps" },
        { DashHud::Page::TIME,    "Time",    "dash.page_time" },
        { DashHud::Page::FUEL,    "Fuel",    "dash.page_fuel" },
        { DashHud::Page::GAP_BAR, "Gap Bar", "dash.page_gapbar" },
        { DashHud::Page::FLAT_MAP, "Flat Map", "dash.page_flatmap" },
        { DashHud::Page::IDEAL,   "Ideal",   "dash.page_ideal" },
        { DashHud::Page::RPM,     "RPM",     "dash.page_rpm" },
        { DashHud::Page::DELTA_TRACE, "Delta Trace", "dash.page_deltatrace" },
        { DashHud::Page::GAP_CHART, "Gap Chart", "dash.page_gapchart" },
        { DashHud::Page::SESSION, "Session", "dash.page_session" },
        { DashHud::Page::TELEMETRY, "Telemetry", "dash.page_telemetry" },
        { DashHud::Page::EVENT_LOG, "Event Log", "dash.page_eventlog" },
        { DashHud::Page::NOW_PLAYING, "Now Playing", "dash.page_nowplaying" },
    };
    const bool lastOne = (hud->m_enabledPages & (hud->m_enabledPages - 1)) == 0;
    ctx.beginColumns(2, static_cast<int>(sizeof(kRows) / sizeof(kRows[0])));
    for (const PageRow& row : kRows) {
        const bool on = hud->pageEnabled(row.page);
        ctx.addToggleControl(row.label, on, SettingsHud::ClickRegion::CHECKBOX, hud,
                             &hud->m_enabledPages, DashHud::pageBit(row.page), !(on && lastOne),
                             row.tooltip);
    }
    ctx.endColumns();

    // === PAGE SETTINGS SECTION ===
    // Information only, in the About tab's prose colour: the Dash has no settings
    // of its own for what a page shows, so this says where they are. One sentence
    // per row inside the content column (as the Spotter tab's Beta note).
    ctx.addSectionHeading("Page Settings");
    const unsigned long prose = ColorConfig::getInstance().getSecondary();
    ctx.addTextRow("Each page follows the settings of the HUD it comes from.", prose);
    ctx.addTextRow("To change what a page shows, use that HUD's own tab.", prose);
    ctx.addTextRow("The HUD itself can stay off: its page still works.", prose);

    return hud;
}
