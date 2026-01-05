// ============================================================================
// hud/settings/settings_tab_standings.cpp
// Tab renderer for Standings HUD settings
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../standings_hud.h"
#include "../../core/settings_manager.h"

// Static member function of SettingsHud - handles click events for Standings tab
bool SettingsHud::handleClickTabStandings(const ClickRegion& region) {
    StandingsHud* standingsHud = dynamic_cast<StandingsHud*>(region.targetHud);
    if (!standingsHud) standingsHud = m_standings;

    switch (region.type) {
        case ClickRegion::ROW_COUNT_UP:
            if (standingsHud) {
                int newRowCount = standingsHud->m_displayRowCount + 2;
                if (newRowCount > StandingsHud::MAX_ROW_COUNT) newRowCount = StandingsHud::MAX_ROW_COUNT;
                standingsHud->m_displayRowCount = newRowCount;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::ROW_COUNT_DOWN:
            if (standingsHud) {
                int newRowCount = standingsHud->m_displayRowCount - 2;
                if (newRowCount < StandingsHud::MIN_ROW_COUNT) newRowCount = StandingsHud::MIN_ROW_COUNT;
                standingsHud->m_displayRowCount = newRowCount;
                standingsHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_MODE_UP:
        case ClickRegion::GAP_MODE_DOWN:
            {
                auto* gapMode = std::get_if<StandingsHud::GapMode*>(&region.targetPointer);
                if (!gapMode || !*gapMode || !region.targetHud) return false;

                bool forward = (region.type == ClickRegion::GAP_MODE_UP);
                if (forward) {
                    switch (**gapMode) {
                        case StandingsHud::GapMode::OFF:
                            **gapMode = StandingsHud::GapMode::PLAYER;
                            break;
                        case StandingsHud::GapMode::PLAYER:
                            **gapMode = StandingsHud::GapMode::ALL;
                            break;
                        case StandingsHud::GapMode::ALL:
                            **gapMode = StandingsHud::GapMode::OFF;
                            break;
                        default:
                            **gapMode = StandingsHud::GapMode::OFF;
                            break;
                    }
                } else {
                    switch (**gapMode) {
                        case StandingsHud::GapMode::OFF:
                            **gapMode = StandingsHud::GapMode::ALL;
                            break;
                        case StandingsHud::GapMode::PLAYER:
                            **gapMode = StandingsHud::GapMode::OFF;
                            break;
                        case StandingsHud::GapMode::ALL:
                            **gapMode = StandingsHud::GapMode::PLAYER;
                            break;
                        default:
                            **gapMode = StandingsHud::GapMode::OFF;
                            break;
                    }
                }
                region.targetHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_INDICATOR_UP:
        case ClickRegion::GAP_INDICATOR_DOWN:
            {
                auto* gapIndicatorMode = std::get_if<StandingsHud::GapIndicatorMode*>(&region.targetPointer);
                if (!gapIndicatorMode || !*gapIndicatorMode || !region.targetHud) return false;

                bool forward = (region.type == ClickRegion::GAP_INDICATOR_UP);
                if (forward) {
                    switch (**gapIndicatorMode) {
                        case StandingsHud::GapIndicatorMode::OFF:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFFICIAL;
                            break;
                        case StandingsHud::GapIndicatorMode::OFFICIAL:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::LIVE;
                            break;
                        case StandingsHud::GapIndicatorMode::LIVE:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::BOTH;
                            break;
                        case StandingsHud::GapIndicatorMode::BOTH:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                            break;
                        default:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                            break;
                    }
                } else {
                    switch (**gapIndicatorMode) {
                        case StandingsHud::GapIndicatorMode::OFF:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::BOTH;
                            break;
                        case StandingsHud::GapIndicatorMode::OFFICIAL:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                            break;
                        case StandingsHud::GapIndicatorMode::LIVE:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFFICIAL;
                            break;
                        case StandingsHud::GapIndicatorMode::BOTH:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::LIVE;
                            break;
                        default:
                            **gapIndicatorMode = StandingsHud::GapIndicatorMode::OFF;
                            break;
                    }
                }
                region.targetHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        case ClickRegion::GAP_REFERENCE_UP:
        case ClickRegion::GAP_REFERENCE_DOWN:
            {
                auto* gapReferenceMode = std::get_if<StandingsHud::GapReferenceMode*>(&region.targetPointer);
                if (!gapReferenceMode || !*gapReferenceMode || !region.targetHud) return false;

                // Toggle between LEADER and PLAYER (only two modes)
                if (**gapReferenceMode == StandingsHud::GapReferenceMode::LEADER) {
                    **gapReferenceMode = StandingsHud::GapReferenceMode::PLAYER;
                } else {
                    **gapReferenceMode = StandingsHud::GapReferenceMode::LEADER;
                }
                region.targetHud->setDataDirty();
                rebuildRenderData();
            }
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud - inherits friend access to StandingsHud
BaseHud* SettingsHud::renderTabStandings(SettingsLayoutContext& ctx) {
    StandingsHud* hud = ctx.parent->getStandingsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("standings");

    // === APPEARANCE SECTION ===
    ctx.addSectionHeader("Appearance");
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === CONFIGURATION SECTION ===
    ctx.addSectionHeader("Configuration");

    // Row count
    char rowCountValue[8];
    snprintf(rowCountValue, sizeof(rowCountValue), "%d", hud->m_displayRowCount);
    ctx.addCycleControl("Rows to display", rowCountValue, 10,
        SettingsHud::ClickRegion::ROW_COUNT_DOWN,
        SettingsHud::ClickRegion::ROW_COUNT_UP,
        hud, true, false, "standings.rows");
    ctx.addSpacing(0.5f);

    // === COLUMNS SECTION ===
    ctx.addSectionHeader("Columns");

    // Column toggles - using addToggleControl with tooltips
    ctx.addToggleControl("Tracked rider marker", (hud->m_enabledColumns & StandingsHud::COL_TRACKED) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_TRACKED, true,
        "standings.col_tracked");
    ctx.addToggleControl("Position number", (hud->m_enabledColumns & StandingsHud::COL_POS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_POS, true,
        "standings.col_pos");
    ctx.addToggleControl("Race number", (hud->m_enabledColumns & StandingsHud::COL_RACENUM) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_RACENUM, true,
        "standings.col_racenum");
    ctx.addToggleControl("Rider name", (hud->m_enabledColumns & StandingsHud::COL_NAME) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_NAME, true,
        "standings.col_name");
    ctx.addToggleControl("Bike model", (hud->m_enabledColumns & StandingsHud::COL_BIKE) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BIKE, true,
        "standings.col_bike");
    ctx.addToggleControl("Connection status", (hud->m_enabledColumns & StandingsHud::COL_STATUS) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_STATUS, true,
        "standings.col_status");
    ctx.addToggleControl("Penalty indicator", (hud->m_enabledColumns & StandingsHud::COL_PENALTY) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_PENALTY, true,
        "standings.col_penalty");
    ctx.addToggleControl("Best lap time", (hud->m_enabledColumns & StandingsHud::COL_BEST_LAP) != 0,
        SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_BEST_LAP, true,
        "standings.col_bestlap");

    // Debug column - only visible in developer mode
    if (SettingsManager::getInstance().isDeveloperMode()) {
        ctx.addToggleControl("Debug data", (hud->m_enabledColumns & StandingsHud::COL_DEBUG) != 0,
            SettingsHud::ClickRegion::CHECKBOX, hud, &hud->m_enabledColumns, StandingsHud::COL_DEBUG, true,
            "standings.col_debug");
    }

    ctx.addSpacing(0.5f);

    // === GAPS SECTION ===
    ctx.addSectionHeader("Gap Display");

    // Official gap column mode
    const char* officialGapValue;
    switch (hud->m_officialGapMode) {
        case StandingsHud::GapMode::OFF:    officialGapValue = "Off"; break;
        case StandingsHud::GapMode::PLAYER: officialGapValue = "Player"; break;
        case StandingsHud::GapMode::ALL:    officialGapValue = "All"; break;
        default: officialGapValue = "Off"; break;
    }
    // Add row-wide tooltip region and custom cycle control for gap mode
    float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "standings.col_official_gap"
    ));
    ctx.parent->addString("Official gap column", ctx.labelX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), ctx.fontSize);
    // Cycle control
    float controlX = ctx.controlX;
    ctx.parent->addString("<", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_MODE_DOWN, &hud->m_officialGapMode, hud
    ));
    controlX += cw * 2;
    std::string formattedOfficialGap = SettingsLayoutContext::formatValue(officialGapValue, 10, false);
    ctx.parent->addString(formattedOfficialGap.c_str(), controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(),
        hud->m_officialGapMode == StandingsHud::GapMode::OFF ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getPrimary(),
        ctx.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    ctx.parent->addString(" >", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_MODE_UP, &hud->m_officialGapMode, hud
    ));
    ctx.currentY += ctx.lineHeightNormal;

    // Live gap column mode
    const char* liveGapValue;
    switch (hud->m_liveGapMode) {
        case StandingsHud::GapMode::OFF:    liveGapValue = "Off"; break;
        case StandingsHud::GapMode::PLAYER: liveGapValue = "Player"; break;
        case StandingsHud::GapMode::ALL:    liveGapValue = "All"; break;
        default: liveGapValue = "Off"; break;
    }
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "standings.col_live_gap"
    ));
    ctx.parent->addString("Live gap column", ctx.labelX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), ctx.fontSize);
    controlX = ctx.controlX;
    ctx.parent->addString("<", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_MODE_DOWN, &hud->m_liveGapMode, hud
    ));
    controlX += cw * 2;
    std::string formattedLiveGap = SettingsLayoutContext::formatValue(liveGapValue, 10, false);
    ctx.parent->addString(formattedLiveGap.c_str(), controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(),
        hud->m_liveGapMode == StandingsHud::GapMode::OFF ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getPrimary(),
        ctx.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    ctx.parent->addString(" >", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_MODE_UP, &hud->m_liveGapMode, hud
    ));
    ctx.currentY += ctx.lineHeightNormal;

    // Adjacent gap indicator mode
    const char* gapIndicatorValue;
    switch (hud->m_gapIndicatorMode) {
        case StandingsHud::GapIndicatorMode::OFF:      gapIndicatorValue = "Off"; break;
        case StandingsHud::GapIndicatorMode::OFFICIAL: gapIndicatorValue = "Official"; break;
        case StandingsHud::GapIndicatorMode::LIVE:     gapIndicatorValue = "Live"; break;
        case StandingsHud::GapIndicatorMode::BOTH:     gapIndicatorValue = "Both"; break;
        default: gapIndicatorValue = "Off"; break;
    }
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "standings.gap_indicator"
    ));
    ctx.parent->addString("Adjacent rider gaps", ctx.labelX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), ctx.fontSize);
    controlX = ctx.controlX;
    ctx.parent->addString("<", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_INDICATOR_DOWN, &hud->m_gapIndicatorMode, hud
    ));
    controlX += cw * 2;
    std::string formattedIndicator = SettingsLayoutContext::formatValue(gapIndicatorValue, 10, false);
    ctx.parent->addString(formattedIndicator.c_str(), controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(),
        hud->m_gapIndicatorMode == StandingsHud::GapIndicatorMode::OFF ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getPrimary(),
        ctx.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    ctx.parent->addString(" >", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_INDICATOR_UP, &hud->m_gapIndicatorMode, hud
    ));
    ctx.currentY += ctx.lineHeightNormal;

    // Gap reference mode
    const char* gapRefValue = (hud->m_gapReferenceMode == StandingsHud::GapReferenceMode::LEADER)
        ? "Leader" : "Player";
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        ctx.labelX, ctx.currentY, rowWidth, ctx.lineHeightNormal, "standings.gap_reference"
    ));
    ctx.parent->addString("Gap reference point", ctx.labelX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), ctx.fontSize);
    controlX = ctx.controlX;
    ctx.parent->addString("<", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_REFERENCE_DOWN, &hud->m_gapReferenceMode, hud
    ));
    controlX += cw * 2;
    std::string formattedRef = SettingsLayoutContext::formatValue(gapRefValue, 10, false);
    ctx.parent->addString(formattedRef.c_str(), controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), ctx.fontSize);
    controlX += PluginUtils::calculateMonospaceTextWidth(10, ctx.fontSize);
    ctx.parent->addString(" >", controlX, ctx.currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::getNormal(), ColorConfig::getInstance().getAccent(), ctx.fontSize);
    ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        controlX, ctx.currentY, cw * 2, ctx.lineHeightNormal,
        SettingsHud::ClickRegion::GAP_REFERENCE_UP, &hud->m_gapReferenceMode, hud
    ));
    ctx.currentY += ctx.lineHeightNormal;

    return hud;
}
