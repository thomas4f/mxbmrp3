// ============================================================================
// core/twitch_chat_manager.h
// Read-only Twitch chat connection for StreamChatHud.
//
// One worker thread holds an anonymous IRC-over-WebSocket session
// (wss://irc-ws.chat.twitch.tv, NICK justinfanNNNNN, no token) to the configured
// channel, parses every line with core/twitch_irc.h and hands the events the HUD
// acts on -- chat messages and the moderation that removes them -- to the game
// thread through a small bounded queue. The game thread never blocks on the
// network: it drains the queue only when an atomic flag says there is something
// in it.
//
// WHEN IT CONNECTS. While the chat is enabled ([Twitch] enabled, the Stream
// Chat tab's Twitch Status toggle) AND a channel is set -- independent of the HUD's
// visibility, so a streamer can hide the chat and bring it back with the
// conversation already there. The worker is started lazily by tick() on the
// first Draw that wants it, never during plugin initialize().
//
// TRANSPORT. WinHTTP's WebSocket API: TLS, proxy settings and certificate
// validation come from the OS, port 443 passes firewalls, and winhttp is already
// linked (RecordsFetcher/UpdateChecker). Evaluated and rejected: plain IRC on
// 6667 (unencrypted, often blocked) and TLS on 6697 via Schannel (a hand-rolled
// TLS pump for what WinHTTP does in one call).
//
// CANCELLATION. The socket calls are synchronous; a channel change, switching off or
// shutdown cancels the one in flight by closing its handle from the calling
// thread -- the documented way to abort synchronous WinHTTP. m_hActive is the
// handle that is currently blocking, guarded by m_mutex so the worker and the
// canceller never both close it.
// ============================================================================
#pragma once

#include "thread_safety.h"
#include "twitch_irc.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <vector>

class TwitchChatManager {
public:
    static TwitchChatManager& getInstance();

    enum class Status : uint8_t {
        OFF = 0,        // switched off, or no channel set
        CONNECTING,     // opening the socket / TLS / WebSocket upgrade
        JOINING,        // connected, JOIN sent, waiting for ROOMSTATE
        CONNECTED,      // in the channel, messages flowing
        NOT_FOUND,      // JOIN sent long ago and Twitch never confirmed the channel
        RETRYING,       // the last attempt failed; waiting out the backoff
    };

    // GAME THREAD. Channel as typed or pasted; normalized with
    // TwitchIrc::normalizeChannel. A change reconnects and queues a ClearAll so
    // the HUD drops the previous channel's lines.
    void setChannel(const std::string& input);
    const std::string& getChannel() const { return m_channelGame; }

    // GAME THREAD. The chat's master switch (default OFF: a new player has no
    // channel and no reason to connect). Off cancels the session; on lets tick()
    // start it. Persisted as [Twitch] enabled.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabledGame; }

    // GAME THREAD, every frame (from StreamChatHud::update, which runs whether
    // or not the HUD is visible): starts the worker when the chat is enabled
    // and has a channel. One bool test and one atomic load when it is running.
    void tick();

    Status getStatus() const;

    // GAME THREAD. Move every queued event into `out` (appended). Returns false
    // without locking when nothing is queued.
    bool drain(std::vector<Chat::Event>& out);

    // Orchestrated shutdown (PluginManager::shutdown): cancel, join.
    void shutdown();

#ifdef MXBMRP3_TEST_BUILD
    // Test-only: feed one raw IRC line through the same path the socket uses
    // (minus the network): the line is interpreted and its event queued.
    // `stale` stamps the line with the PREVIOUS generation -- a line read just
    // before a channel switch -- which must be dropped.
    void testInjectLine(const std::string& raw, bool stale = false);
    // Test-only: make getStatus() report `status` (-1 = the real one), so the
    // chat panel's status line is testable with no network. While forced, no
    // worker is started, so a test can set a channel and show the HUD offline.
    void testForceStatus(int status) { m_testStatus.store(status); }
#endif

    // How long JOINING may last before the status reads NOT_FOUND. Twitch
    // answers a JOIN to an existing channel with ROOMSTATE within a second; a
    // misspelled name gets silence.
    static constexpr long long JOIN_TIMEOUT_MS = 10000;
    static constexpr size_t MAX_QUEUED_EVENTS = 256;
    static constexpr long long RESTART_INTERVAL_MS = 5000;

private:
    TwitchChatManager() = default;
    ~TwitchChatManager();
    TwitchChatManager(const TwitchChatManager&) = delete;
    TwitchChatManager& operator=(const TwitchChatManager&) = delete;

    void ensureWorker();                      // game thread
    void cancelActive();                      // any thread
    void workerMain();
    // Runs one session to its end. Returns true if the channel was joined.
    bool runSession(const std::string& channel, uint32_t generation);
    // Interpret one line; queue what the HUD needs. Returns the event kind so
    // the session loop can answer PING / act on RECONNECT / JOINED.
    Chat::Event handleLine(const std::string& raw, uint32_t generation);
    void pushEvent(Chat::Event&& ev, uint32_t generation);

    static long long nowMs();

    // Game-thread copies (for the settings panel and change detection).
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
    std::atomic<long long> m_joinSentMs{ 0 };
    std::atomic<bool> m_suspended{ false };  // this session's channel is suspended/deleted
    std::atomic<bool> m_notFound{ false };   // the last JOIN went unanswered; see getStatus()
#ifdef MXBMRP3_TEST_BUILD
    std::atomic<int> m_testStatus{ -1 };
#endif
    std::atomic<bool> m_run{ false };
    std::atomic<bool> m_workerFinished{ false };
    // joined-by: shutdown() (PluginManager::shutdown, and the initialize() rollback);
    // the destructor only spins-then-detaches (thread_detach_grace.h).
    std::thread m_thread;
};
