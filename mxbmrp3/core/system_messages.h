// ============================================================================
// core/system_messages.h
// What the plugin says to the player outside the settings menu, in two shapes:
//
//   TOASTS -- short confirmations that expire on their own ("Standings hidden",
//   "Race profile", "Settings not saved"). Drawn by AchievementWidget, on the
//   same card and in the same corner as an achievement, which is why the queue
//   is a sibling of AchievementManager's rather than a new widget: one card,
//   two feeds.
//
//   STARTUP MESSAGES -- the ones that wait for the player: a broken install,
//   an update available, "Updated to 1.33" and the welcome on a fresh install.
//   Shown by the Dash as a notice on its card (DashHud::followNotices), even
//   switched off for all but the welcome; this class only decides WHICH are
//   owed and remembers which were answered, in the [Messages] INI section.
//
// THE RULE FOR A TOAST: post one only when the result is not already visible.
// A HUD switched ON by hotkey appears, which is its own confirmation; one
// switched OFF just vanishes, which is the case that leaves a player wondering
// what they pressed. The same reasoning keeps the web-overlay hotkeys out: their
// result shows on the overlay.
//
// COST: every post is an event (a key press, a profile switch, a save), never a
// frame. The widget's idle poll is one empty() check per frame. Nothing here
// allocates after the deque's first growth (bounded at MAX_QUEUED).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>

class SystemMessages {
public:
    static SystemMessages& getInstance();

    // ---- toasts ---------------------------------------------------------------
    enum class Severity : uint8_t { Info, Warning };

    // Plain char arrays, like AchievementManager::Toast, so the widget copies
    // nothing on its per-frame poll.
    struct Toast {
        char title[48] = {};
        char detail[64] = {};
        char icon[32] = {};          // an icon sprite name; empty = no icon column
        int key = 0;                 // dedup key: a newer post with the same key replaces this one
        int tab = -1;                // SettingsHud::Tab the click opens; -1 = the click only closes
        int durationMs = INFO_DURATION_MS;
        Severity severity = Severity::Info;
        // Drawn through the hide-all-HUDs hotkey: the one message about that
        // hotkey itself, which would otherwise be hidden by what it announces.
        bool throughHideAll = false;
    };

    // Keys. A HUD toggle uses HOTKEY_BASE + its HotkeyAction value, so the same
    // key pressed twice replaces its own card instead of queueing a second.
    enum Key : int {
        KEY_NONE = 0,
        KEY_PROFILE_SWITCH,
        KEY_SETTINGS_RELOADED,
        KEY_SAVE_FAILED,
        KEY_PROFILE_COPIED,
        KEY_TAB_RESET,
        KEY_ALL_HIDDEN_AT_START,
        KEY_PROFILER_REPORT,
        KEY_STATS_SAVE_FAILED,
        KEY_STATS_UNREADABLE,
        KEY_WEB_OVERLAY_FAILED,
        KEY_HOTKEY_BASE = 1000,
    };

    static constexpr int INFO_DURATION_MS = 4000;
    static constexpr int WARNING_DURATION_MS = 7000;

    // Queue a toast. A queued toast with the same key is replaced in place, and
    // a SHOWING one with the same key is replaced by the widget on its next
    // update (see takeReplacement). Off (the Appearance "Messages" row) drops
    // the post, like the achievement toggle does: nothing is held for later.
    void post(const Toast& toast);
    // Withdraw a toast that no longer applies: a HUD switched back on by the
    // same key before its "hidden" card ran out. Drops the queued one and asks
    // the widget to end a showing one (consumeCancel).
    void cancel(int key);

    bool hasPending() const { return !m_toasts.empty(); }
    // The next toast is one the hide-all hotkey must not hide.
    bool nextIsThroughHideAll() const { return !m_toasts.empty() && m_toasts.front().throughHideAll; }
    // Pop the oldest queued toast into `out`; false when empty.
    bool take(Toast& out);
    // Pop a queued toast with `key` into `out` (the card showing that key is
    // replaced by it); false when none is queued.
    bool takeReplacement(int key, Toast& out);
    // True once per cancel() of `key`: the widget ends the card showing it.
    bool consumeCancel(int key);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on);

    // A folder as a toast can show it: from "Documents\" on when the path has
    // one (the rest is the player's user folder, and too long for the card), else
    // its last three folders; leading folders are dropped until it fits `cap`.
    // Never cut mid-name. A trailing separator is dropped.
    static void formatFolder(const char* path, char* out, size_t cap);

    // Observability for the headless tests: every toast ever posted this run,
    // and the last one, whether or not it was shown.
    uint32_t posted() const { return m_posted; }
    const Toast& lastPosted() const { return m_last; }

    // ---- startup messages (the Dash) ------------------------------------
    enum class Startup : uint8_t { None, Update, Updated, Welcome, BrokenInstall };

    // Called once, after the FIRST settings load (never on RELOAD_CONFIG).
    //   freshInstall  there was no settings file
    //   currentLine   the running "MAJOR.MINOR" (WhatsNew::currentLine)
    //
    // NO CRASH MESSAGE, deliberately: most dumps come from the game crashing,
    // and any notice about one shown as the plugin loads reads as the plugin
    // owning it. The crash handler still saves its report for support to ask for.
    void onStartup(bool freshInstall, const char* currentLine);
    // The startup update check found a release the player has not skipped. From
    // the UpdateChecker worker, hence the atomic.
    void offerUpdate() { m_owesUpdate = true; }

    // The install is missing its fonts or icons (HudManager::buildInstallWarning):
    // `what` names what, a string literal. Said all session, never answered, so
    // not among pendingStartup()'s; the Dash puts it before them, and before any
    // notice.
    void setInstallProblem(const char* what) { m_installProblem = what; }
    const char* installProblem() const { return m_installProblem; }

    // The highest-priority answerable message still owed this session: the
    // update, then "Updated to", then the welcome.
    Startup pendingStartup() const;
    // The release line "Updated to" names: the one onStartup was given.
    const char* updatedLine() const { return m_currentLine.c_str(); }
    // The player answered `s` (a Dash step, or the settings menu opened):
    // recorded for good. The update's answer is only for this session; the
    // caller skips the version when that is what the answer meant.
    void resolveStartup(Startup s);
    // `s` timed out unanswered. "Updated to" is told once, so it is recorded
    // anyway; the welcome and the update come back at the next launch.
    void expireStartup(Startup s) {
        if (s == Startup::Welcome) m_owesWelcome = false;
        else resolveStartup(s);
    }
    // The settings menu opened: where every one of them points, so all are
    // answered.
    void onSettingsOpened();

    // ---- [Messages] persistence ---------------------------------------------
    // lastLine: the release line whose "Updated to" was last told. While one is
    // still owed this keeps the OLD line, so a session closed before the message
    // drew owes it again; otherwise the writer stores the running line. An
    // absent key on an existing install (one older than this feature) reads as
    // "never told".
    std::string lineToStore(const char* currentLine) const {
        if (!m_owesUpdated) return currentLine ? currentLine : "";
        return m_lastLine.empty() ? std::string("-") : m_lastLine;
    }
    void setStoredLine(const std::string& line) { m_lastLine = line; }
    // 1 = done, 0 = still owed. An absent key (an install older than this
    // feature) reads as done: the welcome is for fresh installs only.
    int welcomeState() const { return m_welcome == Welcome::Owed ? 0 : 1; }
    void setWelcomeState(int v) { m_welcome = v == 0 ? Welcome::Owed : Welcome::Done; }

#if defined(MXBMRP3_TEST_BUILD)
    void testReset();
#endif

private:
    SystemMessages() = default;
    ~SystemMessages() = default;
    SystemMessages(const SystemMessages&) = delete;
    SystemMessages& operator=(const SystemMessages&) = delete;

    static constexpr size_t MAX_QUEUED = 8;

    std::deque<Toast> m_toasts;
    Toast m_last;
    uint32_t m_posted = 0;
    int m_cancelledKey = KEY_NONE;
    bool m_enabled = true;

    enum class Welcome : uint8_t { Unknown, Owed, Done };
    Welcome m_welcome = Welcome::Unknown;
    std::string m_lastLine;
    std::string m_currentLine;       // the running line, from onStartup

    const char* m_installProblem = nullptr;
    std::atomic<bool> m_owesUpdate{false};
    bool m_owesUpdated = false;
    bool m_owesWelcome = false;
};
