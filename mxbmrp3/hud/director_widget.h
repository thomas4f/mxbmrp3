// ============================================================================
// hud/director_widget.h
// On-screen status button for the auto-director: a camera icon whose tint shows
// the director's state (off / auto / manual / paused).
// Clicking it (cursor-visible) turns the director on or off. A window onto
// the DirectorManager singleton (like RumbleHud is for the rumble system), not a
// data HUD of its own. Shown by default, but clipped unless spectating/replaying.
// ============================================================================
#pragma once

#include "base_hud.h"

class DirectorWidget : public BaseHud {
public:
    DirectorWidget();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;

    // Restore configurable defaults (visibility, position, scale, opacity).
    void resetToDefaults();

    // True on a left-click within the button (cursor-visible only), so HudManager can
    // toggle the director's pause/resume. Mirrors SettingsButtonWidget.
    bool isClicked() const;

protected:
    void rebuildRenderData() override;

private:
    // Last-seen director "reveal bucket" (off / manual / paused / auto), so a meaningful
    // mode change - on/off, gamepad manual takeover or auto-resume, pause/resume - briefly
    // reveals the button even while the mouse is idle. -1 = uninitialised.
    int m_lastRevealBucket = -1;
};
