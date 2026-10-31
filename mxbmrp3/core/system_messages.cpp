// ============================================================================
// core/system_messages.cpp
// See the header.
// ============================================================================
#include "system_messages.h"

#include <cctype>
#include <cstdio>
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

void SystemMessages::formatFolder(const char* path, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!path) return;
    size_t len = std::strlen(path);
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) --len;

    // Start at "Documents\" when the path has one (case-insensitive, as Windows is).
    static const char kDocs[] = "documents";
    const size_t docsLen = sizeof(kDocs) - 1;
    size_t start = len;   // len = not found yet
    for (size_t i = 0; i + docsLen <= len; ++i) {
        if (i > 0 && path[i - 1] != '\\' && path[i - 1] != '/') continue;
        if (i + docsLen < len && path[i + docsLen] != '\\' && path[i + docsLen] != '/') continue;
        size_t k = 0;
        while (k < docsLen && std::tolower(static_cast<unsigned char>(path[i + k])) == kDocs[k]) ++k;
        if (k == docsLen) { start = i; break; }
    }
    if (start == len) {
        // No Documents folder: the last three folders.
        start = 0;
        int seps = 0;
        for (size_t i = len; i > 0; --i) {
            if (path[i - 1] == '\\' || path[i - 1] == '/') {
                if (++seps == 3) { start = i; break; }
            }
        }
    }
    // Drop leading folders until it fits.
    while (len - start >= cap) {
        size_t next = start;
        while (next < len && path[next] != '\\' && path[next] != '/') ++next;
        if (next >= len) break;   // one name longer than the card: shown cut, the only way left
        start = next + 1;
    }
    std::snprintf(out, cap, "%.*s", static_cast<int>(len - start), path + start);
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

// ---- startup messages --------------------------------------------------------

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

SystemMessages::Startup SystemMessages::pendingStartup() const {
    if (m_owesUpdate) return Startup::Update;
    if (m_owesUpdated) return Startup::Updated;
    if (m_owesWelcome) return Startup::Welcome;
    return Startup::None;
}

void SystemMessages::resolveStartup(Startup s) {
    switch (s) {
        case Startup::Update:
            m_owesUpdate = false;
            break;
        case Startup::Updated:
            m_owesUpdated = false;
            m_lastLine = m_currentLine;
            break;
        case Startup::Welcome:
            m_owesWelcome = false;
            m_welcome = Welcome::Done;
            break;
        case Startup::BrokenInstall:   // until the install is fixed
        case Startup::None:
            break;
    }
}

void SystemMessages::onSettingsOpened() {
    m_owesUpdate = false;
    if (m_owesUpdated) resolveStartup(Startup::Updated);
    if (m_owesWelcome) resolveStartup(Startup::Welcome);
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
    m_installProblem = nullptr;
    m_owesUpdate = false;
    m_owesUpdated = m_owesWelcome = false;
}
#endif
