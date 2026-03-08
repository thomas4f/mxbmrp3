// ============================================================================
// hud/settings/settings_tab_stats.cpp
// Tab renderer for Stats HUD settings — displays overall stats inline
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../stats_hud.h"
#include "../../core/stats_manager.h"
#include "../../core/plugin_utils.h"
#include "../../core/color_config.h"
#include "../../core/font_config.h"

#include <cstdio>
#include <cmath>

// Static member function of SettingsHud - handles click events for Stats tab
bool SettingsHud::handleClickTabStats(const ClickRegion& region) {
    StatsHud* hud = dynamic_cast<StatsHud*>(region.targetHud);
    if (!hud) hud = m_statsHud;
    if (!hud) return false;

    switch (region.type) {
        case ClickRegion::STATS_VISIBILITY_UP: {
            int mode = static_cast<int>(hud->m_visibilityMode);
            mode = (mode + 1) % static_cast<int>(StatsHud::VisibilityMode::COUNT);
            hud->m_visibilityMode = static_cast<StatsHud::VisibilityMode>(mode);
            hud->m_finishAutoShown = false;
            hud->m_wasInPits = false;
            hud->setDataDirty();
            setDataDirty();
            return true;
        }
        case ClickRegion::STATS_VISIBILITY_DOWN: {
            int mode = static_cast<int>(hud->m_visibilityMode);
            mode = (mode - 1 + static_cast<int>(StatsHud::VisibilityMode::COUNT)) % static_cast<int>(StatsHud::VisibilityMode::COUNT);
            hud->m_visibilityMode = static_cast<StatsHud::VisibilityMode>(mode);
            hud->m_finishAutoShown = false;
            hud->m_wasInPits = false;
            hud->setDataDirty();
            setDataDirty();
            return true;
        }
        case ClickRegion::STATS_SHOW_LAP_TOGGLE: {
            hud->m_showLap = !hud->m_showLap;
            hud->setDataDirty();
            setDataDirty();
            return true;
        }
        case ClickRegion::STATS_SHOW_SESSION_TOGGLE: {
            hud->m_showSession = !hud->m_showSession;
            hud->setDataDirty();
            setDataDirty();
            return true;
        }
        case ClickRegion::STATS_SHOW_ALLTIME_TOGGLE: {
            hud->m_showAllTime = !hud->m_showAllTime;
            hud->setDataDirty();
            setDataDirty();
            return true;
        }
        default:
            return false;
    }
}


// Static member function of SettingsHud - inherits friend access to StatsHud
BaseHud* SettingsHud::renderTabStats(SettingsLayoutContext& ctx) {
    StatsHud* hud = ctx.parent->getStatsHud();
    if (!hud) return nullptr;

    ctx.addTabTooltip("stats");

    // Standard controls (Visible, Title, Texture, Opacity, Scale)
    ctx.addStandardHudControls(hud);
    ctx.addSpacing(0.5f);

    // === DISPLAY MODE SECTION ===
    ctx.addSectionHeader("Display Mode");

    // Visibility mode
    const char* visModeName = StatsHud::getVisibilityModeName(hud->m_visibilityMode);
    ctx.addCycleControl("Show", visModeName, 10,
        SettingsHud::ClickRegion::STATS_VISIBILITY_DOWN,
        SettingsHud::ClickRegion::STATS_VISIBILITY_UP,
        hud, true, false, "stats.visibility_mode");

    // Column toggles
    ctx.addToggleControl("Last lap", hud->m_showLap,
        SettingsHud::ClickRegion::STATS_SHOW_LAP_TOGGLE,
        hud, nullptr, 0, true, "stats.show_lap");

    ctx.addToggleControl("Session", hud->m_showSession,
        SettingsHud::ClickRegion::STATS_SHOW_SESSION_TOGGLE,
        hud, nullptr, 0, true, "stats.show_session");

    ctx.addToggleControl("All-time", hud->m_showAllTime,
        SettingsHud::ClickRegion::STATS_SHOW_ALLTIME_TOGGLE,
        hud, nullptr, 0, true, "stats.show_alltime");

    ctx.addSpacing(1.0f);

    // Colors and fonts
    ColorConfig& colors = ColorConfig::getInstance();
    unsigned long secondaryColor = colors.getSecondary();
    unsigned long mutedColor = colors.getMuted();
    int normalFont = PluginConstants::Fonts::getNormal();

    float valueX = ctx.controlX;
    const StatsManager& stats = StatsManager::getInstance();

    // ============================================================
    // Overall Stats
    // ============================================================
    ctx.addSectionHeader("Overall");

    const GlobalStats global = stats.getGlobalStats();

    auto addGlobalRow = [&](const char* label, const char* value) {
        ctx.parent->addString(label, ctx.labelX, ctx.currentY, PluginConstants::Justify::LEFT,
            normalFont, colors.getTertiary(), ctx.fontSize);
        ctx.parent->addString(value, valueX, ctx.currentY, PluginConstants::Justify::LEFT,
            normalFont, secondaryColor, ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;
    };

    char valueBuf[32];

    snprintf(valueBuf, sizeof(valueBuf), "%d", global.raceCount);
    addGlobalRow("Races", valueBuf);

    snprintf(valueBuf, sizeof(valueBuf), "%d / %d / %d",
        global.firstPositions, global.secondPositions, global.thirdPositions);
    addGlobalRow("Podiums (1/2/3)", valueBuf);

    snprintf(valueBuf, sizeof(valueBuf), "%d", global.fastestLapCount);
    addGlobalRow("Fastest laps", valueBuf);

    int globalPenalties = stats.getGlobalTotalPenalties();
    snprintf(valueBuf, sizeof(valueBuf), "%d", globalPenalties);
    addGlobalRow("Penalties", valueBuf);

    int64_t globalPenTimeMs = stats.getGlobalTotalPenaltyTimeMs();
    if (globalPenTimeMs > 0) {
        snprintf(valueBuf, sizeof(valueBuf), "%llds", (long long)(globalPenTimeMs + 500) / 1000);
    } else {
        snprintf(valueBuf, sizeof(valueBuf), "--");
    }
    addGlobalRow("Pen. time", valueBuf);

    int globalLaps = stats.getGlobalTotalLaps();
    snprintf(valueBuf, sizeof(valueBuf), "%d", globalLaps);
    addGlobalRow("Laps", valueBuf);

    int64_t globalTimeMs = stats.getGlobalTotalTimeMs();
    PluginUtils::formatDuration(globalTimeMs, valueBuf, sizeof(valueBuf));
    addGlobalRow("Ride time", valueBuf);

    double totalDist = stats.getTotalOdometer();
    char distBuf[32];
    PluginUtils::formatDistance(totalDist, distBuf, sizeof(distBuf));
    addGlobalRow("Distance", distBuf);

    int globalCrashes = stats.getGlobalTotalCrashes();
    snprintf(valueBuf, sizeof(valueBuf), "%d", globalCrashes);
    addGlobalRow("Crashes", valueBuf);

    int globalShifts = stats.getGlobalTotalGearShifts();
    snprintf(valueBuf, sizeof(valueBuf), "%d", globalShifts);
    addGlobalRow("Shifts", valueBuf);

    snprintf(valueBuf, sizeof(valueBuf), "%d", global.breakoutHighScore);
    addGlobalRow("Breakout score", valueBuf);

    // Footer
    ctx.addSpacing(0.5f);
    ctx.parent->addString("Your stats are saved to mxbmrp3_stats.json", ctx.labelX, ctx.currentY,
        PluginConstants::Justify::LEFT, normalFont, mutedColor, ctx.fontSize * 0.9f);

    return hud;
}
