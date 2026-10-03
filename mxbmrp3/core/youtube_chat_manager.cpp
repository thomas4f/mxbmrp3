// ============================================================================
// core/youtube_chat_manager.cpp
// Read-only YouTube live chat. See the header for the threading and
// cancellation model; the parsing lives in youtube_chat.h.
// ============================================================================
#include "youtube_chat_manager.h"
#include "youtube_chat.h"
#include "thread_detach_grace.h"
#include "winhttp_guard.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <windows.h>
#include <winhttp.h>

namespace {
constexpr const wchar_t* YOUTUBE_HOST = L"www.youtube.com";
constexpr DWORD REQUEST_TIMEOUT_MS = 15000;
// A watch page is ~1 MB; anything far past that is not a page we want.
constexpr size_t MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
// The web client's own identity: YouTube serves the page the readers parse to
// a desktop browser. Any current desktop UA works; none of the readers rotate it.
constexpr const wchar_t* USER_AGENT =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    L"Chrome/139.0.0.0 Safari/537.36";
// SOCS=CAI skips the EU cookie-consent interstitial (yt-dlp and chat-downloader
// set the same cookie); without it an EU streamer gets the consent page, which
// parses as Unrecognized.
constexpr const wchar_t* PAGE_HEADERS =
    L"Accept-Language: en-US,en;q=0.9\r\n"
    L"Cookie: SOCS=CAI\r\n";

std::wstring widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());  // ASCII only: paths and header values we built
}
}  // namespace

YouTubeChatManager& YouTubeChatManager::getInstance() {
    static YouTubeChatManager instance;
    return instance;
}

YouTubeChatManager::~YouTubeChatManager() {
    // Static-teardown backstop: only reached when the orchestrated shutdown()
    // was skipped. Never joins (loader lock) -- see thread_detach_grace.h.
    if (m_thread.joinable()) {
        m_run.store(false);
        cancelActive();
        m_cv.notify_all();
        ThreadTeardown::spinThenDetach(m_thread, m_workerFinished);
    }
}

long long YouTubeChatManager::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

YouTubeChatManager::Status YouTubeChatManager::getStatus() const {
#ifdef MXBMRP3_TEST_BUILD
    const int forced = m_testStatus.load(std::memory_order_relaxed);
    if (forced >= 0) return static_cast<Status>(forced);
#endif
    return static_cast<Status>(m_status.load(std::memory_order_acquire));
}

void YouTubeChatManager::setChannel(const std::string& input) {
    const std::string channel = YouTubeChat::normalizeChannel(input);
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
        clear.platform = Chat::Platform::YouTube;
        clear.arrival = Chat::nextArrival();
        m_queue.push_back(std::move(clear));
        m_hasPending.store(true, std::memory_order_release);
    }
    // Never the channel itself: logs get shared and must stay anonymous.
    DEBUG_INFO_F("YouTubeChat: channel %s", channel.empty() ? "cleared" : "set");
    cancelActive();
    m_cv.notify_all();
}

void YouTubeChatManager::setEnabled(bool enabled) {
    if (enabled == m_enabledGame) return;
    m_enabledGame = enabled;
    {
        MutexLock lock(m_mutex);
        m_wanted = enabled;
        ++m_generation;
    }
    DEBUG_INFO_F("YouTubeChat: %s", enabled ? "enabled" : "disabled");
    if (!enabled) cancelActive();
    m_cv.notify_all();
}

void YouTubeChatManager::tick() {
    if (m_enabledGame && !m_channelGame.empty()) ensureWorker();
}

void YouTubeChatManager::ensureWorker() {
#ifdef MXBMRP3_TEST_BUILD
    if (m_testStatus.load() >= 0) return;  // a forced status means "stay offline"
#endif
    if (m_thread.joinable()) {
        if (!m_workerFinished.load(std::memory_order_acquire)) return;  // alive
        // The worker left through its top-level catch: reap and restart it, at
        // most every RESTART_INTERVAL_MS (see TwitchChatManager::ensureWorker).
        const long long now = nowMs();
        if (now - m_lastRestartMs < RESTART_INTERVAL_MS) return;
        m_lastRestartMs = now;
        m_thread.join();
        DEBUG_WARN("YouTubeChat: worker had stopped; restarting it");
    }
    m_run.store(true);
    m_workerFinished.store(false);
    m_thread = std::thread([this]() { workerMain(); });
}

void YouTubeChatManager::cancelActive() {
    MutexLock lock(m_mutex);
    if (m_hActive) {
        WinHttpCloseHandle(static_cast<HINTERNET>(m_hActive));
        m_hActive = nullptr;
    }
}

void YouTubeChatManager::shutdown() {
    if (!m_thread.joinable()) return;
    m_run.store(false);
    cancelActive();
    m_cv.notify_all();
    m_thread.join();
    m_status.store(static_cast<uint8_t>(Status::OFF));
    DEBUG_INFO("YouTubeChat: worker stopped");
}

bool YouTubeChatManager::drain(std::vector<Chat::Event>& out) {
    if (!m_hasPending.load(std::memory_order_acquire)) return false;
    MutexLock lock(m_mutex);
    for (auto& ev : m_queue) out.push_back(std::move(ev));
    m_queue.clear();
    m_hasPending.store(false, std::memory_order_release);
    return true;
}

void YouTubeChatManager::pushEvents(std::vector<Chat::Event>&& events, uint32_t generation) {
    if (events.empty()) return;
    MutexLock lock(m_mutex);
    // A response read after a channel switch belongs to the old channel.
    if (generation != m_generation) return;
    for (auto& ev : events) {
        if (m_queue.size() >= MAX_QUEUED_EVENTS) m_queue.pop_front();
        ev.arrival = Chat::nextArrival();
        m_queue.push_back(std::move(ev));
    }
    m_hasPending.store(true, std::memory_order_release);
}

int YouTubeChatManager::handlePoll(const std::string& body, uint32_t generation,
                                   std::string& continuation, int& delayMs) {
    YouTubeChat::PollInfo poll = YouTubeChat::parseChatResponse(body);
    if (poll.result == YouTubeChat::PollResult::Ok) {
        continuation = std::move(poll.continuation);
        delayMs = poll.delayMs;
        pushEvents(std::move(poll.events), generation);
    } else if (poll.result == YouTubeChat::PollResult::Ended) {
        pushEvents(std::move(poll.events), generation);  // the last lines before the chat closed
    } else if (poll.result == YouTubeChat::PollResult::Unrecognized) {
        m_status.store(static_cast<uint8_t>(Status::UNAVAILABLE));
    }
    return static_cast<int>(poll.result);
}

#ifdef MXBMRP3_TEST_BUILD
int YouTubeChatManager::testInjectPoll(const std::string& body) {
    uint32_t generation;
    {
        MutexLock lock(m_mutex);
        generation = m_generation;
    }
    std::string continuation;
    int delayMs = 0;
    const int result = handlePoll(body, generation, continuation, delayMs);
    // A forced status stands in for the worker's: a poll the worker would have
    // turned into UNAVAILABLE does the same to it, so the panel's message is
    // testable without letting a worker (and a network request) start.
    if (result == static_cast<int>(YouTubeChat::PollResult::Unrecognized) && m_testStatus.load() >= 0) {
        m_testStatus.store(static_cast<int>(Status::UNAVAILABLE));
    }
    return result;
}
#endif

bool YouTubeChatManager::stillCurrent(uint32_t generation) {
    MutexLock lock(m_mutex);
    return m_run.load() && generation == m_generation;
}

bool YouTubeChatManager::waitFor(long long ms, uint32_t generation) {
    CvLock lock(m_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (m_run.load() && generation == m_generation) {
        if (m_cv.wait_until(lock.native(), deadline) == std::cv_status::timeout) {
            return m_run.load() && generation == m_generation;
        }
    }
    return false;
}

void YouTubeChatManager::workerMain() {
    try {
        YouTubeChat::RecheckPolicy policy;
        uint32_t lastGeneration = 0;
        bool first = true;
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
            // "Connecting" only for a new channel or switch-on: a re-check keeps
            // showing what it last found (not live, retrying...) until it finds
            // something else, so the panel does not flicker once a minute.
            if (first || generation != lastGeneration) {
                first = false;
                lastGeneration = generation;
                policy.reset();
                m_status.store(static_cast<uint8_t>(Status::CONNECTING));
            }

            bool connected = false;
            const YouTubeChat::SessionEnd end = runSession(channel, generation, connected);
            if (!m_run.load()) break;
            if (end == YouTubeChat::SessionEnd::Reconfigured || !stillCurrent(generation)) {
                continue;  // new channel / switched off: act on the new config at once
            }

            const YouTubeChat::Recheck next = policy.next(end, connected);
            switch (next.show) {
            case YouTubeChat::RecheckShow::NotLive:     m_status.store(static_cast<uint8_t>(Status::NOT_LIVE)); break;
            case YouTubeChat::RecheckShow::NotFound:    m_status.store(static_cast<uint8_t>(Status::NOT_FOUND)); break;
            case YouTubeChat::RecheckShow::Unavailable: m_status.store(static_cast<uint8_t>(Status::UNAVAILABLE)); break;
            case YouTubeChat::RecheckShow::Retrying:    m_status.store(static_cast<uint8_t>(Status::RETRYING)); break;
            case YouTubeChat::RecheckShow::Keep:        break;
            }
            DEBUG_INFO_F("YouTubeChat: checking the channel again in %us", next.waitS);
            waitFor(static_cast<long long>(next.waitS) * 1000, generation);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("YouTubeChat: worker exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("YouTubeChat: worker unknown exception");
    }
    m_status.store(static_cast<uint8_t>(Status::OFF));
    m_workerFinished.store(true, std::memory_order_release);
}

YouTubeChat::SessionEnd YouTubeChatManager::runSession(const std::string& channel, uint32_t generation,
                                                       bool& connected) {
    const WinHttpHandle hSession(WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) {
        DEBUG_WARN_F("YouTubeChat: WinHttpOpen failed (%lu)", GetLastError());
        return YouTubeChat::SessionEnd::NetworkError;
    }
    WinHttpSetTimeouts(hSession.get(), REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS,
                       REQUEST_TIMEOUT_MS);
    const WinHttpHandle hConnect(WinHttpConnect(hSession.get(), YOUTUBE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect) {
        DEBUG_WARN_F("YouTubeChat: WinHttpConnect failed (%lu)", GetLastError());
        return YouTubeChat::SessionEnd::NetworkError;
    }

    // One request, start to finish. False on a network failure or cancellation;
    // `status` is the HTTP status otherwise.
    std::string response;
    DWORD lastError = 0;  // the failing WinHTTP call's own code, read before anything else runs
    auto request = [&](const wchar_t* method, const std::string& path, const std::wstring& headers,
                       const std::string& body, DWORD& status) -> bool {
        response.clear();
        status = 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect.get(), method, widen(path).c_str(), nullptr,
                                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            lastError = GetLastError();
            return false;
        }
        {
            MutexLock lock(m_mutex);
            if (!m_run.load() || generation != m_generation) {
                WinHttpCloseHandle(hRequest);
                return false;
            }
            m_hActive = hRequest;
        }
        // A canceller may already have closed it (see TwitchChatManager for the
        // handle-value reuse caveat, which applies here the same way). Runs on
        // a throw too (winhttp_guard.h).
        const ScopeExit releaseRequest([&]() {
            MutexLock lock(m_mutex);
            if (m_hActive == hRequest) {
                m_hActive = nullptr;
                WinHttpCloseHandle(hRequest);
            }
        });
        bool ok = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(-1L),
                                     body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                     static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
                  WinHttpReceiveResponse(hRequest, nullptr);
        if (!ok) lastError = GetLastError();
        if (ok) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            char buf[16 * 1024];
            for (;;) {
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, buf, sizeof(buf), &read)) {
                    lastError = GetLastError();
                    ok = false;
                    break;
                }
                if (read == 0) break;
                response.append(buf, read);
                if (response.size() > MAX_RESPONSE_BYTES) { ok = false; break; }
            }
        }
        return ok;
    };

    YouTubeChat::SessionEnd end = YouTubeChat::SessionEnd::NetworkError;
    DWORD status = 0;
    const std::string pagePath = YouTubeChat::livePagePath(channel);
    if (!request(L"GET", pagePath, PAGE_HEADERS, std::string(), status)) {
        end = stillCurrent(generation) ? YouTubeChat::SessionEnd::NetworkError : YouTubeChat::SessionEnd::Reconfigured;
        if (end == YouTubeChat::SessionEnd::NetworkError) DEBUG_INFO_F("YouTubeChat: live page request failed (%lu)", lastError);
    } else if (status == 404) {
        DEBUG_INFO("YouTubeChat: live page not found");
        end = YouTubeChat::SessionEnd::NotFound;
    } else if (status != 200) {
        DEBUG_INFO_F("YouTubeChat: live page returned HTTP %lu", status);
        end = YouTubeChat::SessionEnd::NetworkError;  // 429 / 5xx: back off and retry
    } else {
        const YouTubeChat::PageInfo page = YouTubeChat::parseLivePage(response);
        response.clear();
        response.shrink_to_fit();  // the page is ~1 MB; polls are a few KB
        switch (page.result) {
        case YouTubeChat::PageResult::NotLive:
            DEBUG_INFO("YouTubeChat: the channel is not live");
            end = YouTubeChat::SessionEnd::NotLive;
            break;
        case YouTubeChat::PageResult::NotFound:
            DEBUG_INFO("YouTubeChat: the channel was not found");
            end = YouTubeChat::SessionEnd::NotFound;
            break;
        case YouTubeChat::PageResult::Unrecognized:
            DEBUG_WARN_F("YouTubeChat: the live page is not in a known format (%zu bytes)", response.size());
            end = YouTubeChat::SessionEnd::Unrecognized;
            break;
        case YouTubeChat::PageResult::Live: {
            DEBUG_INFO("YouTubeChat: the channel is live, reading chat");
            // The first poll's continuation comes from the chat page (the watch
            // page's own draws HTTP 400; see parseLivePage).
            std::string clientVersion = page.clientVersion;
            std::string visitorData = page.visitorData;
            const std::string chatPath = YouTubeChat::chatPagePath(page.videoId);
            if (!request(L"GET", chatPath, PAGE_HEADERS, std::string(), status)) {
                end = stillCurrent(generation) ? YouTubeChat::SessionEnd::NetworkError : YouTubeChat::SessionEnd::Reconfigured;
                if (end == YouTubeChat::SessionEnd::NetworkError) DEBUG_INFO_F("YouTubeChat: chat page request failed (%lu)", lastError);
                break;
            }
            if (status != 200) {
                DEBUG_INFO_F("YouTubeChat: chat page returned HTTP %lu", status);
                end = YouTubeChat::SessionEnd::NetworkError;
                break;
            }
            YouTubeChat::ChatPageInfo chatPage = YouTubeChat::parseChatPage(response);
            if (chatPage.continuation.empty() && chatPage.noChat) {
                // The stream ended between the two pages (or its chat went off):
                // the same as a chat that ended, so twice in a row reads "not
                // live" -- never Unavailable, which is for a format we cannot read.
                DEBUG_INFO("YouTubeChat: the chat page has no live chat (stream ended?)");
                end = YouTubeChat::SessionEnd::Ended;
                break;
            }
            if (chatPage.continuation.empty()) {
                // A live stream with chat whose chat page has no continuation:
                // YouTube moved the page, which no retry will fix.
                DEBUG_WARN_F("YouTubeChat: the chat page is not in a known format: %s",
                             YouTubeChat::logShape(response).c_str());
                end = YouTubeChat::SessionEnd::Unrecognized;
                break;
            }
            std::string continuation = std::move(chatPage.continuation);
            if (!chatPage.clientVersion.empty()) clientVersion = std::move(chatPage.clientVersion);
            if (!chatPage.visitorData.empty()) visitorData = std::move(chatPage.visitorData);
            response.clear();
            response.shrink_to_fit();
            const std::wstring pollHeaders =
                L"Content-Type: application/json\r\n"
                L"Origin: https://www.youtube.com\r\n"
                L"X-YouTube-Client-Name: 1\r\n"
                L"X-YouTube-Client-Version: " + widen(clientVersion) + L"\r\n" + PAGE_HEADERS;
            connected = false;
            for (;;) {
                const std::string body = YouTubeChat::buildPollBody(continuation, clientVersion, visitorData);
                if (!request(L"POST", "/youtubei/v1/live_chat/get_live_chat?prettyPrint=false",
                             pollHeaders, body, status)) {
                    end = stillCurrent(generation) ? YouTubeChat::SessionEnd::NetworkError : YouTubeChat::SessionEnd::Reconfigured;
                    break;
                }
                if (status == 404 || status == 403) {  // chat gone (stream deleted / made private)
                    end = YouTubeChat::SessionEnd::Ended;
                    break;
                }
                if (status != 200) {
                    // The body names the refused field; the request summary is
                    // what to compare it against (visitor data itself stays out).
                    DEBUG_INFO_F("YouTubeChat: get_live_chat returned HTTP %lu: %s", status,
                                 YouTubeChat::logShape(response).c_str());
                    DEBUG_INFO_F("YouTubeChat: request had clientVersion %s, continuation %zu chars, visitorData %s",
                                 clientVersion.c_str(), continuation.size(),
                                 visitorData.empty() ? "absent" : "present");
                    end = YouTubeChat::SessionEnd::NetworkError;
                    break;
                }
                int delayMs = YouTubeChat::DEFAULT_POLL_DELAY_MS;
                const auto result = static_cast<YouTubeChat::PollResult>(
                    handlePoll(response, generation, continuation, delayMs));
                if (result == YouTubeChat::PollResult::Unrecognized) {
                    DEBUG_WARN_F("YouTubeChat: get_live_chat response is not in a known format: %s",
                                 YouTubeChat::logShape(response).c_str());
                    end = YouTubeChat::SessionEnd::Unrecognized;
                    break;
                }
                if (result == YouTubeChat::PollResult::Ended) {
                    DEBUG_INFO("YouTubeChat: the chat ended");
                    end = YouTubeChat::SessionEnd::Ended;
                    break;
                }
                if (!connected) {
                    connected = true;
                    m_status.store(static_cast<uint8_t>(Status::CONNECTED));
                    DEBUG_INFO_F("YouTubeChat: connected (continuation %zu chars)", continuation.size());
                }
                if (!waitFor(delayMs, generation)) {
                    end = YouTubeChat::SessionEnd::Reconfigured;
                    break;
                }
            }
            break;
        }
        }
    }

    return end;
}
