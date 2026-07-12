// ============================================================================
// core/steam_friends_manager.cpp
// Steam Friends integration: broadcasts our rich presence and reads friends'
// presence back for the Friends HUD (read + write).
// ============================================================================
#include "steam_friends_manager.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "seh_compat.h"
#include "../diagnostics/logger.h"
#include "../vendor/steam/steam_api_minimal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#include <set>

// ============================================================================
// SEH-protected call wrappers.
//
// Calling into steam_api64.dll through resolved function pointers can fault if
// Steam isn't in the state we assume (e.g. interface not ready). SEH cannot
// share a function with C++ objects that need unwinding (MSVC C2712), so
// every risky call lives in its own POD-returning helper here. Strings
// returned by Steam point into its internal buffers and are only valid until
// the next call on that interface - callers copy them into std::string
// immediately.
//
// SEH is MSVC-only; on the headless cross-platform test build (mingw/GCC) the
// SEH_TRY/SEH_EXCEPT_ALL wrappers run the call unguarded — safe because that
// build has no steam_api64.dll to fault into. See core/seh_compat.h.
// ============================================================================
namespace {

void* sehAccessor(void* fn) {
    SEH_TRY { return reinterpret_cast<void*(S_CALLTYPE*)()>(fn)(); }
    SEH_EXCEPT_ALL { return nullptr; }
}

HSteamUser sehGetHSteamUser(void* fn) {
    SEH_TRY { return reinterpret_cast<SteamAPI_GetHSteamUser_FnPtr>(fn)(); }
    SEH_EXCEPT_ALL { return 0; }
}

HSteamPipe sehGetHSteamPipe(void* fn) {
    SEH_TRY { return reinterpret_cast<SteamAPI_GetHSteamPipe_FnPtr>(fn)(); }
    SEH_EXCEPT_ALL { return 0; }
}

void* sehFindInterface(void* fn, HSteamUser user, const char* version) {
    SEH_TRY { return reinterpret_cast<SteamInternal_FindOrCreateUserInterface_FnPtr>(fn)(user, version); }
    SEH_EXCEPT_ALL { return nullptr; }
}

void* sehClientGetFriends(void* fn, void* client, HSteamUser user, HSteamPipe pipe, const char* version) {
    SEH_TRY { return reinterpret_cast<ISteamClient_GetFriends_FnPtr>(fn)(client, user, pipe, version); }
    SEH_EXCEPT_ALL { return nullptr; }
}

void* sehClientGetUtils(void* fn, void* client, HSteamPipe pipe, const char* version) {
    SEH_TRY { return reinterpret_cast<ISteamClient_GetUtils_FnPtr>(fn)(client, pipe, version); }
    SEH_EXCEPT_ALL { return nullptr; }
}

uint32_t sehGetAppID(void* fn, void* self) {
    SEH_TRY { return reinterpret_cast<ISteamUtils_GetAppID_FnPtr>(fn)(self); }
    SEH_EXCEPT_ALL { return 0; }
}

int sehGetFriendCount(void* fn, void* self, int flags) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendCount_FnPtr>(fn)(self, flags); }
    SEH_EXCEPT_ALL { return -1; }
}

uint64_t sehGetFriendByIndex(void* fn, void* self, int i, int flags) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendByIndex_FnPtr>(fn)(self, i, flags); }
    SEH_EXCEPT_ALL { return 0; }
}

const char* sehGetPersonaName(void* fn, void* self, uint64_t id) {
    // fn can legitimately be null: GetFriendPersonaName is not part of the
    // required-exports check in hookSteamApi(). The SEH guard would catch the
    // null call too, but don't rely on a deliberate AV.
    if (!fn) return nullptr;
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendPersonaName_FnPtr>(fn)(self, id); }
    SEH_EXCEPT_ALL { return nullptr; }
}

bool sehGetGamePlayed(void* fn, void* self, uint64_t id, FriendGameInfo_t* out) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendGamePlayed_FnPtr>(fn)(self, id, out); }
    SEH_EXCEPT_ALL { return false; }
}

const char* sehGetRichPresence(void* fn, void* self, uint64_t id, const char* key) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendRichPresence_FnPtr>(fn)(self, id, key); }
    SEH_EXCEPT_ALL { return nullptr; }
}

int sehGetRPKeyCount(void* fn, void* self, uint64_t id) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendRichPresenceKeyCount_FnPtr>(fn)(self, id); }
    SEH_EXCEPT_ALL { return -1; }
}

const char* sehGetRPKeyByIndex(void* fn, void* self, uint64_t id, int i) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_GetFriendRichPresenceKeyByIndex_FnPtr>(fn)(self, id, i); }
    SEH_EXCEPT_ALL { return nullptr; }
}

bool sehSetRichPresence(void* fn, void* self, const char* key, const char* value) {
    SEH_TRY { return reinterpret_cast<ISteamFriends_SetRichPresence_FnPtr>(fn)(self, key, value); }
    SEH_EXCEPT_ALL { return false; }
}

void sehClearRichPresence(void* fn, void* self) {
    SEH_TRY { reinterpret_cast<ISteamFriends_ClearRichPresence_FnPtr>(fn)(self); }
    SEH_EXCEPT_ALL {}
}

// Copy a Steam-owned string immediately (may be null / invalidated by next call)
std::string copyStr(const char* s) { return s ? std::string(s) : std::string(); }

// The read path's canary: does GetFriendCount work on this candidate interface?
// A version whose vtable layout doesn't match the dll's flat SteamAPI_ISteamFriends_*
// wrappers will fault here (caught -> -1) even though acquisition returned a
// non-null pointer. A matching version returns a count >= 0 (0 is fine).
bool friendsReadable(void* fnGetFriendCount, void* iface) {
    if (!iface || !fnGetFriendCount) return false;
    return sehGetFriendCount(fnGetFriendCount, iface, STEAM_FRIENDFLAG_IMMEDIATE) >= 0;
}

// Acquire ISteamFriends and validate the read path. We can't know which
// interface version string matches the dll's flat wrappers, so we try each
// (via the SteamClient getter, then FindOrCreateUserInterface, then the legacy
// accessor) and accept the first whose GetFriendCount actually works. If none
// validate we return the first non-null candidate anyway via *outReadable=false
// so the write path can still function.
void* acquireFriends(HMODULE hSteam, void* client, HSteamUser user, HSteamPipe pipe,
                     void* fnFind, void* fnGetFriendCount,
                     const char* const* versions, bool* outReadable) {
    void* fallback = nullptr;
    void* clientGetter = client
        ? reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamClient_GetISteamFriends"))
        : nullptr;

    auto consider = [&](void* iface, const char* desc) -> void* {
        if (!iface) return nullptr;
        if (!fallback) fallback = iface;
        if (friendsReadable(fnGetFriendCount, iface)) {
            DEBUG_INFO_F("SteamFriendsManager: ISteamFriends validated via %s", desc);
            *outReadable = true;
            return iface;
        }
        DEBUG_INFO_F("SteamFriendsManager: %s acquired but GetFriendCount faulted - trying next", desc);
        return nullptr;
    };

    char desc[64];
    for (int i = 0; versions[i] != nullptr; ++i) {
        if (clientGetter) {
            snprintf(desc, sizeof(desc), "SteamClient(%s)", versions[i]);
            if (void* ok = consider(sehClientGetFriends(clientGetter, client, user, pipe, versions[i]), desc)) return ok;
        }
        if (fnFind && user != 0) {
            snprintf(desc, sizeof(desc), "FindOrCreate(%s)", versions[i]);
            if (void* ok = consider(sehFindInterface(fnFind, user, versions[i]), desc)) return ok;
        }
    }

    // Legacy unversioned accessor as a last resort.
    void* fnLegacy = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamFriends"));
    if (!fnLegacy) fnLegacy = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_SteamFriends"));
    if (fnLegacy) {
        if (void* ok = consider(sehAccessor(fnLegacy), "legacy accessor")) return ok;
    }

    *outReadable = false;
    return fallback;  // write may still work even if reads don't
}

} // namespace

// ============================================================================
// Singleton / lifecycle
// ============================================================================

SteamFriendsManager& SteamFriendsManager::getInstance() {
    static SteamFriendsManager instance;
    return instance;
}

SteamFriendsManager::SteamFriendsManager()
    : m_status(Status::NOT_INITIALIZED)
    , m_enabled(true)
    , m_friends(nullptr)
    , m_utils(nullptr)
    , m_appId(0)
    , m_readsValidated(false)
    , m_fnGetFriendCount(nullptr)
    , m_fnGetFriendByIndex(nullptr)
    , m_fnGetFriendPersonaName(nullptr)
    , m_fnGetFriendGamePlayed(nullptr)
    , m_fnGetFriendRichPresence(nullptr)
    , m_fnGetRPKeyCount(nullptr)
    , m_fnGetRPKeyByIndex(nullptr)
    , m_fnSetRichPresence(nullptr)
    , m_fnClearRichPresence(nullptr)
    , m_lastScan(std::chrono::steady_clock::time_point{})
    , m_lastHookAttempt(std::chrono::steady_clock::time_point{})
{
}

SteamFriendsManager::~SteamFriendsManager() {
    shutdown();
}

void SteamFriendsManager::initialize() {
    // Default on (Steam rich presence is expected for a Steam game): hooks and
    // broadcasts on first run unless the user has turned the integration off.
    // setEnabled() (the General toggle, or a saved "off") gates everything below.
    if (!m_enabled) {
        return;
    }
    if (m_status != Status::NOT_INITIALIZED) {
        return;
    }

    m_lastHookAttempt = std::chrono::steady_clock::now();

    if (!hookSteamApi()) {
        // Not fatal: Steam may simply not be ready yet at plugin-load time.
        // onDataChanged() retries the hook (throttled) so init ordering can't
        // strand us in a permanent false "not available".
        DEBUG_INFO_F("SteamFriendsManager: not connected at startup (%s); will retry on data changes",
                     getStatusString());
        return;
    }

    onConnected();
}

void SteamFriendsManager::onConnected() {
    m_status = Status::CONNECTED;
    DEBUG_INFO_F("SteamFriendsManager: connected, AppID=%u (0 = unknown, friends not filtered by game)",
                 m_appId);

    // Push initial presence and do a first scan so the log shows state right away.
    updateLocalPresence();
    scanFriends();
    m_lastScan = std::chrono::steady_clock::now();
}

void SteamFriendsManager::shutdown() {
    if (m_status == Status::CONNECTED && m_friends && m_fnClearRichPresence) {
        sehClearRichPresence(m_fnClearRichPresence, m_friends);
    }
    // We never owned steam_api64.dll - the game does - so no FreeLibrary.
    m_status = Status::NOT_INITIALIZED;
    m_friends = nullptr;
    m_utils = nullptr;
    m_readsValidated = false;

    // Clear cached change-detection state so a later re-initialize() republishes
    // presence and re-scans instead of short-circuiting on stale values.
    m_lastLocalStatus.clear();
    m_hasPresenceInputs = false;
    m_lastRosterSig.clear();
    m_roster.clear();
    m_lastScan = std::chrono::steady_clock::time_point{};
    m_lastHookAttempt = std::chrono::steady_clock::time_point{};
}

void SteamFriendsManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;

    if (enabled) {
        // Turn on: hook now (if not already) and broadcast immediately so the
        // toggle takes effect without waiting for the next data change. If Steam
        // isn't ready yet, onDataChanged() keeps retrying the hook on a throttle.
        if (m_status == Status::CONNECTED) {
            updateLocalPresence();
            scanFriends();
            m_lastScan = std::chrono::steady_clock::now();
        } else {
            m_status = Status::NOT_INITIALIZED;  // allow hookSteamApi() to run
            m_lastHookAttempt = std::chrono::steady_clock::now();
            if (hookSteamApi()) {
                onConnected();
            }
        }
    } else {
        // Turn off: stop broadcasting so friends no longer see us.
        if (m_status == Status::CONNECTED && m_friends && m_fnClearRichPresence) {
            sehClearRichPresence(m_fnClearRichPresence, m_friends);
        }
        // Drop change-detection caches so a later re-enable republishes from
        // scratch instead of short-circuiting on stale values.
        m_lastLocalStatus.clear();
        m_hasPresenceInputs = false;
        m_lastRosterSig.clear();
        m_roster.clear();
    }
}

// ============================================================================
// Hooking
// ============================================================================

bool SteamFriendsManager::isSteamRuntimeAvailable() {
    return GetModuleHandleA("steam_api64.dll") != nullptr
        || GetModuleHandleA("steam_api.dll") != nullptr;
}

bool SteamFriendsManager::hookSteamApi() {
    HMODULE hSteam = GetModuleHandleA("steam_api64.dll");
    if (!hSteam) hSteam = GetModuleHandleA("steam_api.dll");
    if (!hSteam) {
        m_status = Status::NOT_AVAILABLE;
        DEBUG_INFO("SteamFriendsManager: steam_api64.dll not loaded (game launched without Steam?)");
        return false;
    }

    // Resolve the entry points used by the three acquisition paths. A valid
    // HSteamUser also tells us Steam is actually initialized, not merely mapped.
    void* fnGetHSteamUser = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_GetHSteamUser"));
    void* fnGetHSteamPipe = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_GetHSteamPipe"));
    void* fnFind          = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamInternal_FindOrCreateUserInterface"));
    void* fnSteamClient   = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamClient"));
    if (!fnSteamClient) fnSteamClient = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_SteamClient"));

    HSteamUser user = fnGetHSteamUser ? sehGetHSteamUser(fnGetHSteamUser) : 0;
    HSteamPipe pipe = fnGetHSteamPipe ? sehGetHSteamPipe(fnGetHSteamPipe) : 0;
    void* client = fnSteamClient ? sehAccessor(fnSteamClient) : nullptr;

    // One diagnostic line that pins down exactly what's available, so a hook
    // failure is self-explanatory on the next run.
    DEBUG_INFO_F("SteamFriendsManager: entry points client=%s user=%d pipe=%d find=%s "
                 "(GetHSteamUser=%s GetHSteamPipe=%s SteamClient=%s)",
                 client ? "ok" : "null", user, pipe, fnFind ? "ok" : "missing",
                 fnGetHSteamUser ? "ok" : "missing",
                 fnGetHSteamPipe ? "ok" : "missing",
                 fnSteamClient ? "ok" : "missing");

    if (fnGetHSteamUser && user == 0) {
        // We can read the user handle and it's zero: Steam isn't initialized yet.
        m_status = Status::NOT_AVAILABLE;
        DEBUG_INFO("SteamFriendsManager: Steam loaded but not initialized (HSteamUser=0)");
        return false;
    }

    // Resolve the flat ISteamFriends functions first - acquireFriends() needs
    // GetFriendCount to validate candidate interfaces. Read path is the whole
    // point here; the write path lets two instances exchange data.
    m_fnGetFriendCount        = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendCount"));
    m_fnGetFriendByIndex      = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendByIndex"));
    m_fnGetFriendPersonaName  = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendPersonaName"));
    m_fnGetFriendGamePlayed   = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendGamePlayed"));
    m_fnGetFriendRichPresence = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendRichPresence"));
    m_fnGetRPKeyCount         = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount"));
    m_fnGetRPKeyByIndex       = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex"));
    m_fnSetRichPresence       = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_SetRichPresence"));
    m_fnClearRichPresence     = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamFriends_ClearRichPresence"));

    if (!m_fnGetFriendCount || !m_fnGetFriendByIndex || !m_fnGetFriendRichPresence) {
        m_status = Status::HOOK_FAILED;
        DEBUG_WARN("SteamFriendsManager: required flat read functions not exported");
        return false;
    }

    static const char* const kFriendsVersions[] = STEAM_FRIENDS_VERSIONS;
    static const char* const kUtilsVersions[]   = STEAM_UTILS_VERSIONS;

    // Acquire + validate ISteamFriends (auto-selects the version whose vtable
    // matches the flat wrappers, using GetFriendCount as the canary).
    bool readable = false;
    m_friends = acquireFriends(hSteam, client, user, pipe, fnFind,
                               m_fnGetFriendCount, kFriendsVersions, &readable);
    m_readsValidated = readable;
    if (!m_friends) {
        m_status = Status::HOOK_FAILED;
        DEBUG_WARN("SteamFriendsManager: could not acquire ISteamFriends interface (see entry points above)");
        return false;
    }
    DEBUG_INFO_F("SteamFriendsManager: ISteamFriends @ %p (read path %s)",
                 m_friends, readable ? "validated" : "FAULTED on all versions - write-only");

    // ISteamUtils (optional, AppID filter only). Its getter takes (pipe, version)
    // - no user handle - unlike GetISteamFriends.
    void* utilsGetter = client
        ? reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamClient_GetISteamUtils"))
        : nullptr;
    if (utilsGetter) {
        for (int i = 0; kUtilsVersions[i] != nullptr && !m_utils; ++i) {
            m_utils = sehClientGetUtils(utilsGetter, client, pipe, kUtilsVersions[i]);
            if (m_utils) DEBUG_INFO_F("SteamFriendsManager: ISteamUtils via SteamClient (%s)", kUtilsVersions[i]);
        }
    }
    if (!m_utils && fnFind && user != 0) {
        for (int i = 0; kUtilsVersions[i] != nullptr && !m_utils; ++i) {
            m_utils = sehFindInterface(fnFind, user, kUtilsVersions[i]);
        }
    }
    if (m_utils) {
        auto fnGetAppID = reinterpret_cast<void*>(GetProcAddress(hSteam, "SteamAPI_ISteamUtils_GetAppID"));
        if (fnGetAppID) m_appId = sehGetAppID(fnGetAppID, m_utils);
    }
    DEBUG_INFO_F("SteamFriendsManager: utils=%s appId=%u", m_utils ? "ok" : "none", m_appId);

    return true;
}

// ============================================================================
// Write our own presence
// ============================================================================

void SteamFriendsManager::updateLocalPresence() {
    if (m_status != Status::CONNECTED || !m_friends || !m_fnSetRichPresence) {
        return;
    }

    // Mirror DiscordManager::buildPresenceJson so in-game, Discord and Steam all
    // read identically. We publish structured keys (the Friends HUD formats them
    // itself) plus a human-readable "status" one-liner.
    const PluginData& pd = PluginData::getInstance();
    const SessionData& session = pd.getSessionData();
    const int drawState = pd.getDrawState();  // 0=ON_TRACK, 1=SPECTATE, 2=REPLAY

    // Fingerprint the raw inputs and skip the whole string build when nothing
    // changed - this runs on every Standings notify (many per second on a
    // full grid) and otherwise allocates ~10 strings before the write-dedup
    // below can kick in. See PresenceInputs in the header.
    PresenceInputs inputs;
    strncpy_s(inputs.trackName, sizeof(inputs.trackName), session.trackName, _TRUNCATE);
    strncpy_s(inputs.serverName, sizeof(inputs.serverName), session.serverName, _TRUNCATE);
    inputs.drawState = drawState;
    inputs.eventType = session.eventType;
    inputs.session = session.session;
    inputs.sessionState = session.sessionState;
    inputs.sessionLength = session.sessionLength;
    inputs.sessionNumLaps = session.sessionNumLaps;
    inputs.online = session.isOnline();
    // >1 rider flips the unknown-serverType label to "Online" (GP Bikes / KRP), so a
    // rider joining/leaving must invalidate the fingerprint or the label would stick.
    const int riderCount = static_cast<int>(pd.getRaceEntries().size());
    inputs.multiRider = (riderCount > 1);
    inputs.lapsToGo = pd.getLeaderLapsToGo();
    inputs.timeSeconds = pd.getSessionTime() / 1000;
    if (m_hasPresenceInputs && inputs == m_lastPresenceInputs) {
        return;
    }
    m_lastPresenceInputs = inputs;
    m_hasPresenceInputs = true;

    std::string status;       // human one-liner, e.g. "Club MX \xC2\xB7 Race 1 (8:00 + 6L, In Progress) \xC2\xB7 myServer"
    std::string track;        // structured keys for the reading side
    std::string server;
    std::string sessionStr;
    std::string stateStr;
    std::string fmt;          // session format, e.g. "8:00 + 6L" (its own key for the Friends HUD)
    std::string progress;     // session clock snapshot for the Friends HUD "Timing" column
    int lapsToGo = -1;        // leader laps-to-go (>=0 in time+lap overtime); drives the sig
    int timeBucket = 0;       // coarse 10s session-time bucket so the clock steps without spamming

    if (session.trackName[0] == '\0') {
        status = LABEL_IN_MENUS;
    } else {
        track = session.trackName;

        if (drawState == 2) {
            sessionStr = "Replay";
            status = track + " \xC2\xB7 Watching Replay";
        } else {
            const char* st = (session.session >= 0)
                ? PluginUtils::getSessionString(session.eventType, session.session) : nullptr;
            const char* ss = (session.sessionState >= 0)
                ? PluginUtils::getSessionStateString(session.sessionState) : nullptr;
            if (st) sessionStr = st;
            if (ss) stateStr = ss;

            // Session format (time / laps) - shared helper, one canonical string.
            char fb[32];
            PluginUtils::formatSessionFormat(session.sessionLength, session.sessionNumLaps, fb, sizeof(fb));
            fmt = fb;

            // Server slot label (name / "Testing" solo / "Online" / "Unknown") shared
            // with the SessionHud server row - see PluginUtils::serverLabel. Feeds the
            // published "server" key, the native Steam string, and our "Show myself"
            // row (m_self.server below); the Friends HUD shows it in the Server column.
            // Rider count resolves GP Bikes / KRP (no serverType in their API) to
            // "Online" once a real opponent is present. Each client publishes its own
            // resolved label, so a friend reading side just displays what was sent.
            server = PluginUtils::serverLabel(session.serverType, session.serverName, riderCount);

            // Session clock snapshot for the Friends HUD "Timing" column - the
            // exact value the web overlay's `time` field shows (MM:SS, or the
            // N TO GO / FINAL LAP / CHECKERED overtime label). Coarse, not live:
            // the 10s bucket below keeps it out of the per-second write path.
            lapsToGo = pd.getLeaderLapsToGo();
            char clockBuf[16];
            PluginUtils::formatSessionClock(lapsToGo, pd.getSessionTime(), clockBuf, sizeof(clockBuf));
            progress = clockBuf;
            timeBucket = pd.getSessionTime() / 10000;

            // Mirror the Friends HUD layout (Server | Track | Info) as one
            // "\xC2\xB7"-joined line - the native Steam UI renders the middle dot fine
            // (the in-game font uses commas instead):
            //   "Server \xC2\xB7 Track \xC2\xB7 Session (Format), State"
            // State is grouped with the session by a comma, matching the Friends
            // Info column and the Discord detail line.
            auto append = [&](const std::string& part) {
                if (part.empty()) return;
                if (!status.empty()) status += " \xC2\xB7 ";
                status += part;
            };
            append(server);
            append(track);
            // Drop the session name from the detail when it's already the server slot
            // (a solo "Testing" session would otherwise show it twice in the native
            // Steam line) - mirrors the SessionHud server row + format row.
            std::string sessPart = (st && server != st) ? std::string(st) : std::string();
            if (!fmt.empty()) sessPart = sessPart.empty() ? ("(" + fmt + ")") : (sessPart + " (" + fmt + ")");
            if (ss && (!st || strcmp(st, ss) != 0)) {       // state, avoid "Race, Race"
                if (!sessPart.empty()) sessPart += ", ";
                sessPart += ss;
            }
            append(sessPart);
        }
    }

    // Mirror the structured fields into our own SteamFriend row so the HUD can
    // show us ("Show myself") from the exact data we just derived - single source
    // of truth, no separate self-derivation to drift. Updated before the dedup
    // return so it's always current even when the broadcast is skipped.
    m_self.name       = "You";
    m_self.status     = status;
    m_self.track      = track;
    m_self.server     = server;
    m_self.session    = sessionStr;
    m_self.state      = stateStr;
    m_self.format     = fmt;
    m_self.progress   = progress;
    m_self.hasData    = !(track.empty() && server.empty() && sessionStr.empty() && status.empty());
    m_self.sameServer = false;  // never badge our own row (you're not your own friend)

    // Dedup on the combined signature so a change to ANY field triggers a rewrite.
    // The progress clock is folded in only as its overtime label (lapsToGo) plus a
    // coarse 10s time bucket - never the raw MM:SS - so the per-second countdown
    // can't drive a write storm against Steam's update budget.
    std::string sig = status + '\x1f' + track + '\x1f' + server + '\x1f' + sessionStr + '\x1f' + stateStr
                    + '\x1f' + std::to_string(lapsToGo) + '\x1f' + std::to_string(timeBucket);
    if (sig == m_lastLocalStatus) {
        return;  // nothing changed, don't spam Steam
    }
    m_lastLocalStatus = std::move(sig);

    // Setting a key to "" removes it, so stale track/server clear on return to menus.
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "status",  status.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "track",   track.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "server",  server.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "session", sessionStr.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "state",   stateStr.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "format",  fmt.c_str());
    sehSetRichPresence(m_fnSetRichPresence, m_friends, "progress", progress.c_str());

    // Log the first write of each session in all builds (a useful field breadcrumb
    // that presence is being published), but keep the every-~10s repeats the session
    // clock drives Debug-only so Release stays quiet.
#ifdef _DEBUG
    const bool logThisWrite = true;
#else
    const bool logThisWrite = (session.sessionGeneration != m_lastLoggedSessionGen);
#endif
    m_lastLoggedSessionGen = session.sessionGeneration;
    if (logThisWrite) {
        DEBUG_INFO_F("SteamFriendsManager: wrote presence status='%s' track='%s' server='%s' session='%s' state='%s' progress='%s'",
                     status.c_str(),
                     track.empty()      ? "(none)" : track.c_str(),
                     server.empty()     ? "(none)" : server.c_str(),
                     sessionStr.empty() ? "(none)" : sessionStr.c_str(),
                     stateStr.empty()   ? "(none)" : stateStr.c_str(),
                     progress.empty()   ? "(none)" : progress.c_str());
    }
}

// ============================================================================
// Read friends
// ============================================================================

void SteamFriendsManager::scanFriends() {
    if (m_status != Status::CONNECTED || !m_friends) {
        return;
    }
    if (!m_readsValidated) {
        // Acquisition couldn't find an interface version whose GetFriendCount
        // works; the write path is up but reads would just fault. Already logged
        // at connect time - don't spam the scan.
        return;
    }

    const int count = sehGetFriendCount(m_fnGetFriendCount, m_friends, STEAM_FRIENDFLAG_IMMEDIATE);
    if (count < 0) {
        DEBUG_WARN("SteamFriendsManager: GetFriendCount faulted");
        return;
    }

    const bool canCheckGame = (m_fnGetFriendGamePlayed != nullptr);

    // Build the roster into a buffer + a content signature, then only emit it
    // when the signature changes. The scan runs every ~10s during a session;
    // logging unconditionally would flood the log with identical rosters.
    std::vector<std::string> lines;
    std::string sig;
    char buf[640];
    int reported = 0;
    int playingTotal = 0;

    // Our own session, for the same-server badge. Match on server + track; an
    // empty local server (offline) never matches.
    const SessionData& localSession = PluginData::getInstance().getSessionData();
    const std::string localServer = localSession.isOnline() ? localSession.serverName : std::string();
    const std::string localTrack  = localSession.trackName;

    std::vector<SteamFriend> roster;  // rebuilt every scan; the HUD reads this

    for (int i = 0; i < count; ++i) {
        const uint64_t id = sehGetFriendByIndex(m_fnGetFriendByIndex, m_friends, i, STEAM_FRIENDFLAG_IMMEDIATE);
        if (id == 0) continue;

        // Is this friend in our game? Only knowable if GetFriendGamePlayed is
        // present. With an unknown AppID we accept any game they're in.
        FriendGameInfo_t gi{};
        const bool playing = canCheckGame && sehGetGamePlayed(m_fnGetFriendGamePlayed, m_friends, id, &gi);
        const uint32_t friendApp = static_cast<uint32_t>(gi.m_gameID & 0xFFFFFFFFull);
        const bool inOurGame = playing && (m_appId == 0 || friendApp == m_appId);

        // How many rich-presence keys Steam has synced for this friend. This is
        // also the fallback signal when we can't query game membership.
        const int keyCount = (m_fnGetRPKeyCount && m_fnGetRPKeyByIndex)
            ? sehGetRPKeyCount(m_fnGetRPKeyCount, m_friends, id) : -1;

        // Report friends in our game; if we can't check game membership at all,
        // fall back to any friend that has published rich-presence keys so we
        // still surface data instead of going silent.
        const bool report = inOurGame || (!canCheckGame && keyCount > 0);

        // Friends not in any game and not reportable are noise - skip entirely.
        if (!playing && !report) continue;

        // Persona name and all rich-presence values are attacker-controlled -
        // sanitize before they touch the log (or, later, the HUD).
        const std::string name = PluginUtils::sanitizeUntrusted(sehGetPersonaName(m_fnGetFriendPersonaName, m_friends, id));

        // Verbose roster: every friend currently in *any* game. This exercises the
        // whole read chain (GetFriendByIndex / PersonaName / GamePlayed /
        // RPKeyCount) even when nobody is in MX Bikes, so a solo run still
        // confirms reads work.
        if (playing) {
            ++playingTotal;
            snprintf(buf, sizeof(buf), "  [in-game] '%s' app=%u keys=%d%s",
                     name.c_str(), friendApp, keyCount,
                     (friendApp == m_appId) ? "  <-- MX Bikes" : "");
            lines.emplace_back(buf);
            sig += buf;
        }

        if (!report) continue;
        ++reported;

        // Pull the two keys we care about, plus a full key dump so we can see
        // exactly what the other plugin instance published.
        const std::string track  = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "track"));
        const std::string server = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "server"));

        // Remaining structured keys for the HUD roster.
        SteamFriend fe;
        fe.name    = name;
        fe.track   = track;
        fe.server  = server;
        fe.session = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "session"));
        fe.state   = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "state"));
        fe.format  = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "format"));
        fe.progress = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "progress"));
        fe.status  = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, "status"));
        // hasData = published any plugin key (incl. "status", so a friend sitting in
        // menus counts as running the plugin rather than reading as "Unknown").
        fe.hasData = !(fe.track.empty() && fe.server.empty() && fe.session.empty() && fe.status.empty());
        fe.sameServer = !server.empty() && !localServer.empty()
                        && server == localServer && track == localTrack;
        roster.push_back(std::move(fe));

        snprintf(buf, sizeof(buf), "  friend '%s' (id=%llu, gameID=0x%llX) track='%s' server='%s'",
                 name.c_str(),
                 static_cast<unsigned long long>(id),
                 static_cast<unsigned long long>(gi.m_gameID),
                 track.empty() ? "(empty)" : track.c_str(),
                 server.empty() ? "(empty)" : server.c_str());
        lines.emplace_back(buf);
        sig += buf;

#ifdef _DEBUG
        // Debug-only: full rich-presence key dump (tells us if keys arrive under
        // other names or not at all). Verbose + redundant with the typed reads
        // above, so it's kept out of Release logs and the Release read path.
        if (keyCount >= 0) {
            snprintf(buf, sizeof(buf), "    rich-presence keys: %d", keyCount);
            lines.emplace_back(buf);
            for (int k = 0; k < keyCount; ++k) {
                // Keep the raw key for the value lookup; sanitize only for display.
                const std::string rawKey = copyStr(sehGetRPKeyByIndex(m_fnGetRPKeyByIndex, m_friends, id, k));
                const std::string dispKey = PluginUtils::sanitizeUntrusted(rawKey.c_str(), 64);
                const std::string val = PluginUtils::sanitizeUntrusted(sehGetRichPresence(m_fnGetFriendRichPresence, m_friends, id, rawKey.c_str()));
                snprintf(buf, sizeof(buf), "      [%d] %s = %s", k, dispKey.c_str(), val.c_str());
                lines.emplace_back(buf);
                sig += buf;
            }
        }
#endif
    }

    // "On Join" activity for the Friends HUD: a friend newly in our game, or one
    // that moved to a new server, refreshes the activity timestamp. The user's
    // own session/timing changes don't (they don't change any friend's name or
    // server). A friend leaving doesn't count either. Compared against the prior
    // roster before we overwrite it.
    {
        std::set<std::string> prevKeys;
        for (const SteamFriend& f : m_roster) prevKeys.insert(f.name + '|' + f.server);
        for (const SteamFriend& f : roster) {
            if (!prevKeys.count(f.name + '|' + f.server)) {
                m_lastActivityTime = std::chrono::steady_clock::now();
                break;
            }
        }
    }

    // Publish the roster to the HUD every scan (independent of the log throttle
    // below, which only governs whether we re-emit the verbose log).
    m_roster = std::move(roster);

    // Fold the counts in so an appear/disappear with no other content change
    // still re-logs.
    snprintf(buf, sizeof(buf), "|%d|%d|%d", count, playingTotal, reported);
    sig += buf;

    if (sig == m_lastRosterSig) {
        return;  // roster unchanged since last scan - stay quiet
    }
    m_lastRosterSig = std::move(sig);

    DEBUG_INFO_F("=== SteamFriendsManager scan: %d friends (AppID=%u%s) ===",
                 count, m_appId,
                 canCheckGame ? "" : ", game filter unavailable - showing friends with keys");
    for (const std::string& line : lines) {
        DEBUG_INFO_F("%s", line.c_str());
    }
    DEBUG_INFO_F("=== SteamFriendsManager scan done: %d in a game, %d in MX Bikes ===",
                 playingTotal, reported);
}

// ============================================================================
// Data-change hook (game thread)
// ============================================================================

void SteamFriendsManager::onDataChanged(DataChangeType changeType) {
    // Disabled: do nothing - no presence broadcast, no friend scan, no hook retry.
    if (!m_enabled) {
        return;
    }

    // Once shut down for good, stay down (avoid resurrecting on late callbacks).
    if (m_status == Status::NOT_INITIALIZED) {
        return;
    }

    // Mirror DiscordManager: only react to coarse session/standings changes,
    // ignore high-frequency telemetry.
    switch (changeType) {
        case DataChangeType::SessionData:
        case DataChangeType::Standings:
        case DataChangeType::SpectateTarget:
            break;
        default:
            return;
    }

    const auto now = std::chrono::steady_clock::now();

    // Not connected yet: Steam may not have been ready at plugin load. Retry the
    // hook on a throttle so we recover instead of reporting a false negative.
    if (m_status != Status::CONNECTED) {
        const auto sinceTry = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastHookAttempt).count();
        if (sinceTry < SCAN_INTERVAL_MS) {
            return;
        }
        m_lastHookAttempt = now;
        m_status = Status::NOT_INITIALIZED;  // allow hookSteamApi() to run again
        if (!hookSteamApi()) {
            return;  // hookSteamApi() set the failure status
        }
        onConnected();
        return;  // next data change proceeds on the normal path
    }

    updateLocalPresence();

    // Throttle the read scan so the log isn't flooded.
    const auto sinceScan = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastScan).count();
    if (sinceScan >= SCAN_INTERVAL_MS) {
        m_lastScan = now;
        scanFriends();
    }
}

const char* SteamFriendsManager::getStatusString() const {
    switch (m_status) {
        case Status::NOT_INITIALIZED: return "Not Initialized";
        case Status::CONNECTED:       return "Connected";
        case Status::NOT_AVAILABLE:   return "Steam Not Available";
        case Status::HOOK_FAILED:     return "Hook Failed";
        default:                      return "Unknown";
    }
}
