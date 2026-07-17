// ============================================================================
// hud/settings/settings_tab_records.cpp
// Tab renderer for Records HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../records_hud.h"

// Static member function of SettingsHud - handles click events for Records tab
bool SettingsHud::handleClickTabRecords(const ClickRegion& region) {
    switch (region.type) {
        // Records to show is a data-driven STEPPED control - registered in
        // renderTabRecords via ctx.addSteppedControl and handled by the shared
        // SettingsHud::applySteppedControl.

        // Provider is a data-driven CYCLE control now - registered in
        // renderTabRecords via ctx.addCycleControl. (Cycling only selects the
        // provider; a fetch happens via Compare / auto-fetch, not here.)

        case ClickRegion::RECORDS_AUTO_FETCH_TOGGLE:
            if (m_records) {
                m_records->m_bAutoFetch = !m_records->m_bAutoFetch;
                // If enabling and we're already in an event, fetch immediately
                if (m_records->m_bAutoFetch &&
                    m_records->m_lastSessionTrackName[0] != '\0' &&
                    m_records->m_fetchState != RecordsHud::FetchState::FETCHING) {
                    m_records->startFetch();
                }
                setDataDirty();
            }
            return true;

        case ClickRegion::RECORDS_HEADERS_TOGGLE:
            if (m_records) {
                m_records->m_bShowHeaders = !m_records->m_bShowHeaders;
                m_records->setDataDirty();
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to RecordsHud
BaseHud* SettingsHud::renderTabRecords(SettingsLayoutContext& ctx) {
    RecordsHud* hud = ctx.parent->getRecordsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("records");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === LAYOUT SECTION ===
    ctx.addSectionHeader("Layout");

    // Provider control
    const char* providerName = "Unknown";
    switch (hud->m_provider) {
        case RecordsHud::DataProvider::CBR: providerName = "CBR"; break;
        case RecordsHud::DataProvider::MXB_RANKED: providerName = "MXB Ranked"; break;
        default: break;
    }
    ctx.addCycleControl("Provider", providerName, 10,
        SettingsHud::CycleControl::enumMember(hud, &RecordsHud::m_provider,
            static_cast<int>(RecordsHud::DataProvider::COUNT), hud),
        hud, true, false, "records.provider", /*tooltipOnArrows=*/false);

    // Rows count control: plain ±1 clamped stepper over [3, 30] with deliberately
    // NO hold acceleration (verbatim from the old RECORDS_COUNT handlers).
    char recordsValue[8];
    snprintf(recordsValue, sizeof(recordsValue), "%d", hud->m_recordsToShow);
    ctx.addSteppedControl("Records to show", recordsValue, 10,
        SettingsHud::SteppedControl::fixedInt(&hud->m_recordsToShow, 1, 3, 30, hud),
        hud, true, false, "records.count");

    // Auto-fetch toggle
    ctx.addToggleControl("Auto-fetch", hud->m_bAutoFetch,
        SettingsHud::ClickRegion::RECORDS_AUTO_FETCH_TOGGLE, hud, nullptr, 0, true,
        "records.autofetch");
    ctx.addSpacing(0.5f);

    // === CONTENT SECTION ===
    // Core columns (Position, Rider, Bike, Lap time) are always shown
    ctx.addSectionHeader("Content");

    // Column-header row
    ctx.addToggleControl("Column headers", hud->m_bShowHeaders,
        SettingsHud::ClickRegion::RECORDS_HEADERS_TOGGLE, hud, nullptr, 0, true,
        "records.headers");

    // Sector columns (toggles all 3 sectors together)
    bool sectorsEnabled = (hud->m_enabledColumns & RecordsHud::COL_SECTORS) == RecordsHud::COL_SECTORS;
    ctx.addToggleControl("Sector times", sectorsEnabled,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, RecordsHud::COL_SECTORS,
        true, "records.col_sectors");

    ctx.addToggleControl("Date recorded", (hud->m_enabledColumns & RecordsHud::COL_DATE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, RecordsHud::COL_DATE, true,
        "records.col_date");

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Your records are saved to mxbmrp3_personal_bests.json.", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        ColorConfig::getInstance().getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
