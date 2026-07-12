// ============================================================================
// core/analytics_manager.cpp
// Privacy-friendly anonymous usage analytics (Aptabase).
// ============================================================================
#include "analytics_manager.h"
#include "analytics_remote_config.h"
#include "analytics_endpoint.h"
#include "atomic_file_writer.h"
#include "plugin_constants.h"
#include "settings_manager.h"
#include "hud_manager.h"
#include "xinput_reader.h"
#include "director_manager.h"
#include "update_checker.h"
#include "profile_manager.h"
#include "ui_config.h"
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
#include <cstdlib>
#include <random>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#if defined(MXBMRP3_TEST_BUILD)
// Dry-run capture mode (headless wiring tests only; never in a shipping DLL). When on,
// every real network sender below is a no-op, so a test build physically cannot phone
// home, and isConfigured() reports true (simulate a configured install). Set by
// AnalyticsManager::testPrime(); default off. See the test-seam methods at the bottom.
static bool s_testCaptureMode = false;
#endif

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
// 2.5.0 = session_end (duration) on clean exit + crash event (fault module+offset,
//         code, recovered duration) reported on the next launch from a crash marker.
// 2.6.0 = added osVersion (registry-derived build, shim-proof; detects Wine/Proton).
// 2.7.0 = crash event also carries crash_plugin_version + game_build (host exe PE
//         timestamp), pinned at crash time so an mxbikes.exe+offset is interpretable.
// 2.8.0 = added feat_director (auto-director adoption flag) to app_started.
// 2.9.0 = added feat_autoswitch (profile auto-switch adoption flag) to app_started.
// 2.10.0 = remote sampling: a public config file gates session_end + custom events
//          behind aptabase_full_sample (a per-launch fraction); app_started + crash are
//          always sent. Lets Aptabase volume be dialed down without a release (cost lever).
// 2.11.0 = added feat_companion (standalone companion HUD window adoption flag;
//          COMPANION or BOTH display target counts as enabled) to app_started.
constexpr const char* ANALYTICS_SDK_VERSION = "mxbmrp3-analytics@2.11.0";

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
// platform). The version detail goes in osVersion() below.
std::string osName() {
    return "Windows";
}

// Real OS version string for Aptabase's osVersion dimension. Robust against the
// compat-mode shim that makes GetVersionEx/RtlGetVersion report a bogus
// "6.1.7600" (Win7): we read the build straight from the registry, which the
// shim doesn't touch. Also detects Wine/Proton (many PiBoSo players run on
// Linux via Steam Play). Returns "" if nothing could be determined.
//   -> "Windows 11 (22631)", "Windows 10 (19045)", "Wine 9.0 (Linux)", ...
std::string detectOsVersion() {
    // --- Wine / Proton / Steam Deck? ntdll exports wine_get_version only under
    // Wine, which Proton (Steam Play) and the Steam Deck are built on. Refine the
    // label from Steam's environment when present, and fall back to plain "Wine"
    // if those hints are missing (graceful downgrade). Examples:
    //   "Steam Deck [Wine 8.0] (Linux)", "Proton [Wine 8.0] (Linux)", "Wine 9.0 (Linux)"
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
        typedef const char* (__cdecl *wine_get_version_t)(void);
        auto pWineVer = reinterpret_cast<wine_get_version_t>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version")));
        if (pWineVer) {
            const char* wv = pWineVer();   // e.g. "8.0"; guarded below in case it's null

            // Steam sets SteamDeck=1 on the Deck and STEAM_COMPAT_DATA_PATH for any
            // Proton launch; Proton passes these into the Windows env. Best-effort.
            const char* label = "Wine";
            bool refined = false;
            char envBuf[8] = {};
            if (GetEnvironmentVariableA("SteamDeck", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1') {
                label = "Steam Deck"; refined = true;
            } else if (GetEnvironmentVariableA("STEAM_COMPAT_DATA_PATH", nullptr, 0) > 0) {
                label = "Proton"; refined = true;
            }

            std::string out;
            if (!refined) {
                out = (wv && *wv) ? std::string("Wine ") + wv : "Wine";
            } else {
                out = label;
                if (wv && *wv) { out += " [Wine "; out += wv; out += "]"; }
            }

            // wine_get_host_version reports the real host OS (e.g. "Linux", "Darwin").
            typedef void (__cdecl *wine_get_host_version_t)(const char**, const char**);
            auto pHost = reinterpret_cast<wine_get_host_version_t>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_host_version")));
            if (pHost) {
                const char* sysname = nullptr;
                const char* release = nullptr;
                pHost(&sysname, &release);
                if (sysname && *sysname) { out += " ("; out += sysname; out += ")"; }
            }
            return out;
        }
    }

    // --- Native Windows: read the true build from the registry (not shimmed). ---
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
            KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        unsigned long build = 0;
        char buildStr[32] = {};
        DWORD sz = sizeof(buildStr), type = 0;
        if (RegQueryValueExA(hKey, "CurrentBuildNumber", nullptr, &type,
                             reinterpret_cast<LPBYTE>(buildStr), &sz) == ERROR_SUCCESS) {
            build = strtoul(buildStr, nullptr, 10);
        }
        DWORD major = 0; sz = sizeof(major); type = 0;
        RegQueryValueExA(hKey, "CurrentMajorVersionNumber", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&major), &sz);  // absent pre-Win10
        RegCloseKey(hKey);

        // Win 10 and 11 share major 10; 11 is build >= 22000 (ProductName still
        // says "Windows 10", so the build is the only reliable discriminator).
        if (build >= 22000) return "Windows 11 (" + std::to_string(build) + ")";
        if (major == 10 || build >= 10240) return "Windows 10 (" + std::to_string(build) + ")";
        if (build >= 9600) return "Windows 8.1 (" + std::to_string(build) + ")";
        if (build >= 9200) return "Windows 8 (" + std::to_string(build) + ")";
        if (build >= 7600) return "Windows 7 (" + std::to_string(build) + ")";
        if (build > 0)     return "Windows (" + std::to_string(build) + ")";
    }
    return "";
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
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return true;   // capture mode simulates a configured install
#endif
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
    m_prevSessionStart = 0;
    m_sessionStartUnix = epochSecondsNow();   // this launch's start time

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
    // Marker the crash handler writes next to the analytics file on a fault.
    m_pendingCrashPath = dir + "\\pending_crash.json";

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
                // Previous launch's start time — lets us recover a crashed
                // session's duration (crashTime - prevStart) next launch.
                m_prevSessionStart = j.value("sessionStart", 0ULL);
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

    // Persist identity + counters at launch (off-track, once per launch — the file is tiny).
    // Route through the shared atomic writer so a crash mid-write can't corrupt it, consistent
    // with every other persisted file.
    try {
        nlohmann::json j;
        j["installId"] = m_installId;
        j["lastVersion"] = PluginConstants::PLUGIN_VERSION;
        j["firstSeen"] = m_firstSeenUnix;
        j["launchCount"] = m_launchCount;
        j["sessionStart"] = m_sessionStartUnix;   // for next-launch crash-duration recovery
        AtomicFileWriter::writeFileAtomic(path, j.dump(2));
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: failed to write analytics file: %s", e.what());
    }
}

std::string AnalyticsManager::buildEventBody() const {
    using nlohmann::json;

    json sys;
    sys["locale"] = userLocale();
    sys["osName"] = osName();
    if (!m_osVersion.empty()) sys["osVersion"] = m_osVersion;  // shim-proof, incl. Wine
#ifdef _DEBUG
    sys["isDebug"] = true;
#elif defined(MXBMRP3_TEST_BUILD)
    sys["isDebug"] = s_testCaptureMode;   // capture mode → debug bucket (belt-and-suspenders)
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
    // Auto-director (spectate broadcast tool): adoption rate of the feature.
    props["feat_director"] = DirectorManager::getInstance().isEnabled() ? 1 : 0;
    // Profile auto-switch: automatically swap the active HUD profile on context change.
    props["feat_autoswitch"] = ProfileManager::getInstance().isAutoSwitchEnabled() ? 1 : 0;
    props["feat_updates"] = UpdateChecker::getInstance().isEnabled() ? 1 : 0;
    // Master widgets toggle: when off, no widget_* shows regardless of its flag.
    props["feat_widgets"] = HudManager::getInstance().areWidgetsEnabled() ? 1 : 0;
    // Companion window (standalone HUD window): COMPANION or BOTH counts as enabled.
    props["feat_companion"] = (UiConfig::getInstance().getDisplayTarget() != DisplayTarget::IN_GAME) ? 1 : 0;
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
    if (!m_osVersion.empty()) sys["osVersion"] = m_osVersion;
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

std::string AnalyticsManager::buildSessionEventBody(const std::string& eventName,
        long long durationSec, const std::map<std::string, std::string>& extraProps) const {
    using nlohmann::json;

    json sys;
    sys["osName"] = osName();
    if (!m_osVersion.empty()) sys["osVersion"] = m_osVersion;
#ifdef _DEBUG
    sys["isDebug"] = true;
#else
    sys["isDebug"] = false;
#endif
    sys["appVersion"] = PluginConstants::PLUGIN_VERSION;   // reporting-launch version
    sys["sdkVersion"] = ANALYTICS_SDK_VERSION;

    json p;
    p["install_id"] = m_installId;   // same identity context as app_started
    p["game"] = GAME_NAME;
    // duration_seconds as a JSON NUMBER so Aptabase can sum/avg playtime.
    if (durationSec >= 0) p["duration_seconds"] = durationSec;
    // Extra string props (crash fault/code, crash-time plugin/game versions).
    for (const auto& kv : extraProps) if (!kv.second.empty()) p[kv.first] = kv.second;

    json event;
    event["timestamp"] = isoTimestamp();
    event["sessionId"] = m_sessionId;   // group into the launch session
    event["eventName"] = eventName;
    event["systemProps"] = std::move(sys);
    event["props"] = std::move(p);

    json arr = json::array();
    arr.push_back(std::move(event));
    return arr.dump();
}

void AnalyticsManager::sendPendingCrashReport() {
    // Called from initialize() only when Aptabase is active (worker running,
    // m_sessionId set). Reads the crash handler's marker from a previous launch,
    // queues a "crash" event, and deletes the marker so a crash is reported once.
    if (m_pendingCrashPath.empty()) return;
    if (GetFileAttributesA(m_pendingCrashPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;  // no crash since the last launch
    }

    std::string fault, code, pluginVer, gameBuild, host;
    unsigned long long crashTime = 0;
    try {
        std::ifstream in(m_pendingCrashPath);
        if (in.is_open()) {
            nlohmann::json j;
            in >> j;
            fault = j.value("fault", "");
            code  = j.value("code", "");
            pluginVer = j.value("plugin", "");        // plugin version at crash time
            gameBuild = j.value("game_build", "");    // host exe PE timestamp (game build)
            host = j.value("host", "");               // host exe basename (game vs dev tool)
            crashTime = j.value("time", 0ULL);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: unreadable crash marker: %s", e.what());
    }

    // Recover the crashed session's length: crash time minus that session's start
    // (persisted last launch). Omit (-1) if either timestamp is missing/inconsistent.
    long long duration = -1;
    if (crashTime > 0 && m_prevSessionStart > 0 && crashTime >= m_prevSessionStart) {
        duration = static_cast<long long>(crashTime - m_prevSessionStart);
    }

    if (!fault.empty() || !code.empty()) {
        try {
            // Crash-time versions pinned in the marker (the report is sent on a later
            // launch, so sys.appVersion is the reporting version, not the crashed one).
            std::map<std::string, std::string> props = {
                {"fault", fault}, {"code", code},
                {"crash_plugin_version", pluginVer}, {"game_build", gameBuild},
                {"host", host},
            };
            std::string body = buildSessionEventBody("crash", duration, props);
            {
                std::lock_guard<std::mutex> lock(m_eventMutex);
                m_eventQueue.push_back(std::move(body));
            }
            m_eventCv.notify_one();
            DEBUG_INFO_F("AnalyticsManager: reporting previous crash: %s (%s)",
                         fault.c_str(), code.c_str());
        } catch (...) { /* never throw */ }
    }

    // Delete regardless of parse/send outcome so a crash is reported at most once.
    DeleteFileA(m_pendingCrashPath.c_str());
}

void AnalyticsManager::queueSessionEnd() {
    // Track a clean exit's duration. Only when Aptabase is active this launch
    // (session established). Crashed sessions get no session_end - their length is
    // reported by the crash event instead. Caller sets m_shutdownRequested AFTER
    // this and notifies, so the worker drains this event before exiting.
    // Idempotent: shutdown() runs on both explicit teardown and the destructor, so
    // guard on m_shutdownRequested (false on the first real call, true on any later
    // one) to avoid queueing a second session_end the departed worker never sends.
    if (m_shutdownRequested) return;
    if (!m_enabled || m_host.empty() || m_sessionId.empty()) return;
    // Remote cost lever: session_end is one of the two per-launch events, so a "minimal"
    // launch (aptabase_full_sample rolled us out of the full set) drops it. app_started
    // and crash still go out. See applyRemoteSampling().
    if (!m_fullLaunch.load()) return;
    const unsigned long long now = epochSecondsNow();
    const long long duration = (now >= m_sessionStartUnix)
                               ? static_cast<long long>(now - m_sessionStartUnix) : 0;
    try {
        std::string body = buildSessionEventBody("session_end", duration, {});
        {
            std::lock_guard<std::mutex> lock(m_eventMutex);
            m_eventQueue.push_back(std::move(body));
        }
        m_eventCv.notify_one();   // self-contained (shutdown() also notify_all's after)
    } catch (...) { /* never throw on shutdown */ }
}

void AnalyticsManager::trackEvent(const std::string& eventName,
                                  const std::map<std::string, std::string>& props) {
    // Only when analytics is active and app_started has been sent this launch
    // (m_sessionId set), so the event groups into the same Aptabase session.
    if (m_shutdownRequested) return;
    if (!m_enabled || !isConfigured() || m_host.empty() || m_sessionId.empty()) return;
    // Custom events are part of the FULL tier — a minimal launch drops them (they're rare,
    // but the lever is about total event count). app_started + crash are unaffected.
    if (!m_fullLaunch.load()) return;

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

    // Real OS/Wine version (registry-derived, shim-proof) — computed once here.
    m_osVersion = detectOsVersion();

    // --- Aptabase (rich structured events) ---
    std::wstring aptHost;
    std::string aptBody;
    if (aptConfigured && !m_installId.empty()) {
        const char* key = PluginConstants::ANALYTICS_APP_KEY;
        aptHost = AnalyticsEndpoint::aptabaseHostForKey(key);
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

    // If a previous launch crashed, the crash handler left a marker: report it
    // now (fault module+offset, code, recovered duration) under this session.
    // Requires the event worker + session id, so only when Aptabase is active.
    if (haveApt) sendPendingCrashReport();
}

void AnalyticsManager::postBeacon(std::wstring host, std::string body) {
    // Thread entry. Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game. Sends the Aptabase app_started
    // POST (when configured), then the GoatCounter headcount GET — the two are
    // independent so a failure of one never blocks the other.
    try {
        if (m_shutdownRequested) return;
        if (!host.empty() && !body.empty()) {
            // Decide this launch's tier (full vs app_started+crash only) BEFORE the beacon,
            // so a later session_end/custom on the game thread reads a settled m_fullLaunch.
            // app_started itself is sent regardless — it's the always-on minimal tier.
            applyRemoteSampling();
            if (!m_shutdownRequested) postAptabase(host, body);
        }
        if (!m_shutdownRequested) sendGoatCounterHit();
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: beacon thread exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("AnalyticsManager: beacon thread unknown exception");
    }
}

void AnalyticsManager::postAptabase(const std::wstring& host, const std::string& body) {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
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
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
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

void AnalyticsManager::applyRemoteSampling() {
    // Developer cost lever (Aptabase bills per event). Fetch the public config file and
    // read aptabase_full_sample ∈ [0,1]: the fraction of launches that send the FULL set
    // (session_end + custom) on top of the always-sent app_started (+ crash). Roll once to
    // decide THIS launch. Fail-open: any fetch/parse failure leaves sample at 1.0 (full),
    // so a GitHub outage or a typo can never silently blind analytics.
    std::string body;
    double sample = 1.0;
    if (fetchRemoteConfig(body)) {
        sample = AnalyticsRemoteConfig::parseFullSample(body);
    }
    double roll = 0.0;
    if (sample > 0.0 && sample < 1.0) {
        // Only the strictly-between case needs randomness; the 0.0/1.0 endpoints are
        // deterministic (see shouldSendFull), so the binary switch is exact.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        roll = dist(gen);
    }
    const bool full = AnalyticsRemoteConfig::shouldSendFull(sample, roll);
    m_fullLaunch.store(full);
    DEBUG_INFO_F("AnalyticsManager: remote aptabase_full_sample=%.3f -> this launch is %s",
                 sample, full ? "FULL" : "minimal (app_started + crash only)");
}

bool AnalyticsManager::fetchRemoteConfig(std::string& out) {
    out.clear();
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return false;   // dry-run: no fetch (caller fail-opens to full)
#endif

    std::wstring host;
    { const char* h = PluginConstants::ANALYTICS_CONFIG_HOST; host.assign(h, h + std::strlen(h)); }
    // Path: /<owner>/<repo>/<branch>/<file> on raw.githubusercontent.com (public repo).
    std::string pathA = std::string("/") + PluginConstants::GITHUB_REPO_OWNER + "/" +
                        PluginConstants::GITHUB_REPO_NAME + "/" +
                        PluginConstants::ANALYTICS_CONFIG_BRANCH + "/" +
                        PluginConstants::ANALYTICS_CONFIG_FILE;
    std::wstring path(pathA.begin(), pathA.end());   // ASCII path

    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);   // short: never stall startup

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    // Publish handles so shutdown() can cancel this GET (reused by the beacon after).
    {
        std::lock_guard<std::mutex> lock(m_handleMutex);
        m_hSession = hSession; m_hConnect = hConnect; m_hRequest = hRequest;
    }
    if (m_shutdownRequested) { closeHandles(); return false; }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (out.size() < 8192 &&
               WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> buf(avail + 1, 0);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
            out.append(buf.data(), read);
        }
    } else if (!m_shutdownRequested) {
        DEBUG_INFO_F("AnalyticsManager: remote config fetch failed (WinHTTP error %lu)", GetLastError());
    }

    closeHandles();
    // A non-200 (e.g. 404 while the file doesn't exist yet) is a miss -> caller fail-opens.
    if (status != 200) { out.clear(); return false; }
    return !out.empty();
}

void AnalyticsManager::postSync(const std::wstring& host, const std::string& body,
                                unsigned long timeoutMs) {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
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
    // Queue the clean-exit session_end BEFORE requesting shutdown, so the event
    // worker's drain loop still accepts it and flushes it before exiting.
    queueSessionEnd();

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

#if defined(MXBMRP3_TEST_BUILD)
// ============================================================================
// Dry-run capture seam (headless wiring tests). Never compiled into a shipping
// DLL. Drives the payload build + the sampling gate with no network and no
// background threads — see analytics_wiring_test.cpp.
// ============================================================================
void AnalyticsManager::testPrime() {
    // Fake just enough of what initialize() would establish (identity + session +
    // ingest host) that the event-build gates pass and buildEventBody() has an
    // identity — without loadAndUpdateIdentity() (file I/O), the beacon/worker
    // threads, or any network. Capture mode makes the real senders no-ops.
    m_enabled = true;
    m_installId = "test-install-000000000000";
    m_versionStatus = "new";
    m_prevVersion.clear();
    m_launchCount = 1;
    m_firstSeenUnix = epochSecondsNow();
    m_sessionStartUnix = m_firstSeenUnix;
    m_host = L"capture.invalid";        // non-empty → queueSessionEnd()/trackEvent() gates pass
    m_wAppKey = L"A-US-testtesttest";
    m_sessionId = makeSessionId();
    m_shutdownRequested = false;
    m_fullLaunch.store(true);
    m_pendingCrashPath.clear();
    s_testCaptureMode = true;           // real senders become no-ops; isConfigured() → true
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.clear();
}

void AnalyticsManager::testSetFullLaunch(bool full) { m_fullLaunch.store(full); }

std::string AnalyticsManager::testBuildAppStarted() { return buildEventBody(); }

void AnalyticsManager::testQueueSessionEnd() { queueSessionEnd(); }

void AnalyticsManager::testQueueCustom(const std::string& name) { trackEvent(name, {}); }

void AnalyticsManager::testSeedAndReportCrash(const std::string& markerPath,
                                              const std::string& fault, const std::string& code) {
    // Write a minimal crash marker (mirrors the crash handler's), point the manager at it,
    // then run the crash path — which is DELIBERATELY not gated on m_fullLaunch, so it
    // queues even in a minimal launch.
    m_pendingCrashPath = markerPath;
    try {
        std::ofstream out(markerPath, std::ios::trunc);
        nlohmann::json j;
        j["fault"] = fault; j["code"] = code;
        j["plugin"] = "9.9.9"; j["game_build"] = "test"; j["host"] = "test.exe";
        j["time"] = static_cast<unsigned long long>(epochSecondsNow());
        out << j.dump();
    } catch (...) { /* no marker → sendPendingCrashReport() no-ops */ }
    sendPendingCrashReport();
}

std::vector<std::string> AnalyticsManager::testDrainPending() {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    std::vector<std::string> out(m_eventQueue.begin(), m_eventQueue.end());
    m_eventQueue.clear();
    return out;
}
#endif  // MXBMRP3_TEST_BUILD
