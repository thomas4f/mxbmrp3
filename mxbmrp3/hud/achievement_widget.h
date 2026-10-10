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
// TWO FEEDS, ONE CARD. The same card carries the system toasts
// (core/system_messages.h): "Standings hidden", "Race profile", "Settings not
// saved". A system toast takes the card from a showing achievement, which goes
// back to the front of its queue and is shown whole afterwards -- feedback on a
// key press is only useful while the press is fresh, an achievement keeps.
// Each feed keeps its own switch: achievements off still shows system toasts,
// and Appearance > Messages off still shows achievements.
//
// IDLE COST: two deque-empty checks per frame while nothing is queued and
// nothing is showing. The toast's clock starts when it is TAKEN here (in an
// update(), i.e. a Draw), never when it was queued -- the load-time summary is
// queued in the menus, where no Draw arrives, and would otherwise expire unseen.
// ============================================================================
#pragma once

#include <chrono>

#include "base_hud.h"
#include "../core/achievement_manager.h"
#include "../core/system_messages.h"

class AchievementWidget : public BaseHud {
public:
    AchievementWidget();
    virtual ~AchievementWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // A toast is on screen (for the tests and the settings tab's preview state).
    bool isShowing() const { return m_showing; }
    // A system toast is on screen (for the tests).
    bool isShowingSystem() const { return m_showing && m_source == Source::System; }
    // The card showing is the hide-all hotkey's own message, which HudManager
    // lets through that hotkey (HudManager::isHeldBack).
    bool isShowingThroughHideAll() const {
        return m_showing && m_source == Source::System && m_system.throughHideAll;
    }
#ifdef MXBMRP3_TEST_BUILD
    // The system toast on the card, valid while isShowingSystem().
    const SystemMessages::Toast& testSystemToast() const { return m_system; }
#endif

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    enum class Source : uint8_t { Achievement, System };

    // Start showing a system toast (taken, or replacing the one showing).
    void showSystem(const std::chrono::steady_clock::time_point& now);

    bool m_showing = false;
    Source m_source = Source::Achievement;
    AchievementManager::Toast m_toast;       // valid while showing an achievement
    SystemMessages::Toast m_system;          // valid while showing a system toast
    std::chrono::steady_clock::time_point m_shownAt{};
};
