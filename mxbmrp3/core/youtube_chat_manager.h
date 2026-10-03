// ============================================================================
// core/youtube_chat_manager.h
// Read-only YouTube live chat for the stream chat HUD (StreamChatHud), beside
// TwitchChatManager and on the same threading contract.
//
// One worker thread resolves the configured channel to its current live stream
// (GET its /live page), then polls the chat (POST get_live_chat) at the delay
// YouTube asks for. core/youtube_chat.h decides what every response means; this
// file only does the requests and the waiting. Messages reach the game thread
// through a small bounded queue, drained only when an atomic flag says there
// is something in it.
//
// UNOFFICIAL INTERFACE. This is the chat popout's own web interface, not the
// Data API (see youtube_chat.h for why). When a response is not in the shape
// this build knows, the status becomes UNAVAILABLE ("Unavailable") and
// the worker re-checks rarely; nothing here can reach Twitch.
//
// WHEN IT CONNECTS. While enabled ([YouTube] enabled, the Stream Chat tab's YouTube
// Status toggle) AND a channel is set, independent of the HUD's visibility --
// the same rule as Twitch. The worker is started lazily by tick() on the first
// Draw that wants it, never during plugin initialize().
//
// CANCELLATION. The requests are synchronous WinHTTP calls; a channel change,
// switching off or shutdown closes the one in flight from the calling thread
// (the documented way to abort synchronous WinHTTP). m_hActive is that handle,
// guarded by m_mutex so the worker and the canceller never both close it. Waits
// between polls are condition-variable waits, so they end at once too.
// ============================================================================
#pragma once

#include "thread_safety.h"
#include "chat_event.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <vector>

// Declared in youtube_chat.h; opaque here so this header stays free of the JSON parser.
namespace YouTubeChat { enum class SessionEnd : uint8_t; }

class YouTubeChatManager {
public:
    static YouTubeChatManager& getInstance();

    enum class Status : uint8_t {
        OFF = 0,       // switched off, or no channel set
        CONNECTING,    // fetching the live page / the first poll
        CONNECTED,     // polling the chat
        NOT_LIVE,      // the channel exists but is not live; re-checked every YouTubeChat::NOT_LIVE_RECHECK_S
        NOT_FOUND,     // YouTube has no such channel or video
        RETRYING,      // a request failed (network, 429, 5xx); waiting out the backoff
        UNAVAILABLE,   // a response this build does not recognise
    };

    // GAME THREAD. Channel as typed or pasted; normalized with
    // YouTubeChat::normalizeChannel. A change reconnects and queues a ClearAll
    // so the HUD drops the previous channel's YouTube lines.
    void setChannel(const std::string& input);
    const std::string& getChannel() const { return m_channelGame; }

    // GAME THREAD. The master switch (default OFF). Persisted as [YouTube] enabled.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabledGame; }

    // GAME THREAD, every frame (from StreamChatHud::update): starts the worker
    // when enabled with a channel. One bool test and one atomic load when running.
    void tick();

    Status getStatus() const;

    // GAME THREAD. Move every queued event into `out` (appended). Returns false
    // without locking when nothing is queued.
    bool drain(std::vector<Chat::Event>& out);

    // Orchestrated shutdown (PluginManager::shutdown): cancel, join.
    void shutdown();

#ifdef MXBMRP3_TEST_BUILD
    // Test-only: feed one get_live_chat response body through the same parse-
    // and-queue path the worker uses (minus the network). Returns the
    // YouTubeChat::PollResult; an Unrecognized body sets UNAVAILABLE exactly as
    // a real poll would.
    int testInjectPoll(const std::string& body);
    // Test-only: make getStatus() report `status` (-1 = the real one). While
    // forced, no worker is started.
    void testForceStatus(int status) { m_testStatus.store(status); }
#endif

    static constexpr size_t MAX_QUEUED_EVENTS = 256;
    static constexpr long long RESTART_INTERVAL_MS = 5000;

private:
    YouTubeChatManager() = default;
    ~YouTubeChatManager();
    YouTubeChatManager(const YouTubeChatManager&) = delete;
    YouTubeChatManager& operator=(const YouTubeChatManager&) = delete;

    void ensureWorker();                      // game thread
    void cancelActive();                      // any thread
    void workerMain();
    // `connected`: whether the chat was read at least once (RecheckPolicy).
    YouTubeChat::SessionEnd runSession(const std::string& channel, uint32_t generation, bool& connected);
    // Parse one poll body and queue its events. Returns the PollResult.
    int handlePoll(const std::string& body, uint32_t generation, std::string& continuation, int& delayMs);
    void pushEvents(std::vector<Chat::Event>&& events, uint32_t generation);
    // Wait `ms`, or less if the config changes or shutdown starts. True when
    // the full wait passed with nothing changed.
    bool waitFor(long long ms, uint32_t generation);
    bool stillCurrent(uint32_t generation);

    static long long nowMs();

    std::string m_channelGame;  // mt-plain: game thread only; the worker reads m_channel under m_mutex
    bool m_enabledGame = false;  // mt-plain: game thread only; the worker reads m_wanted under m_mutex
    long long m_lastRestartMs = 0;  // mt-plain: game thread only (ensureWorker's restart throttle)

    Mutex m_mutex;
    std::condition_variable m_cv;
    std::string m_channel MXB_GUARDED_BY(m_mutex);
    bool m_wanted MXB_GUARDED_BY(m_mutex) = false;  // mirrors m_enabledGame
    uint32_t m_generation MXB_GUARDED_BY(m_mutex) = 0;  // bumped on every config change
    void* m_hActive MXB_GUARDED_BY(m_mutex) = nullptr;  // HINTERNET currently blocking
    std::deque<Chat::Event> m_queue MXB_GUARDED_BY(m_mutex);

    std::atomic<bool> m_hasPending{ false };
    std::atomic<uint8_t> m_status{ static_cast<uint8_t>(Status::OFF) };
#ifdef MXBMRP3_TEST_BUILD
    std::atomic<int> m_testStatus{ -1 };
#endif
    std::atomic<bool> m_run{ false };
    std::atomic<bool> m_workerFinished{ false };
    // joined-by: shutdown() (PluginManager::shutdown, and the initialize() rollback);
    // the destructor only spins-then-detaches (thread_detach_grace.h).
    std::thread m_thread;
};
