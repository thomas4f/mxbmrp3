// ============================================================================
// core/settings_default_positions.cpp
// The v11 migration: the default-off HUDs' default spots (hud/default_slots.h).
//
// A HUD section writes EVERY key, so a changed default position never reaches
// an existing INI on its own: the old default sits in the file and wins.
//
// CONSERVATIVE, like the v5 colour/font unpinning and the v9 row pitch: a stored
// position moves only while it IS the old default -- both of its coordinates,
// in the base section and in each profile. Such a value carries no intent: the
// old writer wrote it, nobody placed the HUD there. A HUD the player dragged
// keeps its spot. The new value is this build's own factory default, so the
// table below is only the OLD spots, frozen as 1.33 shipped them.
//
// The old spots were computed from the pristine layout lattice (cellsX/cellsY at
// construction, before [Advanced] is read), so they are recomputed from a
// pristine LayoutMetrics here, not from the live one. An INI written under an
// older lattice (the row pitch before v9) stores other numbers and is left
// alone, which is the conservative outcome.
//
// The companion window's positions (companionX/Y) are not touched: present only
// where the companion diverged, they were placed there on purpose.
//
// Also the v12 migration, the Spotter's: it moved to low centre and became
// centre-anchored in the same step -- see migrateSpotterPosition() below.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "layout_metrics.h"
#include "../hud/spotter_widget.h"
#include "../diagnostics/logger.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

using namespace Settings;

struct OldSpot { const char* hud; float x, y; };

// A stored float plus `delta`, written back the way the writer formats it. False
// for a missing key or a hand-edited non-number, which is left alone.
bool shiftStored(SettingsManager::HudSettings& section, const char* key, float delta) {
    auto it = section.find(key);
    if (it == section.end()) return false;
    try {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", std::stof(it->second) + delta);
        it->second = buf;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool storedEquals(const SettingsManager::HudSettings& section, const char* key, float want) {
    auto it = section.find(key);
    if (it == section.end()) return false;
    try {
        return std::fabs(std::stof(it->second) - want) < 2.0e-5f;
    } catch (const std::exception&) {
        return false;   // a hand-edited non-number was placed by hand
    }
}

}  // namespace

void SettingsManager::migrateDefaultPositions(int loadedVersion) {
    if (!(loadedVersion > 0 && loadedVersion < 11)) return;

    LayoutMetrics pristine;
    pristine.derive();
    const float cw = pristine.cellW, ch = pristine.cellH;
    // Where each HUD opened until 1.33 (its offsetX/offsetY). Centre-stack members
    // and the Radar store their centre (settings v7/v8), hence the 0.5s.
    const OldSpot spots[] = {
        { "TelemetryHud",     133 * cw, 38 * ch }, { "DeltaTraceHud",  133 * cw, 35 * ch },
        { "RumbleHud",        133 * cw, 32 * ch }, { "PerformanceHud", 133 * cw, 56 * ch },
        { "EventLogHud",      133 * cw, 44 * ch }, { "StreamChatHud",  133 * cw, 59 * ch },
        { "FriendsHud",       133 * cw, 47 * ch }, { "FmxHud",         133 * cw, 50 * ch },
        { "StatsHud",         133 * cw, 53 * ch }, { "RecordsHud",     133 * cw, 41 * ch },
        { "LapLogHud",          1 * cw, 64 * ch }, { "IdealLapHud",     49 * cw, 64 * ch },
        { "SpeedoWidget",     125 * cw, 64 * ch }, { "TachoWidget",    112 * cw, 71 * ch },
        { "GamepadWidget",     68 * cw, 62 * ch }, { "SessionChartsHud", 0.7315f, 1 * ch },
        { "RadarHud",            0.5f,  1 * ch }, { "NoticesHud",        0.5f,   1 * ch },
        { "TimingHud",           0.5f,  1 * ch },
        { "GapBarHud",           0.5f,  1 * ch }, { "PrestigeWidget",    0.5f,   0.05f },
    };

    int moved = 0;
    // One section: both coordinates on the old spot, so both take the new one.
    auto settle = [&moved](HudSettings& section, const HudSettings& factory,
                           const char* keyX, float oldX, const char* keyY, float oldY) {
        if (!storedEquals(section, keyX, oldX)) return;
        if (keyY && !storedEquals(section, keyY, oldY)) return;
        auto fx = factory.find(keyX);
        if (fx == factory.end()) return;
        section[keyX] = fx->second;
        if (keyY) {
            auto fy = factory.find(keyY);
            if (fy != factory.end()) section[keyY] = fy->second;
        }
        ++moved;
    };
    // Every profile, then the base section the save writes back -- the same places
    // the earlier migrations in loadSettings() rewrite. m_hudDefaults is seeded from
    // the factory snapshot, which is never on an old spot, so only a value the
    // file's base section carried can match there.
    auto settleEverywhere = [&](const char* hud, const char* keyX, float oldX,
                                const char* keyY, float oldY) {
        auto factoryIt = m_hudFactoryDefaults.find(hud);
        if (factoryIt == m_hudFactoryDefaults.end()) return;   // not registered on this game
        for (auto& cache : m_profileCache) {
            auto it = cache.find(hud);
            if (it != cache.end()) settle(it->second, factoryIt->second, keyX, oldX, keyY, oldY);
        }
        auto it = m_hudDefaults.find(hud);
        if (it != m_hudDefaults.end()) settle(it->second, factoryIt->second, keyX, oldX, keyY, oldY);
    };

    for (const OldSpot& s : spots)
        settleEverywhere(s.hud, Keys::Base::OFFSET_X, s.x, Keys::Base::OFFSET_Y, s.y);
    // The Map is placed by its top-right anchor, not its offset; only the right
    // edge moved (one cell clear of the Toggles strip), but both must still be on
    // the old corner, like every offset above.
    settleEverywhere("MapHud", "anchorX", 0.994125f, "anchorY", 0.0113039f);

    DEBUG_INFO_F("Settings v%d -> v11: %d HUD position(s) still on the old default moved to the new one",
                 loadedVersion, moved);
}

// THE SPOTTER, files written at version <= 11. Two changes in one step, so one
// version: its card moved from cells 20,24 to low centre (DefaultSlots), and its
// offsetX now means the card's CENTRE, not its left edge, so a card as wide as
// its cue stays centred wherever it is put.
//
// Per section: still on the old spot (both coordinates) -> this build's default,
// like the v11 table. Anywhere else, the player put it there, so the left edge
// becomes a centre by adding half the PREVIEW card's width at that section's
// scale (SpotterWidget::previewCardWidth): the preview they dragged into place
// draws where it did. A real cue is wider or narrower than the preview, and now
// grows about that centre instead of rightward from the edge.
//
// The base section is shifted only where the FILE carried the key: the rest of
// m_hudDefaults is the factory snapshot, already in the new meaning (the same
// gate as the v7/v8 re-anchors in loadSettings()). companionX gets the shift too
// where present (a diverged companion); it never sat on the old default.
void SettingsManager::migrateSpotterPosition(int loadedVersion, bool baseCarriesX,
                                             bool baseCarriesCompanionX) {
    if (!(loadedVersion > 0 && loadedVersion < 12)) return;
    auto factoryIt = m_hudFactoryDefaults.find("SpotterWidget");
    if (factoryIt == m_hudFactoryDefaults.end()) return;
    const HudSettings& factory = factoryIt->second;

    LayoutMetrics pristine;
    pristine.derive();
    const float oldX = 20 * pristine.cellW, oldY = 24 * pristine.cellH;

    int moved = 0, shifted = 0;
    auto settle = [&](HudSettings& section, bool mayShiftX, bool mayShiftCompanionX) {
        float scale = 1.0f;
        auto scaleIt = section.find(Keys::Base::SCALE);
        if (scaleIt != section.end()) {
            try { scale = std::stof(scaleIt->second); }
            catch (const std::exception&) { scale = 1.0f; }
        }
        if (!(scale > 0.0f) || !std::isfinite(scale)) scale = 1.0f;
        const float half = SpotterWidget::previewCardWidth(scale) * 0.5f;

        if (mayShiftCompanionX && shiftStored(section, Keys::Base::COMPANION_X, half)) ++shifted;
        if (!mayShiftX) return;
        if (storedEquals(section, Keys::Base::OFFSET_X, oldX) &&
            storedEquals(section, Keys::Base::OFFSET_Y, oldY)) {
            auto fx = factory.find(Keys::Base::OFFSET_X);
            auto fy = factory.find(Keys::Base::OFFSET_Y);
            if (fx != factory.end() && fy != factory.end()) {
                section[Keys::Base::OFFSET_X] = fx->second;
                section[Keys::Base::OFFSET_Y] = fy->second;
                ++moved;
                return;
            }
        }
        if (shiftStored(section, Keys::Base::OFFSET_X, half)) ++shifted;
    };
    for (auto& cache : m_profileCache) {
        auto it = cache.find("SpotterWidget");
        if (it != cache.end()) settle(it->second, true, true);
    }
    auto it = m_hudDefaults.find("SpotterWidget");
    if (it != m_hudDefaults.end()) settle(it->second, baseCarriesX, baseCarriesCompanionX);

    DEBUG_INFO_F("Settings v%d -> v12: Spotter moved to low centre in %d section(s), "
                 "%d offset(s) re-anchored on the centre", loadedVersion, moved, shifted);
}
