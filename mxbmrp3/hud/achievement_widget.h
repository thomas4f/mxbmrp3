// ============================================================================
// hud/achievement_widget.h
// Achievement toast: a card with the entry's marker icon, "Winner - Silver" and
// what that tier meant ("Win 10 races"), shown for a few seconds when a tier is
// earned, then the next queued one. A window onto the toast up right now
// (hud/toast_feed.h), which the Notice deck shows too.
//
// GLOBAL, not per-profile: its geometry is persisted in [Achievements] (see
// settings_manager_global.cpp), like the Director's own settings, so
// a profile switch never moves or hides it and one toggle covers every profile.
//
// TWO FEEDS, ONE CARD. The same card carries the system toasts
// (core/system_messages.h): "Standings hidden", "Race profile", "Settings not
// saved". Which toast is up, and the rules between the two feeds, are
// ToastFeed's. Each feed keeps its own switch: achievements off still shows
// system toasts, and Appearance > Messages off still shows achievements.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "toast_feed.h"

class AchievementWidget : public BaseHud {
public:
    AchievementWidget();
    virtual ~AchievementWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // A toast is up (for the tests and the settings tab's preview state).
    bool isShowing() const { return ToastFeed::getInstance().showing(); }
    // A system toast is up (for the tests).
    bool isShowingSystem() const { return ToastFeed::getInstance().source() == ToastFeed::Source::System; }
#ifdef MXBMRP3_TEST_BUILD
    // The system toast on the card, valid while isShowingSystem().
    const SystemMessages::Toast& testSystemToast() const { return ToastFeed::getInstance().system(); }
#endif

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // The ToastFeed revision last rendered: a rebuild runs only when it moves.
    uint32_t m_renderedRevision = 0;
};
