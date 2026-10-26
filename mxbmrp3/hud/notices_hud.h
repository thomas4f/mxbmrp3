// ============================================================================
// hud/notices_hud.h
// Notices HUD - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#pragma once

#include "base_hud.h"
#include "notice_tracker.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class NoticesHud : public BaseHud {
public:
    // Notice visibility flags (bitfield)
    enum NoticeFlags : uint32_t {
        NOTICE_WRONG_WAY       = 1 << 0,
        NOTICE_BLUE_FLAG       = 1 << 1,
        NOTICE_LAST_LAP        = 1 << 2,
        NOTICE_FINISHED        = 1 << 3,
        NOTICE_ALLTIME_PB      = 1 << 4,
        NOTICE_FASTEST_LAP     = 1 << 5,
        NOTICE_SESSION_PB      = 1 << 6,
        NOTICE_DEFAULT_SETUP   = 1 << 7,
        NOTICE_HAZARD_STATIONARY = 1 << 8,
        NOTICE_HAZARD_WRONG_WAY  = 1 << 9,
        NOTICE_OVERTIME          = 1 << 10,
        // bit 11 was NOTICE_SEGMENT (removed; segment-timer feedback is always on)
        NOTICE_LAPPING           = 1 << 12,
        // A new track on Now playing (MediaManager); fires only with the
        // integration on, which is off by default.
        NOTICE_TRACK_CHANGED     = 1 << 13,
        NOTICE_DEFAULT     = NOTICE_WRONG_WAY | NOTICE_LAST_LAP | NOTICE_FINISHED
                           | NOTICE_ALLTIME_PB | NOTICE_FASTEST_LAP | NOTICE_SESSION_PB
                           | NOTICE_DEFAULT_SETUP | NOTICE_HAZARD_STATIONARY | NOTICE_HAZARD_WRONG_WAY
                           | NOTICE_LAPPING | NOTICE_TRACK_CHANGED
    };

    // Timed notice display duration bounds (covers PB notices)
    static constexpr int MIN_NOTICE_DURATION_MS = 1000;
    static constexpr int DEFAULT_NOTICE_DURATION_MS = 5000;
    static constexpr int MAX_NOTICE_DURATION_MS = 30000;
    static constexpr int DURATION_STEP_MS = 1000;

    NoticesHud();
    virtual ~NoticesHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-notices"; }
    void resetToDefaults();

    // The Notices tab's settings, which the shared NoticeTracker steps with.
    uint32_t getEnabledNotices() const { return m_enabledNotices; }
    int getNoticeDurationMs() const { return m_noticeDurationMs; }
    // A notice's slab in this HUD's colours: the fill whose colour is the message
    // (still to take the slab's opacity) and the caption ink on it at `opacity`.
    // The Dash's Notices page draws its slab from this, so the two never disagree.
    void slabColors(NoticeTracker::Kind kind, float opacity, unsigned long& fill, unsigned long& ink) const;
    // The notices that call for action now (wrong way, hazard, rider oncoming, blue
    // flag) glow behind their slab (BaseHud::addGlow), here and on the Dash; the
    // informational ones (PBs, lapper ahead, last lap...) do not. Reach is a share
    // of the slab's height.
    static bool glows(NoticeTracker::Kind kind);
    static constexpr float GLOW_SPREAD = 0.6f;
    static constexpr float GLOW_OPACITY = 0.4f;
    static float glowOpacity(NoticeTracker::Kind kind);

    // Allow settings system to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // One notice background, themed when the theme has button slices. Uniform
    // quad count across every notice branch -- the layout fast path rewrites
    // this span by index.
    void addNoticeBackground(float x, float y, float w, float h, unsigned long color);

    // The NoticeTracker revision last rendered: a rebuild runs only when the
    // tracker's notice changes. The state machine itself is notice_tracker.h's,
    // shared with the Dash.
    uint32_t m_renderedRevision = 0;

    // Settings
    uint32_t m_enabledNotices = NOTICE_DEFAULT;          // Bitfield of enabled notices
    int m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS; // How long timed notices display (ms)
};
