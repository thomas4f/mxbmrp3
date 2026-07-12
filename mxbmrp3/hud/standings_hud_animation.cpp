// ============================================================================
// mxbmrp3/hud/standings_hud_animation.cpp
// Position animation (row-slide offsets, fades, animation state)
// (extracted verbatim from standings_hud.cpp; no behavior change)
// ============================================================================

#include "standings_hud.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/director_manager.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace PluginConstants;

// ============================================================================
// Position Animation
// ============================================================================

float StandingsHud::getAnimatedRowOffset(int raceNum, float lineHeight) const {
    auto it = m_activeAnimations.find(raceNum);
    if (it == m_activeAnimations.end()) return 0.0f;

    const auto& anim = it->second;
    float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - anim.startTime).count();
    float t = elapsedMs / m_animationDurationMs;

    if (t >= 1.0f) return 0.0f;  // Animation complete

    float progress = easeOutCubic(t);
    float slotDelta = static_cast<float>(anim.fromSlot - anim.toSlot);  // How many slots to travel
    float remaining = slotDelta * (1.0f - progress);  // Remaining offset (shrinks to 0)
    return remaining * lineHeight;
}

float StandingsHud::getSlideFade(int raceNum) const {
    auto it = m_activeAnimations.find(raceNum);
    if (it == m_activeAnimations.end()) return 0.0f;
    float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - it->second.startTime).count();
    float t = elapsedMs / m_animationDurationMs;
    if (t >= 1.0f) return 0.0f;
    return 1.0f - t;
}

void StandingsHud::updateAnimationState() {
    if (m_animationMode == AnimationMode::OFF) {
        m_previousPositions.clear();
        m_previousSlots.clear();
        m_activeAnimations.clear();
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Build current maps: raceNum -> race position, raceNum -> display slot index
    std::unordered_map<int, int> currentPositions;
    std::unordered_map<int, int> currentSlots;
    for (int i = 0; i < static_cast<int>(m_displayEntries.size()); ++i) {
        const auto& entry = m_displayEntries[i];
        if (!entry.isPlaceholder && entry.raceNum >= 0) {
            currentPositions[entry.raceNum] = entry.position;
            currentSlots[entry.raceNum] = i;
        }
    }

    // Detect race position changes (not display slot changes from window scrolling)
    int maxSlot = static_cast<int>(m_displayEntries.size()) - 1;
    for (const auto& [raceNum, currentPos] : currentPositions) {
        auto prevIt = m_previousPositions.find(raceNum);
        if (prevIt != m_previousPositions.end() && prevIt->second != currentPos) {
            // Only animate riders that were also visible in the previous frame
            auto prevSlotIt = m_previousSlots.find(raceNum);
            if (prevSlotIt == m_previousSlots.end()) continue;

            auto slotIt = currentSlots.find(raceNum);
            if (slotIt != currentSlots.end()) {
                int currentSlot = slotIt->second;
                // Estimate the previous slot from the race-position delta instead of
                // reading m_previousSlots directly. m_previousSlots tracks the actual
                // slot occupied last frame, but the visible window can scroll
                // independently (DNS filter changes, top-N pinning, spectator switch),
                // and using the raw previous slot would inflate the slide distance
                // when only the window moved. Posing the delta in race-position space
                // produces the correct visual move for pure overtakes, and degrades
                // gracefully when both an overtake and a window shift happen in the
                // same frame.
                int posDelta = prevIt->second - currentPos;  // positive = moved up
                int estimatedPrevSlot = currentSlot + posDelta;
                // Clamp to visible range so animations never start from far off-screen
                estimatedPrevSlot = std::max(-1, std::min(estimatedPrevSlot, maxSlot + 1));
                m_activeAnimations[raceNum] = { estimatedPrevSlot, currentSlot, now };
            }
        }
    }

    // Note: cleanup of finished animations happens in update(), not here.
    // This method only runs on data change; cleanup must run every frame.

    // Update previous positions and slots for next comparison
    m_previousPositions = std::move(currentPositions);
    m_previousSlots = std::move(currentSlots);
}

bool StandingsHud::hasActiveAnimations() const {
    return !m_activeAnimations.empty();
}
