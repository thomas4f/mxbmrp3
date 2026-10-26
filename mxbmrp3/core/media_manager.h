// ============================================================================
// core/media_manager.h
// The "Now playing" integration: reads whatever media player Windows
// is showing in its volume flyout (Spotify, a browser tab, foobar, ...) and
// sends it previous / play-pause / next.
//
// SOURCE. Windows' Global System Media Transport Controls (GSMTC,
// Windows.Media.Control) through C++/WinRT -- the same windowsapp.lib the
// controller-name lookup in xinput_reader.cpp already links, so no DLL ships
// and nothing needs a login. Windows 10 1809+. MSVC-only: mingw ships no WinRT
// headers, so in the headless test build the manager never starts a thread and
// reports UNAVAILABLE.
//
// THREADING. Every GSMTC call is a cross-process async call, so all of them run
// on one worker thread, started on the first enable (after settings load) and
// joined by shutdown() from PluginManager::shutdown. Each call waits at most
// ASYNC_TIMEOUT_MS, so a hung player can never hold up the join. The worker
// polls once a second (or at once when a command is queued), converts the text
// to the in-game font's CP1252 and publishes a Snapshot under m_mutex, bumping
// revision() only when something shown changed. The game thread never formats:
// a consumer compares revision() and copies the snapshot on a change.
//
// PLAYERS. With [General] nowPlayingApps (INI-only, core/media_app_filter.h)
// empty, the worker follows Windows' current session, which moves between
// players (a browser tab starts and it is the current one). Naming players
// there, it follows the first session that matches instead, Windows' current
// one first; each new current player's app id is logged, so the log shows
// what to type.
//
// VOLUME. GSMTC has none. The player's own volume -- its slider in the Windows
// Volume Mixer -- is set through WASAPI (IAudioSessionManager2 ->
// ISimpleAudioVolume) on every audio session whose process matches the GSMTC
// player's app (Spotify.exe, or the part after '!' of a Store app id). A
// browser is one session for all its tabs. The game's own volume is untouched.
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <thread>
#include "thread_safety.h"

class MediaManager {
public:
    enum class State : int {
        OFF,            // integration disabled
        UNAVAILABLE,    // no GSMTC on this system (or this build)
        NO_PLAYER,      // enabled, but no player has a media session
        PAUSED,         // a session exists and is not playing
        PLAYING
    };

    enum class Command : int { NONE, PREVIOUS, TOGGLE_PLAY, NEXT };

    // Everything a consumer draws, pre-formatted on the worker thread. Text is
    // CP1252 (what the .fnt renderer draws); characters it cannot draw are dropped.
    struct Snapshot {
        State state = State::OFF;
        char title[128] = {};
        char artist[128] = {};
        char time[24] = {};        // "1:23 / 3:45", empty when the player reports no timeline
        int positionMs = 0;        // at the moment of the last poll
        int durationMs = 0;
        int volume = -1;           // the player's mixer volume 0..100; -1 = no audio session found
    };

    static MediaManager& getInstance();

    // GAME THREAD. initialize() runs once after settings are loaded and starts
    // the worker if the saved setting is on; shutdown() joins it.
    void initialize();
    void shutdown();

    // GAME THREAD. The General tab toggle and the [General] nowPlaying key.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled.load(); }

    // False in builds without WinRT (the headless test build): the toggle is
    // shown disabled, like Steam on a non-Steam install.
    static bool isSupported();

    // GAME THREAD. [General] nowPlayingApps: the players to follow (comma-
    // separated app-id parts), empty = any. Applied on the worker's next poll.
    void setAppFilter(const char* filter);
    void getAppFilter(char* out, size_t outSize) const;

    // GAME THREAD. Queues a transport command; the worker sends it on its next
    // wake (immediately). A newer command replaces an unsent one.
    void sendCommand(Command command);
    // GAME THREAD. The player's mixer volume, 0..100; the worker applies the
    // latest one at once (a drag sends many, only the newest is applied).
    void setVolume(int percent);

    // Any thread. Bumped by the worker whenever the snapshot changes.
    uint32_t revision() const { return m_revision.load(std::memory_order_acquire); }
    void getSnapshot(Snapshot& out) const;

#if defined(MXBMRP3_TEST_BUILD)
    // core/test_hooks_media.cpp: a player as the worker would publish it, in the
    // build that has no worker, and what the controls queued for it.
    void publishForTest(const Snapshot& next) { publish(next); }
    int pendingCommandForTest() const { return m_pendingCommand.load(); }
    int pendingVolumeForTest() const { return m_pendingVolume.load(); }
#endif

private:
    MediaManager() = default;
    ~MediaManager();
    MediaManager(const MediaManager&) = delete;
    MediaManager& operator=(const MediaManager&) = delete;

    void startWorker();
    void workerLoop();
    void publish(const Snapshot& next);

    static constexpr int POLL_INTERVAL_MS = 1000;
    static constexpr int ASYNC_TIMEOUT_MS = 500;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_workerStop{false};
    std::atomic<bool> m_workerFinished{false};
    // Set only by the static-teardown backstop: the worker then skips releasing
    // its WinRT objects and the apartment, which can need the loader lock the
    // destructing thread holds (see thread_detach_grace.h).
    std::atomic<bool> m_abandonCleanup{false};
    std::atomic<int> m_pendingCommand{static_cast<int>(Command::NONE)};
    std::atomic<int> m_pendingVolume{-1};   // 0..100, -1 = none
    std::atomic<uint32_t> m_revision{0};

    mutable Mutex m_mutex;
    std::condition_variable m_cv;   // wakes the worker for a command or to stop
    Snapshot m_snapshot MXB_GUARDED_BY(m_mutex);
    char m_appFilter[256] MXB_GUARDED_BY(m_mutex) = {};   // MediaAppFilter::MAX_CHARS + 1
    std::thread m_worker;   // joined-by: shutdown() (PluginManager::shutdown)
};
