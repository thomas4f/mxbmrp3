// ============================================================================
// core/system_messages.cpp
// See the header.
// ============================================================================
#include "system_messages.h"

#include <cstring>

SystemMessages& SystemMessages::getInstance() {
    static SystemMessages instance;
    return instance;
}

void SystemMessages::post(const Toast& toast) {
    ++m_posted;
    m_last = toast;
    if (!m_enabled) return;   // off: nothing is held for later
    // A fresh post of a key outlives an earlier cancel() of it: that cancel was
    // for the card before this one.
    if (toast.key == m_cancelledKey) m_cancelledKey = KEY_NONE;
    // The same key already queued: replace it in place, so mashing a key
    // leaves one card with the latest text rather than a queue of stale ones.
    if (toast.key != KEY_NONE) {
        for (Toast& t : m_toasts) {
            if (t.key == toast.key) { t = toast; return; }
        }
    }
    if (m_toasts.size() >= MAX_QUEUED) m_toasts.pop_front();
    // The hide-all hotkey's own message goes FIRST: it is the only toast the
    // widget may take while that hotkey hides everything, and it can only take
    // the front one.
    if (toast.throughHideAll) m_toasts.push_front(toast);
    else m_toasts.push_back(toast);
}

void SystemMessages::cancel(int key) {
    if (key == KEY_NONE) return;
    for (auto it = m_toasts.begin(); it != m_toasts.end(); ) {
        it = (it->key == key) ? m_toasts.erase(it) : it + 1;
    }
    m_cancelledKey = key;
}

bool SystemMessages::take(Toast& out) {
    if (m_toasts.empty()) return false;
    out = m_toasts.front();
    m_toasts.pop_front();
    return true;
}

bool SystemMessages::takeReplacement(int key, Toast& out) {
    if (key == KEY_NONE) return false;
    for (auto it = m_toasts.begin(); it != m_toasts.end(); ++it) {
        if (it->key == key) {
            out = *it;
            m_toasts.erase(it);
            return true;
        }
    }
    return false;
}

bool SystemMessages::consumeCancel(int key) {
    if (key == KEY_NONE || m_cancelledKey != key) return false;
    m_cancelledKey = KEY_NONE;
    return true;
}

void SystemMessages::setEnabled(bool on) {
    m_enabled = on;
    if (!on) m_toasts.clear();
}

// ---- startup popups --------------------------------------------------------

void SystemMessages::onStartup(bool freshInstall, const char* currentLine) {
    const char* line = currentLine ? currentLine : "";

    // WELCOME: a fresh install owes it; an existing file that never had the key
    // predates this feature, and its player knows where settings are.
    if (freshInstall) m_welcome = Welcome::Owed;
    else if (m_welcome == Welcome::Unknown) m_welcome = Welcome::Done;
    m_owesWelcome = m_welcome == Welcome::Owed;

    // UPDATED: a different line than the one last told, on an install that is
    // not new (a fresh install gets the welcome instead).
    m_owesUpdated = false;
    if (freshInstall) m_lastLine = line;
    else if (m_lastLine != line) m_owesUpdated = true;

    m_currentLine = line;
}

SystemMessages::Popup SystemMessages::pendingPopup() const {
    if (m_owesUpdated) return Popup::Updated;
    if (m_owesWelcome) return Popup::Welcome;
    return Popup::None;
}

void SystemMessages::resolvePopup(Popup p) {
    switch (p) {
        case Popup::Updated:
            m_owesUpdated = false;
            m_lastLine = m_currentLine;
            break;
        case Popup::Welcome:
            m_owesWelcome = false;
            m_welcome = Welcome::Done;
            break;
        case Popup::None:
            break;
    }
}

#if defined(MXBMRP3_TEST_BUILD)
void SystemMessages::testReset() {
    m_toasts.clear();
    m_last = Toast{};
    m_posted = 0;
    m_cancelledKey = KEY_NONE;
    m_enabled = true;
    m_welcome = Welcome::Unknown;
    m_lastLine.clear();
    m_currentLine.clear();
    m_owesUpdated = m_owesWelcome = false;
}
#endif
