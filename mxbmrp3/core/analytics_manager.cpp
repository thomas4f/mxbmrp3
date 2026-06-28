// ============================================================================
// core/analytics_manager.cpp
// Privacy-friendly anonymous usage analytics (Aptabase).
// ============================================================================
#include "analytics_manager.h"
#include "plugin_constants.h"
#include "settings_manager.h"
#include "hud_manager.h"
#include "xinput_reader.h"
#include "update_checker.h"
#include "../hud/helmet_overlay_hud.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#if GAME_HAS_DISCORD
    #include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
    #include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
    #include "http_server.h"
#endif

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr const char* ANALYTICS_SUBDIRECTORY = "mxbmrp3";
constexpr const char* ANALYTICS_FILENAME = "mxbmrp3_analytics.json";
// SDK version is our own informal identifier, bumped if the payload shape changes.
// Bump on any payload-shape change so old/new event schemas are distinguishable
// in the data. 2.0.0 = per-feature 0/1 flags (hud_*/widget_*/feat_*), app_ended.
// 2.0.1 = dropped bogus feat_global, added feat_widgets (master widget toggle).
// 2.1.0 = added version_status/prev_version (install vs update detection).
// 2.2.0 = added launch_count, install_age_days, steam_runtime.
// 2.3.0 = added feat_devmode, update_channel.
// 2.4.0 = removed the app_ended event (session duration no longer tracked).
constexpr const char* ANALYTICS_SDK_VERSION = "mxbmrp3-analytics@2.4.0";

// Build a UUID-v4 string from 16 cryptographically-random bytes. This is the
// ONLY identifier we ever send — it is random (not derived from hardware or
// user), so it cannot be tied back to a person. Returns "" on RNG failure.
std::string generateUuidV4() {
    unsigned char b[16];
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, b, sizeof(b),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        return "";
    }
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);  // variant 1
    char out[37];
    snprintf(out, sizeof(out),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(out);
}

// Current UTC time as Aptabase's ISO-8601 millisecond timestamp.
std::string isoTimestamp() {
    SYSTEMTIME st;
    GetSystemTime(&st);
    char out[32];
    snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
    return std::string(out);
}

// Seconds since the Unix epoch (UTC).
unsigned long long epochSecondsNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;
    return (u.QuadPart - EPOCH_DIFF_100NS) / 10000000ULL;
}

// Per-launch session id, in Aptabase's exact format: epoch SECONDS * 1e8 plus
// an 8-digit random suffix (an ~18-digit number). This is critical — Aptabase
// derives the session's start time as (sessionId / 100000000), so a plain
// epoch-millis id decodes to a 1970 session date and the event never appears in
// the dashboard. Matches what Aptabase's own SDKs emit.
std::string makeSessionId() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100ns ticks since 1601; convert to seconds since Unix epoch.
    const unsigned long long EPOCH_DIFF_100NS = 116444736000000000ULL;
    unsigned long long epochSeconds = (u.QuadPart - EPOCH_DIFF_100NS) / 10000000ULL;

    // 8-digit random suffix (0..99,999,999).
    unsigned int rnd = 0;
    unsigned long long suffix;
    if (BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<unsigned char*>(&rnd),
                                       sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        suffix = rnd % 100000000ULL;
    } else {
        suffix = u.QuadPart % 100000000ULL;  // fallback: sub-second clock bits
    }

    unsigned long long sid = epochSeconds * 100000000ULL + suffix;
    char out[32];
    snprintf(out, sizeof(out), "%llu", sid);
    return std::string(out);
}

// Aptabase's osName dimension. Always Windows here (the only supported
// platform). We deliberately don't send osVersion — a Win7 compat shim on the
// host makes RtlGetVersion/GetVersionEx report a bogus "6.1.7600".
std::string osName() {
    return "Windows";
}

std::string userLocale() {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0) {
        // Locale names are ASCII (e.g. "en-US"), so a plain narrowing is safe.
        std::string s;
        for (wchar_t* p = buf; *p; ++p) s += static_cast<char>(*p & 0x7F);
        return s;
    }
    return "";
}

// Host for the App Key's region. Aptabase encodes the region as the middle
// segment of the key (A-US-xxxx / A-EU-xxxx). Self-hosted ("SH") keys need a
// custom host we don't carry, so they return "" (no send).
std::wstring hostForAppKey(const std::string& key) {
    if (key.find("-EU-") != std::string::npos) return L"eu.aptabase.com";
    if (key.find("-US-") != std::string::npos) return L"us.aptabase.com";
    return L"";
}

}  // namespace

AnalyticsManager::AnalyticsManager()
    : m_enabled(true)   // opt-out: on by default
    , m_shutdownRequested(false)
    , m_hSession(nullptr)
    , m_hConnect(nullptr)
    , m_hRequest(nullptr)
{
}

AnalyticsManager::~AnalyticsManager() {
    shutdown();
}

AnalyticsManager& AnalyticsManager::getInstance() {
    static AnalyticsManager instance;
    return instance;
}

bool AnalyticsManager::isConfigured() {
    const char* key = PluginConstants::ANALYTICS_APP_KEY;
    if (!key || key[0] == '\0') return false;
    // Shipped placeholder disables all sends until a real key is pasted in.
    return std::strcmp(key, PluginConstants::ANALYTICS_APP_KEY_PLACEHOLDER) != 0;
}

bool AnalyticsManager::goatCounterConfigured() {
    const char* code = PluginConstants::GOATCOUNTER_CODE;
    const char* token = PluginConstants::GOATCOUNTER_TOKEN;
    if (!code || code[0] == '\0' || !token || token[0] == '\0') return false;
    return std::strcmp(code, PluginConstants::GOATCOUNTER_CODE_PLACEHOLDER) != 0
        && std::strcmp(token, PluginConstants::GOATCOUNTER_TOKEN_PLACEHOLDER) != 0;
}

std::string AnalyticsManager::buildGoatCounterBody() const {
    using nlohmann::json;
    // GoatCounter breaks down by path, so encode launch + game + version there.
    // Keep the hit object to documented fields only — the API rejects unknown
    // ones (e.g. "bot", a pixel-only query param, which 404'd earlier).
    json hit;
    hit["path"] = std::string("/launch/") + GAME_SHORT_NAME + "/" + PluginConstants::PLUGIN_VERSION;
    hit["title"] = std::string("Launch (") + GAME_NAME + " " + PluginConstants::PLUGIN_VERSION + ")";

    json body;
    if (!m_installId.empty()) {
        // Provide our own session = the anonymous install id, so GoatCounter
        // groups all of an install's launches together and counts UNIQUE
        // INSTALLS (a real headcount), not raw launches. GoatCounter hashes this
        // for grouping and doesn't store it raw.
        hit["session"] = m_installId;
        body["no_sessions"] = false;
    } else {
        // No identity available — fall back to counting total launches.
        body["no_sessions"] = true;
    }
    body["hits"] = json::array({ hit });
    return body.dump();
}

void AnalyticsManager::loadAndUpdateIdentity(const char* savePath) {
    m_installId.clear();
    m_prevVersion.clear();
    m_firstSeenUnix = 0;
    m_launchCount = 0;

    // Resolve <savePath>/mxbmrp3/mxbmrp3_analytics.json (mirrors StatsManager).
    std::string dir;
    if (!savePath || savePath[0] == '\0') {
        dir = std::string(".\\") + ANALYTICS_SUBDIRECTORY;
    } else {
        dir = savePath;
        if (dir.back() != '/' && dir.back() != '\\') dir += '\\';
        dir += ANALYTICS_SUBDIRECTORY;
    }
    if (!CreateDirectoryA(dir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("AnalyticsManager: could not create %s (error %lu)", dir.c_str(), err);
        }
    }
    std::string path = dir + "\\" + ANALYTICS_FILENAME;

    // Distinguish "file doesn't exist yet" from "exists but couldn't be read".
    // We must never overwrite an existing-but-unreadable file (a transient lock,
    // e.g. an AV scanner) — doing so would rotate the stable install id and
    // inflate unique-install counts.
    const bool fileExists = (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    bool existingUnreadable = false;
    std::string id;
    unsigned long long firstSeen = 0, launches = 0;

    if (fileExists) {
        try {
            std::ifstream in(path);
            if (in.is_open()) {
                nlohmann::json j;
                in >> j;
                id = j.value("installId", "");
                m_prevVersion = j.value("lastVersion", "");  // "" if absent (pre-2.1 file)
                firstSeen = j.value("firstSeen", 0ULL);
                launches = j.value("launchCount", 0ULL);
            } else {
                existingUnreadable = true;  // present but not openable (locked?)
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("AnalyticsManager: failed to read analytics file: %s", e.what());
            existingUnreadable = true;  // present but unparseable — don't clobber
        }
    }

    if (existingUnreadable) {
        // Use a session-only id and leave the file intact, so the persisted id
        // (and counters) survive if the read failure was transient.
        DEBUG_WARN("AnalyticsManager: analytics file unreadable; using session-only id");
        m_installId = generateUuidV4();   // may be "" on RNG failure; caller handles it
        m_firstSeenUnix = epochSecondsNow();
        m_launchCount = 0;                 // unknown this run
        return;
    }

    if (id.empty()) {
        id = generateUuidV4();  // first run, or well-formed file missing the key
        if (id.empty()) return;  // RNG failure — leave m_installId empty
    }
    m_installId = id;
    m_firstSeenUnix = (firstSeen != 0) ? firstSeen : epochSecondsNow();  // set once
    m_launchCount = launches + 1;  // count this launch

    // Persist identity + counters. Written every launch — the file is tiny.
    try {
        nlohmann::json j;
        j["installId"] = m_installId;
        j["lastVersion"] = PluginConstants::PLUGIN_VERSION;
        j["firstSeen"] = m_firstSeenUnix;
        j["launchCount"] = m_launchCount;
        std::ofstream out(path, std::ios::trunc);
        if (out.is_open()) out << j.dump(2);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: failed to write analytics file: %s", e.what());
    }
}

std::string AnalyticsManager::buildEventBody() const {
    using nlohmann::json;

    json sys;
    sys["locale"] = userLocale();
    sys["osName"] = osName();
    // osVersion intentionally omitted: a Win7 compat shim on the host reports a
    // bogus "6.1.7600", so the value is misleading. osName + appVersion suffice.
#ifdef _DEBUG
    sys["isDebug"] = true;
#else
    sys["isDebug"] = false;
#endif
    sys["appVersion"] = PluginConstants::PLUGIN_VERSION;  // version distribution
    sys["sdkVersion"] = ANALYTICS_SDK_VERSION;

    json props;
    props["install_id"] = m_installId;
    props["game"] = GAME_NAME;  // dimension for per-game charts (MXB/GPB/KRP)
    // Install/update lifecycle: "new" (first run), "update" (version changed
    // since last launch, by any means), or "same". prev_version is "" on a new
    // install. Captures adoption/reach with zero extra events.
    props["version_status"] = m_versionStatus;
    props["prev_version"] = m_prevVersion;
    // Update channel this install follows — sizes the beta-tester population.
    props["update_channel"] = UpdateChecker::getInstance().isPrereleaseChannel() ? "prerelease" : "stable";

    // Retention signals (anonymous counters, no dates/history kept): how many
    // times this install has launched, and how long it's been installed.
    props["launch_count"] = static_cast<long long>(m_launchCount);
    {
        const unsigned long long now = epochSecondsNow();
        const unsigned long long ageSec = (now >= m_firstSeenUnix) ? (now - m_firstSeenUnix) : 0;
        props["install_age_days"] = static_cast<long long>(ageSec / 86400ULL);
    }

    // Steam build vs standalone (which distribution channel users come from).
#if GAME_HAS_STEAM_FRIENDS
    props["steam_runtime"] = SteamFriendsManager::isSteamRuntimeAvailable() ? 1 : 0;
#endif

    // One 0/1 flag per HUD/widget (key "hud_<name>" / "widget_<name>"), derived
    // from the canonical settings capture so new HUDs/widgets appear with no
    // hardcoded list. avg() of a 0/1 flag in Aptabase is the feature's adoption
    // rate. Counts are convenience aggregates.
    std::vector<std::pair<std::string, int>> flags;
    SettingsManager::getInstance().getHudWidgetFlags(HudManager::getInstance(), flags);
    int hudCount = 0, widgetCount = 0;
    for (const auto& f : flags) {
        props[f.first] = f.second;
        if (f.second) {
            if (f.first.rfind("hud_", 0) == 0) ++hudCount;
            else if (f.first.rfind("widget_", 0) == 0) ++widgetCount;
        }
    }
    props["hud_count"] = hudCount;
    props["widget_count"] = widgetCount;

    // feat_* : capabilities that aren't HUDs/widgets (managers / global toggles).
#if GAME_HAS_DISCORD
    props["feat_discord"] = DiscordManager::getInstance().isEnabled() ? 1 : 0;
#endif
#if GAME_HAS_STEAM_FRIENDS
    props["feat_steam"] = SteamFriendsManager::getInstance().isEnabled() ? 1 : 0;
#endif
#if GAME_HAS_HTTP_SERVER
    props["feat_overlay"] = HttpServer::getInstance().isEnabled() ? 1 : 0;
#endif
    props["feat_rumble"] = XInputReader::getInstance().getRumbleConfig().enabled ? 1 : 0;
    props["feat_helmet"] = HudManager::getInstance().getHelmetOverlayHud().isVisible() ? 1 : 0;
    props["feat_updates"] = UpdateChecker::getInstance().isEnabled() ? 1 : 0;
    // Master widgets toggle: when off, no widget_* shows regardless of its flag.
    props["feat_widgets"] = HudManager::getInstance().areWidgetsEnabled() ? 1 : 0;
    // Developer mode (INI-only power-user flag): mainly to filter the dev's and
    // testers' own Release-build sessions out of real-user stats.
    props["feat_devmode"] = SettingsManager::getInstance().isDeveloperMode() ? 1 : 0;

    json event;
    event["timestamp"] = isoTimestamp();
    event["sessionId"] = m_sessionId;
    event["eventName"] = "app_started";
    event["systemProps"] = std::move(sys);
    event["props"] = std::move(props);

    json arr = json::array();
    arr.push_back(std::move(event));
    return arr.dump();
}

std::string AnalyticsManager::buildCustomEventBody(const std::string& eventName,
        const std::map<std::string, std::string>& props) const {
    using nlohmann::json;

    json sys;
    sys["osName"] = osName();
#ifdef _DEBUG
    sys["isDebug"] = true;
#else
    sys["isDebug"] = false;
#endif
    sys["appVersion"] = PluginConstants::PLUGIN_VERSION;
    sys["sdkVersion"] = ANALYTICS_SDK_VERSION;

    json p;
    p["install_id"] = m_installId;   // same identity context as app_started
    p["game"] = GAME_NAME;
    for (const auto& kv : props) p[kv.first] = kv.second;

    json event;
    event["timestamp"] = isoTimestamp();
    event["sessionId"] = m_sessionId;  // group into the launch session
    event["eventName"] = eventName;
    event["systemProps"] = std::move(sys);
    event["props"] = std::move(p);

    json arr = json::array();
    arr.push_back(std::move(event));
    return arr.dump();
}

void AnalyticsManager::trackEvent(const std::string& eventName,
                                  const std::map<std::string, std::string>& props) {
    // Only when analytics is active and app_started has been sent this launch
    // (m_sessionId set), so the event groups into the same Aptabase session.
    if (m_shutdownRequested) return;
    if (!m_enabled || !isConfigured() || m_host.empty() || m_sessionId.empty()) return;

    std::string body;
    try {
        body = buildCustomEventBody(eventName, props);
    } catch (...) {
        return;  // never throw on the game thread
    }
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);
        m_eventQueue.push_back(std::move(body));
    }
    m_eventCv.notify_one();
}

void AnalyticsManager::eventWorkerLoop() {
    // Exception barrier: an uncaught throw here would std::terminate the game.
    try {
        for (;;) {
            std::string body;
            {
                std::unique_lock<std::mutex> lock(m_eventMutex);
                m_eventCv.wait(lock, [this] {
                    return m_shutdownRequested || !m_eventQueue.empty();
                });
                if (m_eventQueue.empty()) {
                    if (m_shutdownRequested) break;  // drained + asked to stop
                    continue;
                }
                body = std::move(m_eventQueue.front());
                m_eventQueue.pop_front();
            }
            // Short timeout so a slow send can't stall game shutdown for long.
            postSync(m_host, body, 3000);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: event worker exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("AnalyticsManager: event worker unknown exception");
    }
}

void AnalyticsManager::initialize(const char* savePath) {
    // Defensive: only one beacon per process. A second initialize() would
    // overwrite a live std::thread and call std::terminate.
    if (m_thread.joinable()) {
        DEBUG_INFO("AnalyticsManager: already initialized, skipping");
        return;
    }
    if (!m_enabled) {
        DEBUG_INFO("AnalyticsManager: disabled by settings, no beacon sent");
        return;
    }

    const bool aptConfigured = isConfigured();
    const bool gcConfigured = goatCounterConfigured();
    if (!aptConfigured && !gcConfigured) {
        DEBUG_INFO("AnalyticsManager: nothing configured, no beacon sent");
        return;
    }

    // Shared anonymous identity (install id + version + firstSeen + launchCount),
    // used by both services — so GoatCounter can group by install even when
    // Aptabase is off. m_installId is "" on RNG/file failure.
    loadAndUpdateIdentity(savePath);

    // --- Aptabase (rich structured events) ---
    std::wstring aptHost;
    std::string aptBody;
    if (aptConfigured && !m_installId.empty()) {
        const char* key = PluginConstants::ANALYTICS_APP_KEY;
        aptHost = hostForAppKey(key);
        if (aptHost.empty()) {
            DEBUG_WARN("AnalyticsManager: unrecognized App Key region, skipping Aptabase");
        } else {
            m_wAppKey.assign(key, key + std::strlen(key));
            m_host = aptHost;
            // Detect install/update: compare the stored version to this one.
            if (m_prevVersion.empty()) m_versionStatus = "new";
            else if (m_prevVersion != PluginConstants::PLUGIN_VERSION) m_versionStatus = "update";
            else m_versionStatus = "same";
            m_sessionId = makeSessionId();
            try {
                aptBody = buildEventBody();
            } catch (const std::exception& e) {
                DEBUG_WARN_F("AnalyticsManager: failed to build beacon: %s", e.what());
                aptBody.clear();
                aptHost.clear();
                // The event worker won't be started, so clear the fields
                // trackEvent() gates on — otherwise queued custom events would
                // pile up in m_eventQueue with no drainer.
                m_host.clear();
                m_sessionId.clear();
            }
        }
    }

    // --- GoatCounter (uncapped headcount + version) ---
    if (gcConfigured) {
        const char* code = PluginConstants::GOATCOUNTER_CODE;
        m_gcHost.assign(code, code + std::strlen(code));
        m_gcHost += L".goatcounter.com";
        m_gcBody = buildGoatCounterBody();  // groups by m_installId for unique counts
    }

    const bool haveApt = !aptHost.empty() && !aptBody.empty();
    const bool haveGc = !m_gcHost.empty() && !m_gcBody.empty();
    if (!haveApt && !haveGc) {
        DEBUG_INFO("AnalyticsManager: nothing to send");
        return;
    }

    m_shutdownRequested = false;
    // The custom-event worker (trackEvent) only makes sense when Aptabase is
    // active — custom events go there, not to the GoatCounter headcount pixel.
    if (haveApt) {
        m_eventWorker = std::thread(&AnalyticsManager::eventWorkerLoop, this);
    }
    m_thread = std::thread(&AnalyticsManager::postBeacon, this,
                           haveApt ? aptHost : std::wstring(),
                           haveApt ? aptBody : std::string());
}

void AnalyticsManager::postBeacon(std::wstring host, std::string body) {
    // Thread entry. Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game. Sends the Aptabase app_started
    // POST (when configured), then the GoatCounter headcount GET — the two are
    // independent so a failure of one never blocks the other.
    try {
        if (m_shutdownRequested) return;
        if (!host.empty() && !body.empty()) postAptabase(host, body);
        if (!m_shutdownRequested) sendGoatCounterHit();
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: beacon thread exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("AnalyticsManager: beacon thread unknown exception");
    }
}

void AnalyticsManager::postAptabase(const std::wstring& host, const std::string& body) {
    {
        HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return;
        WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/events",
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return;
        }

        // Publish handles so shutdown() can cancel the blocking send by closing
        // them (CancelSynchronousIo does not work with WinHTTP).
        {
            std::lock_guard<std::mutex> lock(m_handleMutex);
            m_hSession = hSession;
            m_hConnect = hConnect;
            m_hRequest = hRequest;
        }
        if (m_shutdownRequested) { closeHandles(); return; }

        std::wstring headers = L"Content-Type: application/json\r\nApp-Key: " + m_wAppKey;

        // Log the outgoing request for diagnostics. The payload is anonymous and
        // this is the player's own local log, so logging the full body is safe.
        {
            std::string hostA;
            for (wchar_t c : host) hostA += static_cast<char>(c);  // host is ASCII
            DEBUG_INFO_F("AnalyticsManager: POST https://%s/api/v0/events (%zu bytes): %s",
                         hostA.c_str(), body.size(), body.c_str());
        }

        BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                     (LPVOID)body.data(), (DWORD)body.size(),
                                     (DWORD)body.size(), 0);
        if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

        DWORD status = 0;
        if (ok) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(hRequest,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);

            // Read the (small) response body so the log shows exactly what
            // Aptabase replied — "{}" means accepted; anything else is a clue.
            std::string response;
            DWORD avail = 0;
            while (response.size() < 4096 &&
                   WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                std::vector<char> buf(avail + 1, 0);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
                response.append(buf.data(), read);
            }

            if (status == 200) {
                DEBUG_INFO_F("AnalyticsManager: beacon accepted (HTTP 200, response: %s)",
                             response.c_str());
            } else {
                DEBUG_WARN_F("AnalyticsManager: beacon rejected (HTTP %lu, response: %s)",
                             status, response.c_str());
            }
        } else if (!m_shutdownRequested) {
            DEBUG_WARN_F("AnalyticsManager: beacon send failed (WinHTTP error %lu)", GetLastError());
        }

        closeHandles();
    }
}

void AnalyticsManager::closeHandles() {
    std::lock_guard<std::mutex> lock(m_handleMutex);
    // Close child handles before parent (WinHTTP documented best practice).
    if (m_hRequest) { WinHttpCloseHandle(m_hRequest); m_hRequest = nullptr; }
    if (m_hConnect) { WinHttpCloseHandle(m_hConnect); m_hConnect = nullptr; }
    if (m_hSession) { WinHttpCloseHandle(m_hSession); m_hSession = nullptr; }
}

void AnalyticsManager::sendGoatCounterHit() {
    if (m_gcHost.empty() || m_gcBody.empty()) return;

    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, m_gcHost.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/count",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    // Publish handles so shutdown() can cancel the POST. Safe to reuse the slots:
    // the Aptabase POST (if any) has already finished and cleared them.
    {
        std::lock_guard<std::mutex> lock(m_handleMutex);
        m_hSession = hSession;
        m_hConnect = hConnect;
        m_hRequest = hRequest;
    }
    if (m_shutdownRequested) { closeHandles(); return; }

    std::wstring tokenW;
    {
        const char* t = PluginConstants::GOATCOUNTER_TOKEN;
        tokenW.assign(t, t + std::strlen(t));  // ASCII
    }
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + tokenW;

    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                 (LPVOID)m_gcBody.data(), (DWORD)m_gcBody.size(),
                                 (DWORD)m_gcBody.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        // 202 Accepted is the documented success for /api/v0/count.
        DEBUG_INFO_F("AnalyticsManager: GoatCounter hit (HTTP %lu)", status);
    } else if (!m_shutdownRequested) {
        DEBUG_INFO_F("AnalyticsManager: GoatCounter hit failed (WinHTTP error %lu)", GetLastError());
    }

    closeHandles();
}

void AnalyticsManager::postSync(const std::wstring& host, const std::string& body,
                                unsigned long timeoutMs) {
    // Self-contained synchronous POST with local handles, used by the custom-
    // event worker. Short timeout so a slow send can't stall game shutdown for
    // long; not tied to the cancellation handles.
    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/events",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    std::wstring headers = L"Content-Type: application/json\r\nApp-Key: " + m_wAppKey;
    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                 (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DEBUG_INFO_F("AnalyticsManager: event POST (HTTP %lu)", status);
    } else {
        DEBUG_INFO_F("AnalyticsManager: event POST failed (WinHTTP error %lu)", GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

void AnalyticsManager::shutdown() {
    m_shutdownRequested = true;
    // Abort any in-flight startup POST so we don't stall game shutdown on the timeout.
    closeHandles();
    if (m_thread.joinable()) m_thread.join();

    // Wake the custom-event worker so it flushes any queued events and exits.
    // The empty locked scope synchronizes with the worker's wait predicate so
    // the notify can't be missed.
    {
        std::lock_guard<std::mutex> lk(m_eventMutex);
    }
    m_eventCv.notify_all();
    if (m_eventWorker.joinable()) m_eventWorker.join();
}
