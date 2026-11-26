// ============================================================================
// hud/toast_feed.h
// The toast up right now: an achievement earned or one of the plugin's own
// messages (core/system_messages.h). This is the state machine that used to
// live in AchievementWidget, moved out so every panel that shows toasts reads
// the SAME one: the Achievement widget's card, and the Notice deck, which
// shows race notices and toasts through one queue.
//
// WHY ONE OWNER. A toast is TAKEN from its queue when shown, and its clock
// starts then. Two panels each taking their own would split the queue between
// them. So HudManager steps this once per frame, right after NoticeTracker
// (whose notice can hold a toast back, below), before any HUD updates.
//
// THE RULES, as the widget had them:
//  - A system toast takes the card from a showing achievement, which goes
//    back to the front of its queue and is shown whole afterwards: feedback on
//    a key press is only useful while the press is fresh, an achievement keeps.
//  - The same key again replaces the showing message in place and restarts it.
//  - A toast is taken only while some panel can show it, and the hide-all
//    hotkey holds them back, except that hotkey's own message.
//  - The clock starts when a toast is taken here, never when it was queued.
//
// HELD, NOT CONSUMED. While every panel watching is showing something else
// over the toast (the Notice deck under a race notice that outranks toasts),
// its clock stops and resumes where it stopped (NoticePriority::stepTimer, the
// rule the PB notices use), so it is still shown for its whole duration.
//
// IDLE COST: two deque-empty checks per frame while nothing is queued and
// nothing is showing.
// ============================================================================
#pragma once

#include <cstdint>

#include "notice_priority.h"
#include "../core/achievement_manager.h"
#include "../core/system_messages.h"

class ToastFeed {
public:
    enum class Source : uint8_t { None, Achievement, System };

    static ToastFeed& getInstance();

    // Once per frame, before any HUD reads it.
    //  watched  some panel that shows toasts is on a surface
    //  hudsOn   the hide-all hotkey is not active
    //  masked   every watching panel is showing something else over the toast
    void step(bool watched, bool hudsOn, bool masked);

    bool showing() const { return m_source != Source::None; }
    Source source() const { return m_source; }
    // Valid while source() says so.
    const AchievementManager::Toast& achievement() const { return m_achievement; }
    const SystemMessages::Toast& system() const { return m_system; }
    // The toast up is the hide-all hotkey's own message, drawn through it.
    bool isThroughHideAll() const { return m_source == Source::System && m_system.throughHideAll; }
    // Toasts queued behind the one showing, both feeds.
    int waiting() const;
    // Bumped whenever the toast up changes, so a renderer rebuilds only then.
    uint32_t revision() const { return m_revision; }

    // The toast was clicked: open the menu where it points (an achievement's
    // page, or the tab a message names) and end it.
    void click();

    // Back to nothing showing (the settings load that switches a feed off
    // clears its queue; this drops a taken toast with it).
    void reset();

private:
    ToastFeed() = default;
    void show(Source source, long long nowMs);
    void end();

    Source m_source = Source::None;
    AchievementManager::Toast m_achievement;
    SystemMessages::Toast m_system;
    long long m_shownMs = 0;              // when it was taken (ms, Motion clock)
    NoticePriority::Timer m_timer;
    uint32_t m_revision = 0;
};
