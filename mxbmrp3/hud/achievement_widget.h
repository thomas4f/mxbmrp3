// ============================================================================
// hud/achievement_widget.h
// Achievement toast: a card with the entry's marker icon, "Winner - Silver" and
// what that tier meant ("Win 10 races"), shown for a few seconds when a tier is
// earned, then the next queued one. A window onto AchievementManager's toast
// queue, like SpotterWidget is onto the spotter's cue log.
//
// GLOBAL, not per-profile: its geometry is persisted in [Achievements] (see
// settings_manager_global.cpp), the shape the Director's status button uses, so
// a profile switch never moves or hides it and one toggle covers every profile.
//
// IDLE COST: one deque-empty check per frame while nothing is queued and
// nothing is showing. The toast's clock starts when it is TAKEN here (in an
// update(), i.e. a Draw), never when it was queued -- the load-time summary is
// queued in the menus, where no Draw arrives, and would otherwise expire unseen.
// ============================================================================
#pragma once

#include <chrono>

#include "base_hud.h"
#include "../core/achievement_manager.h"

class AchievementWidget : public BaseHud {
public:
    AchievementWidget();
    virtual ~AchievementWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // A toast is on screen (for the tests and the settings tab's preview state).
    bool isShowing() const { return m_showing; }

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    bool m_showing = false;
    AchievementManager::Toast m_toast;
    std::chrono::steady_clock::time_point m_shownAt{};
};
