// ============================================================================
// mxbmrp3/hud/standings_hud_animation.cpp
// Position animation (row-slide offsets, fades, animation state)
// (the slide itself is RowSlide, row_slide.h, shared with the Standings deck)
// ============================================================================

#include "standings_hud.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "digit_roll.h"
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
    return m_slide.offset(raceNum, m_slideNowUs) * lineHeight;
}

float StandingsHud::getSlideFade(int raceNum) const {
    return m_slide.fade(raceNum, m_slideNowUs);
}

void StandingsHud::updateAnimationState() {
    if (UiConfig::getInstance().getMotion() == Motion::Level::OFF) {
        m_slide.forget();
        return;
    }
    // Only the riders' rows: a placeholder has no rider to slide.
    m_slide.begin();
    for (int i = 0; i < static_cast<int>(m_displayEntries.size()); ++i) {
        const auto& entry = m_displayEntries[i];
        if (!entry.isPlaceholder && entry.raceNum >= 0) m_slide.add(entry.raceNum, entry.position, i);
    }
    // Finished slides are dropped in update(), every frame; this runs on a
    // data change only.
    m_slide.commit(static_cast<int>(m_displayEntries.size()) - 1, DigitRoll::nowUs());
}

bool StandingsHud::hasActiveAnimations() const {
    return m_slide.active();
}
