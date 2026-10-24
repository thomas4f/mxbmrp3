// ============================================================================
// hud/dash_hud_notices.cpp
// The Dash's notice card: what takes it over (a race notice, a startup
// message), how a startup message is answered or runs out, and the Dash +/-
// step that answers or turns. See NOTICE TAKEOVER and STARTUP MESSAGES in
// dash_hud.h.
// ============================================================================
#include "dash_hud.h"

#include "digit_roll.h"
#include "notice_tracker.h"
#include "settings_hud.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/settings_manager.h"
#include "../core/system_messages.h"
#include "../core/update_checker.h"

void DashHud::followNotices() {
    const NoticeTracker& tracker = NoticeTracker::getInstance();
    const bool noticeUp = (m_options & OPT_NOTICE_TAKEOVER) &&
                          tracker.current().kind != NoticeTracker::Kind::None;
    // A startup message holds the card under no notice, each time: a step answers
    // it, so one still up has not been answered yet.
    const bool messageUp = cardMessage() != Startup::None;
    if (m_noticeHold) {
        if (!noticeUp && !messageUp) {
            goTo(firstEnabledFrom(m_returnPage));   // while held, so the strip stays put
            m_noticeHold = false;
        }
    } else if ((noticeUp && tracker.revision() != m_noticeSeen) || messageUp) {
        if (noticeUp) m_noticeSeen = tracker.revision();
        if (m_page != Page::NOTICES) {
            m_returnPage = m_page;
            m_noticeHold = true;
            goTo(Page::NOTICES);
        }
    }
}

void DashHud::followBorrowed() {
    // One load and a switch a frame.
    const SystemMessages& messages = SystemMessages::getInstance();
    const bool borrowed = !isVisibleAnySurface() &&
                          (messages.installProblem() || borrows(messages.pendingStartup()));
    if (borrowed != m_borrowed) {
        m_borrowed = borrowed;
        setDataDirty();
    }
}

bool DashHud::borrows(Startup message) {
    switch (message) {
        case Startup::BrokenInstall:
        case Startup::Update:   // offered only with the update check on
            return true;
        case Startup::Updated:
            return UpdateChecker::getInstance().isEnabled();
        case Startup::Welcome:
        case Startup::None:
        default:
            return false;
    }
}

DashHud::Startup DashHud::cardMessage() const {
    const SystemMessages& messages = SystemMessages::getInstance();
    if (messages.installProblem()) return Startup::BrokenInstall;
    // Borrowed, the card is the message's alone.
    if (m_borrowed) {
        const Startup owed = messages.pendingStartup();
        return borrows(owed) ? owed : Startup::None;
    }
    if ((m_options & OPT_NOTICE_TAKEOVER) &&
        NoticeTracker::getInstance().current().kind != NoticeTracker::Kind::None) {
        return Startup::None;
    }
    return messages.pendingStartup();
}

void DashHud::answerMessage(Startup message) {
    // Said until it is fixed; None when a borrowed step lands as the message ends.
    if (message == Startup::BrokenInstall || message == Startup::None) return;
    SystemMessages::getInstance().resolveStartup(message);
    // What was answered persists with the next save (leave-track or Save).
    SettingsManager::getInstance().markDirty();
    setDataDirty();
}

void DashHud::tickMessage() {
    const Startup message = m_page == Page::NOTICES ? cardMessage() : Startup::None;
    const auto now = std::chrono::steady_clock::now();
    if (message != m_timedMessage) {
        m_timedMessage = message;
        m_messageShownMs = 0.0f;
        m_messageTick = now;
        return;
    }
    if (message == Startup::None || message == Startup::Welcome || message == Startup::BrokenInstall) return;
    float dtMs = std::chrono::duration<float, std::milli>(now - m_messageTick).count();
    m_messageTick = now;
    // A gap between Draws (a menu, a load) is not time on screen.
    if (dtMs > 100.0f) dtMs = 100.0f;
    if (isHeldBack()) return;
    m_messageShownMs += dtMs;
    if (m_messageShownMs >= MESSAGE_SHOWN_MS) {
        SystemMessages::getInstance().expireStartup(message);
        SettingsManager::getInstance().markDirty();
        setDataDirty();
    }
}

void DashHud::followMessageClick() {
    // The card is the way in, as the Version widget's buttons were: the update
    // opens Updates, "Updated to" What's New, the welcome the menu. The menu
    // opening is what answers it (HudManager). Gated like the Toggles strip: on
    // screen, and the pointer showing (none on track with Menu-only cursor).
    // Nothing a broken install can be sent to.
    const Startup message = m_page == Page::NOTICES ? cardMessage() : Startup::None;
    if (message == Startup::None || message == Startup::BrokenInstall || isHeldBack()) return;
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor() || !input.getLeftButton().isClicked()) return;
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    if (!cursor.isValid || !isPointInBounds(cursor.x, cursor.y)) return;
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    if (settings.isVisible()) return;   // vis-gate: the menu's own state
    if (message == Startup::Update) settings.showUpdatesTab();
    else if (message == Startup::Updated) settings.showWhatsNew();
    else settings.show();
}

void DashHud::step(int direction) {
    const Startup message = cardMessage();
    // Switched off and shown for a message, a step only answers it; and nothing
    // takes a broken install's card away.
    if (m_borrowed || message == Startup::BrokenInstall) {
        answerMessage(message);
        return;
    }
    // The first step ever drops the hint for good (FIRST-STEP HINT) and does only
    // that: the page the hint covered comes back, so the key that answers it
    // does not also turn a page the rider never saw.
    const bool hinted = !m_stepTried;
    m_stepTried = true;
    // A step while a startup message shows answers it, held or on the Notices
    // page the rider picked (else that page would bring it back every frame).
    if (message != Startup::None && (m_noticeHold || m_page == Page::NOTICES)) answerMessage(message);
    // Either step while a notice holds the card is the rider taking it back: their
    // own page returns, this notice is done with, and the next one takes over again.
    if (m_noticeHold) {
        if (message == Startup::None) m_noticeSeen = NoticeTracker::getInstance().revision();
        goTo(firstEnabledFrom(m_returnPage));   // while held, so the strip stays put
        m_noticeHold = false;
        return;
    }
    if (hinted) {
        setDataDirty();
        return;
    }
    const int count = CYCLE_PAGES;
    int next = static_cast<int>(m_page);
    for (int i = 0; i < count; ++i) {
        next = ((next + direction) % count + count) % count;
        if (pageEnabled(static_cast<Page>(next))) break;
    }
    const Page target = static_cast<Page>(next);
    if (target == m_page) return;
    goTo(target);
    // Only a page coming in from nothing: one stepped back to mid-fade carries
    // on bringing its content back from where it got to, never dropping it for
    // a name popping up at that alpha.
    if ((m_options & OPT_PAGE_NAMES) && m_inFrom <= 0.0f) {
        m_nameUp = true;
        m_nameStartUs = DigitRoll::nowUs();
    }
}
