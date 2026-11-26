// ============================================================================
// hud/toast_feed.cpp
// The toast up right now - see toast_feed.h.
// ============================================================================
#include "toast_feed.h"

#include "digit_roll.h"
#include "settings_hud.h"
#include "../core/hud_manager.h"

namespace {
// The Motion clock, which the tests can set (DigitRoll::nowUs)
long long nowMs() { return DigitRoll::nowUs() / 1000; }
}  // namespace

ToastFeed& ToastFeed::getInstance() {
    static ToastFeed instance;
    return instance;
}

int ToastFeed::waiting() const {
    return static_cast<int>(AchievementManager::getInstance().pendingToasts() +
                            SystemMessages::getInstance().pendingCount());
}

void ToastFeed::show(Source source, long long now) {
    m_source = source;
    m_shownMs = now;
    m_timer = {};
    ++m_revision;
}

void ToastFeed::end() {
    if (m_source == Source::None) return;
    m_source = Source::None;
    m_timer = {};
    ++m_revision;
}

void ToastFeed::reset() {
    end();
}

void ToastFeed::step(bool watched, bool hudsOn, bool masked) {
    AchievementManager& ach = AchievementManager::getInstance();
    SystemMessages& sys = SystemMessages::getInstance();
    const long long now = nowMs();

    // END THE TOAST UP when its time is up, its feed was switched off (a
    // settings load or the toggle -- the queue is cleared behind it, but a taken
    // toast lives here), or, for a system toast, when what it announced was
    // undone (SystemMessages::cancel). Its time stops while a panel shows
    // something over it (held, not consumed: the header).
    if (showing()) {
        const int durationMs = m_source == Source::Achievement ? ach.getToastDurationMs() : m_system.durationMs;
        const NoticePriority::TimerOut out = NoticePriority::stepTimer(
            { true, masked, m_shownMs, m_timer }, now, durationMs);
        m_timer = out.timer;
        const bool off = m_source == Source::Achievement ? !ach.isToastsEnabled() : !sys.isEnabled();
        if (off || (m_source == Source::System && sys.consumeCancel(m_system.key)) || out.consume) end();
    }
    // The same key again replaces the message in place and restarts its clock:
    // a key mashed five times is one card with the latest text.
    if (m_source == Source::System && sys.takeReplacement(m_system.key, m_system)) show(Source::System, now);

    // Take the next toast only when it can actually be seen: some panel on a
    // surface, and the hide-all-HUDs hotkey not active (unless the toast is
    // that hotkey's own message). Taken while hidden, a toast would run its
    // clock unseen and be gone.
    const bool systemDrawable = watched && (hudsOn || sys.nextIsThroughHideAll());
    // A system toast takes the card from an achievement; the achievement goes
    // back to the front of its queue and is shown whole afterwards.
    if (m_source == Source::Achievement && systemDrawable && sys.hasPending()) {
        ach.requeueFront(m_achievement);
        end();
    }
    if (!showing() && systemDrawable && sys.take(m_system)) {
        show(Source::System, now);
    } else if (!showing() && watched && hudsOn && ach.takeToast(m_achievement)) {
        show(Source::Achievement, now);
    }
}

void ToastFeed::click() {
    if (!showing()) return;
    const Source source = m_source;
    end();
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    if (source == Source::Achievement) {
        settings.showAchievementsTab(m_achievement.catalogueIndex);
    } else if (m_system.tab >= 0) {
        settings.showTab(m_system.tab);
    }
}
