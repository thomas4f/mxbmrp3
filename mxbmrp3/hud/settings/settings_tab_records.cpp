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
        case ClickRegion::RECORDS_COUNT_UP:
            if (m_records && m_records->m_recordsToShow < 30) {
                m_records->m_recordsToShow++;
                m_records->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::RECORDS_COUNT_DOWN:
            if (m_records && m_records->m_recordsToShow > 4) {
                m_records->m_recordsToShow--;
                m_records->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::RECORDS_PROVIDER_UP:
            if (m_records) {
                int current = static_cast<int>(m_records->m_provider);
                int count = static_cast<int>(RecordsHud::DataProvider::COUNT);
                m_records->m_provider = static_cast<RecordsHud::DataProvider>((current + 1) % count);
                m_records->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::RECORDS_PROVIDER_DOWN:
            if (m_records) {
                int current = static_cast<int>(m_records->m_provider);
                int count = static_cast<int>(RecordsHud::DataProvider::COUNT);
                m_records->m_provider = static_cast<RecordsHud::DataProvider>((current + count - 1) % count);
                m_records->setDataDirty();
                setDataDirty();
            }
            return true;

        case ClickRegion::RECORDS_AUTO_FETCH_TOGGLE:
            if (m_records) {
                m_records->m_bAutoFetch = !m_records->m_bAutoFetch;
                // If enabling and we're already in an event, fetch immediately
                if (m_records->m_bAutoFetch &&
                    m_records->m_lastSessionTrackId[0] != '\0' &&
                    m_records->m_fetchState != RecordsHud::FetchState::FETCHING) {
                    m_records->startFetch();
                }
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

    // === CONFIGURATION SECTION ===
    ctx.addSectionHeader("Configuration");

    // Provider control
    const char* providerName = "Unknown";
    switch (hud->m_provider) {
        case RecordsHud::DataProvider::CBR: providerName = "CBR"; break;
        case RecordsHud::DataProvider::MXB_RANKED: providerName = "MXB Ranked"; break;
        default: break;
    }
    ctx.addCycleControl("Provider", providerName, 10,
        SettingsHud::ClickRegion::RECORDS_PROVIDER_DOWN,
        SettingsHud::ClickRegion::RECORDS_PROVIDER_UP,
        hud, true, false, "records.provider");

    // Rows count control
    char recordsValue[8];
    snprintf(recordsValue, sizeof(recordsValue), "%d", hud->m_recordsToShow);
    ctx.addCycleControl("Records to display", recordsValue, 10,
        SettingsHud::ClickRegion::RECORDS_COUNT_DOWN,
        SettingsHud::ClickRegion::RECORDS_COUNT_UP,
        hud, true, false, "records.count");

    // Auto-fetch toggle
    ctx.addToggleControl("Auto-fetch", hud->m_bAutoFetch,
        SettingsHud::ClickRegion::RECORDS_AUTO_FETCH_TOGGLE, hud, nullptr, 0, true,
        "records.autofetch");
    ctx.addSpacing(0.5f);

    // === COLUMNS SECTION ===
    // Core columns (Position, Rider, Bike, Lap time) are always shown
    ctx.addSectionHeader("Optional Columns");

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
    ctx.parent->addString("Your records are saved to mxbmrp3_personal_bests.json", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, PluginConstants::Fonts::getNormal(),
        ColorConfig::getInstance().getMuted(), ctx.fontSize * 0.9f);

    return hud;
}
