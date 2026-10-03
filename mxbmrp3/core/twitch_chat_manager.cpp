// ============================================================================
// core/twitch_chat_manager.cpp
// Read-only Twitch chat connection. See the header for the threading and
// cancellation model; the parsing lives in twitch_irc.h.
// ============================================================================
#include "twitch_chat_manager.h"
#include "thread_detach_grace.h"
#include "winhttp_guard.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <windows.h>
#include <winhttp.h>

namespace {
constexpr const wchar_t* TWITCH_HOST = L"irc-ws.chat.twitch.tv";
// Twitch PINGs roughly every five minutes. A receive that waits longer than
// this has lost the connection without being told; treat it as dropped.
constexpr DWORD RECEIVE_TIMEOUT_MS = 400000;
constexpr DWORD HANDSHAKE_TIMEOUT_MS = 10000;
constexpr unsigned MAX_BACKOFF_S = 60;
}  // namespace

TwitchChatManager& TwitchChatManager::getInstance() {
    static TwitchChatManager instance;
    return instance;
}

TwitchChatManager::~TwitchChatManager() {
    // Static-teardown backstop: only reached when the orchestrated shutdown()
    // was skipped. Never joins (loader lock) -- see thread_detach_grace.h.
    if (m_thread.joinable()) {
        m_run.store(false);
        cancelActive();
        m_cv.notify_all();
        ThreadTeardown::spinThenDetach(m_thread, m_workerFinished);
    }
}

long long TwitchChatManager::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

TwitchChatManager::Status TwitchChatManager::getStatus() const {
#ifdef MXBMRP3_TEST_BUILD
    const int forced = m_testStatus.load(std::memory_order_relaxed);
    if (forced >= 0) return static_cast<Status>(forced);
#endif
    const Status s = static_cast<Status>(m_status.load(std::memory_order_acquire));
    if (s == Status::JOINING && nowMs() - m_joinSentMs.load() > JOIN_TIMEOUT_MS) {
        return Status::NOT_FOUND;
    }
    // A channel that never answered a JOIN stays "not found" through the
    // worker's quiet retries, rather than cycling Retrying/Connecting/Joining
    // on screen every few minutes. Cleared by a join or a config change.
    if (m_notFound.load() &&
        (s == Status::RETRYING || s == Status::CONNECTING || s == Status::JOINING)) {
        return Status::NOT_FOUND;
    }
    return s;
}

void TwitchChatManager::setChannel(const std::string& input) {
    const std::string channel = TwitchIrc::normalizeChannel(input);
    if (channel == m_channelGame) return;
    m_channelGame = channel;
    {
        MutexLock lock(m_mutex);
        m_channel = channel;
        ++m_generation;
        // The previous channel's lines must not linger under the new name.
        m_queue.clear();
        Chat::Event clear;
        clear.kind = Chat::EventKind::ClearAll;
        clear.platform = Chat::Platform::Twitch;
        clear.arrival = Chat::nextArrival();
        m_queue.push_back(std::move(clear));
        m_hasPending.store(true, std::memory_order_release);
        m_notFound.store(false);   // under the lock: see workerMain
    }
    // Never the channel itself: logs get shared and must stay anonymous.
    DEBUG_INFO_F("TwitchChat: channel %s", channel.empty() ? "cleared" : "set");
    cancelActive();
    m_cv.notify_all();
}

void TwitchChatManager::setEnabled(bool enabled) {
    if (enabled == m_enabledGame) return;
    m_enabledGame = enabled;
    {
        MutexLock lock(m_mutex);
        m_wanted = enabled;
        ++m_generation;
        m_notFound.store(false);
    }
    DEBUG_INFO_F("TwitchChat: %s", enabled ? "enabled" : "disabled");
    if (!enabled) cancelActive();
    m_cv.notify_all();
}

void TwitchChatManager::tick() {
    if (m_enabledGame && !m_channelGame.empty()) ensureWorker();
}

void TwitchChatManager::ensureWorker() {
#ifdef MXBMRP3_TEST_BUILD
    if (m_testStatus.load() >= 0) return;  // a forced status means "stay offline"
#endif
    if (m_thread.joinable()) {
        if (!m_workerFinished.load(std::memory_order_acquire)) return;  // alive
        // The worker left through its top-level catch: reap it and start a
        // fresh one, or the chat would stay dead until the game restarts. At
        // most every RESTART_INTERVAL_MS: tick() asks every frame, and a worker
        // that dies on arrival must not become a thread per frame. It has
        // already stored m_workerFinished, so this join returns at once.
        const long long now = nowMs();
        if (now - m_lastRestartMs < RESTART_INTERVAL_MS) return;
        m_lastRestartMs = now;
        m_thread.join();
        DEBUG_WARN("TwitchChat: worker had stopped; restarting it");
    }
    m_run.store(true);
    m_workerFinished.store(false);
    m_thread = std::thread([this]() { workerMain(); });
}

void TwitchChatManager::cancelActive() {
    MutexLock lock(m_mutex);
    if (m_hActive) {
        WinHttpCloseHandle(static_cast<HINTERNET>(m_hActive));
        m_hActive = nullptr;
    }
}

void TwitchChatManager::shutdown() {
    if (!m_thread.joinable()) return;
    m_run.store(false);
    cancelActive();
    m_cv.notify_all();
    m_thread.join();
    m_status.store(static_cast<uint8_t>(Status::OFF));
    DEBUG_INFO("TwitchChat: worker stopped");
}

bool TwitchChatManager::drain(std::vector<Chat::Event>& out) {
    if (!m_hasPending.load(std::memory_order_acquire)) return false;
    MutexLock lock(m_mutex);
    for (auto& ev : m_queue) out.push_back(std::move(ev));
    m_queue.clear();
    m_hasPending.store(false, std::memory_order_release);
    return true;
}

void TwitchChatManager::pushEvent(Chat::Event&& ev, uint32_t generation) {
    MutexLock lock(m_mutex);
    // A line read after a channel switch belongs to the old channel.
    if (generation != m_generation) return;
    if (m_queue.size() >= MAX_QUEUED_EVENTS) m_queue.pop_front();
    ev.arrival = Chat::nextArrival();
    m_queue.push_back(std::move(ev));
    m_hasPending.store(true, std::memory_order_release);
}

Chat::Event TwitchChatManager::handleLine(const std::string& raw, uint32_t generation) {
    TwitchIrc::Line line;
    if (!TwitchIrc::parseLine(raw, line)) return Chat::Event{};
    Chat::Event ev = TwitchIrc::interpret(line);
    switch (ev.kind) {
    case Chat::EventKind::Message:
    case Chat::EventKind::ClearUser:
    case Chat::EventKind::ClearAll:
    case Chat::EventKind::ClearMsg: {
        Chat::Event copy = ev;
        pushEvent(std::move(copy), generation);
        break;
    }
    case Chat::EventKind::Notice:
        // The msg-id only: the text can name the channel or a viewer.
        DEBUG_INFO_F("TwitchChat: notice (%s)", ev.id.c_str());
        // A JOIN to a NONEXISTENT channel is silently dropped (the JOINING
        // timeout covers that); a suspended or deleted one says so -- report it
        // at once instead of waiting out the timeout, and end the session: the
        // worker then holds NOT_FOUND without retrying (workerMain).
        if (ev.id == "msg_channel_suspended") {
            m_suspended.store(true);
            m_status.store(static_cast<uint8_t>(Status::NOT_FOUND));
        }
        break;
    default:
        break;
    }
    return ev;
}

#ifdef MXBMRP3_TEST_BUILD
void TwitchChatManager::testInjectLine(const std::string& raw, bool stale) {
    uint32_t generation;
    {
        MutexLock lock(m_mutex);
        generation = stale ? m_generation - 1 : m_generation;
    }
    handleLine(raw, generation);
}
#endif

void TwitchChatManager::workerMain() {
    try {
        unsigned backoffS = 0;
        while (m_run.load()) {
            std::string channel;
            uint32_t generation = 0;
            {
                CvLock lock(m_mutex);
                while (m_run.load() && !(m_wanted && !m_channel.empty())) {
                    m_status.store(static_cast<uint8_t>(Status::OFF));
                    m_cv.wait(lock.native());
                }
                channel = m_channel;
                generation = m_generation;
            }
            if (!m_run.load()) break;

            const bool joined = runSession(channel, generation);
            if (!m_run.load()) break;

            bool reconfigured;
            {
                MutexLock lock(m_mutex);
                reconfigured = (generation != m_generation);
            }
            if (reconfigured) {
                backoffS = 0;
                continue;  // new channel / switched off: act on the new config at once
            }

            // A suspended or deleted channel said so (NOTICE msg_channel_suspended):
            // retrying cannot help, so hold NOT_FOUND until the channel changes
            // or the chat is switched off.
            if (m_suspended.load()) {
                m_status.store(static_cast<uint8_t>(Status::NOT_FOUND));
                DEBUG_INFO("TwitchChat: the channel is suspended or deleted; not retrying");
                CvLock lock(m_mutex);
                while (m_run.load() && generation == m_generation) m_cv.wait(lock.native());
                backoffS = 0;
                continue;
            }

            // A JOIN that was never answered: the channel does not exist (Twitch
            // stays silent; the receive timeout ended the session). Keep reading
            // NOT_FOUND while retrying, in case the channel is created later.
            // Set under the lock, for this generation only, so a channel change
            // racing it cannot inherit the verdict.
            if (!joined && static_cast<Status>(m_status.load()) == Status::JOINING &&
                nowMs() - m_joinSentMs.load() > JOIN_TIMEOUT_MS) {
                MutexLock lock(m_mutex);
                if (generation == m_generation) m_notFound.store(true);
            }

            // A session that got into the channel and later dropped starts the
            // backoff over; repeated failures to connect at all double it.
            backoffS = (joined || backoffS == 0) ? 2 : std::min(backoffS * 2, MAX_BACKOFF_S);
            m_status.store(static_cast<uint8_t>(Status::RETRYING));
            DEBUG_INFO_F("TwitchChat: session ended, retrying in %us", backoffS);
            CvLock lock(m_mutex);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(backoffS);
            while (m_run.load() && generation == m_generation &&
                   m_cv.wait_until(lock.native(), deadline) != std::cv_status::timeout) {
            }
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("TwitchChat: worker exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("TwitchChat: worker unknown exception");
    }
    m_status.store(static_cast<uint8_t>(Status::OFF));
    m_workerFinished.store(true, std::memory_order_release);
}

bool TwitchChatManager::runSession(const std::string& channel, uint32_t generation) {
    m_status.store(static_cast<uint8_t>(Status::CONNECTING));
    m_suspended.store(false);

    char userAgentA[128];
    snprintf(userAgentA, sizeof(userAgentA), "%s/%s",
             PluginConstants::PLUGIN_DISPLAY_NAME, PluginConstants::PLUGIN_VERSION);
    const std::wstring userAgent(userAgentA, userAgentA + strlen(userAgentA));

    const WinHttpHandle hSession(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) {
        DEBUG_WARN_F("TwitchChat: WinHttpOpen failed (%lu)", GetLastError());
        return false;
    }
    WinHttpSetTimeouts(hSession.get(), HANDSHAKE_TIMEOUT_MS, HANDSHAKE_TIMEOUT_MS,
                       HANDSHAKE_TIMEOUT_MS, HANDSHAKE_TIMEOUT_MS);
    const WinHttpHandle hConnect(WinHttpConnect(hSession.get(), TWITCH_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect) {
        DEBUG_WARN_F("TwitchChat: WinHttpConnect failed (%lu)", GetLastError());
        return false;
    }

    // Publish the blocking handle so a canceller can close it. Returns false
    // (and closes it here) when the config changed before the call started.
    auto publish = [&](HINTERNET h) -> bool {
        MutexLock lock(m_mutex);
        if (!m_run.load() || generation != m_generation) {
            WinHttpCloseHandle(h);
            return false;
        }
        m_hActive = h;
        return true;
    };
    // Take the handle back. False when a canceller already closed it.
    auto unpublish = [&](HINTERNET h) -> bool {
        MutexLock lock(m_mutex);
        if (m_hActive != h) return false;
        m_hActive = nullptr;
        return true;
    };

    bool joined = false;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect.get(), L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    HINTERNET hWs = nullptr;
    if (hRequest && publish(hRequest)) {
        // Closed on every exit, a throw included (winhttp_guard.h).
        const ScopeExit releaseRequest([&]() { if (unpublish(hRequest)) WinHttpCloseHandle(hRequest); });
        const bool upgraded =
            WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
            WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr);
        // A canceller may close hRequest between ReceiveResponse and this call
        // (and hWs between a receive and the PONG send below). The call then
        // fails with ERROR_INVALID_HANDLE and the session ends -- the intended
        // outcome. The residual risk is WinHTTP reusing the closed handle's
        // VALUE in that instant, which is why cancellation stays rare (a channel
        // change, a hide, shutdown) rather than routine.
        if (upgraded) hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
        if (!hWs) DEBUG_WARN_F("TwitchChat: WebSocket handshake failed (%lu)", GetLastError());
    } else if (!hRequest) {
        DEBUG_WARN_F("TwitchChat: WinHttpOpenRequest failed (%lu)", GetLastError());
    }

    if (hWs && publish(hWs)) {
        const ScopeExit releaseWs([&]() { if (unpublish(hWs)) WinHttpCloseHandle(hWs); });
        // Without this the socket keeps the session's 10 s handshake timeout,
        // and a quiet channel would drop every few seconds (receive fails with
        // ERROR_WINHTTP_TIMEOUT, logged below). Say so if WinHTTP refuses it.
        DWORD timeout = RECEIVE_TIMEOUT_MS;
        if (!WinHttpSetOption(hWs, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout))) {
            DEBUG_WARN_F("TwitchChat: could not set the receive timeout (%lu); quiet channels may reconnect",
                         GetLastError());
        }

        auto send = [&](const std::string& s) -> bool {
            return WinHttpWebSocketSend(hWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                        const_cast<char*>(s.data()),
                                        static_cast<DWORD>(s.size())) == NO_ERROR;
        };

        // Anonymous read-only login: any justinfan<digits> nick, no PASS.
        std::mt19937 rng(static_cast<unsigned>(nowMs()));
        char nick[32];
        snprintf(nick, sizeof(nick), "justinfan%u", 10000u + rng() % 80000u);
        bool ok = send("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n") &&
                  send(std::string("NICK ") + nick + "\r\n") &&
                  send("JOIN #" + channel + "\r\n");
        if (ok) {
            m_joinSentMs.store(nowMs());
            m_status.store(static_cast<uint8_t>(Status::JOINING));
            DEBUG_INFO("TwitchChat: connected, joining the channel");
        }

        std::vector<char> buf(16 * 1024);
        std::string message;  // one WebSocket message, possibly several fragments
        while (ok && m_run.load()) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            const DWORD err = WinHttpWebSocketReceive(hWs, buf.data(), static_cast<DWORD>(buf.size()),
                                                      &read, &type);
            if (err != NO_ERROR) {
                // Cancelled is us closing the handle (channel change, hide,
                // shutdown), not a fault -- say so rather than print a code.
                if (err == ERROR_WINHTTP_OPERATION_CANCELLED) DEBUG_INFO("TwitchChat: session closed");
                else if (err == ERROR_WINHTTP_TIMEOUT) DEBUG_INFO("TwitchChat: nothing received within the timeout; reconnecting");
                else DEBUG_INFO_F("TwitchChat: receive ended (%lu)", err);
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE &&
                type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                continue;  // binary frames: not part of the protocol
            }
            message.append(buf.data(), read);
            if (message.size() > 1024 * 1024) message.clear();  // runaway fragment guard
            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;

            // One message can carry several CRLF-terminated IRC lines.
            size_t start = 0;
            while (start < message.size() && ok) {
                size_t end = message.find("\r\n", start);
                if (end == std::string::npos) end = message.size();
                const std::string rawLine = message.substr(start, end - start);
                start = end + 2;
                if (rawLine.empty()) continue;
                const Chat::Event ev = handleLine(rawLine, generation);
                if (ev.kind == Chat::EventKind::Ping) {
                    ok = send("PONG :" + ev.text + "\r\n");
                } else if (ev.kind == Chat::EventKind::Joined) {
                    if (!joined) DEBUG_INFO("TwitchChat: joined the channel");
                    joined = true;
                    m_notFound.store(false);
                    m_status.store(static_cast<uint8_t>(Status::CONNECTED));
                } else if (ev.kind == Chat::EventKind::Notice && m_suspended.load()) {
                    ok = false;  // nothing will ever arrive; see workerMain
                } else if (ev.kind == Chat::EventKind::Reconnect) {
                    DEBUG_INFO("TwitchChat: server requested reconnect");
                    ok = false;
                }
            }
            message.clear();
        }
        // No WebSocket close handshake: it waits for the server's reply, and a
        // read-only session has nothing to flush. Closing the handle (releaseWs)
        // is enough.
    }
    return joined;
}
