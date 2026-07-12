// ============================================================================
// core/steam_friends_manager.h
// Steam Friends integration backing the Friends HUD.
//
// Broadcasts our own rich presence (track/server/session/state/format/progress
// + a human-readable status one-liner) and reads friends' presence back, so the
// Friends HUD can show which friends are in the same game and where. Two plugin
// instances (you + a friend, both in MX Bikes, both Steam friends) exchange
// rich-presence keys. Opt-in via a global toggle, mirroring DiscordManager.
//
// Design notes:
//  - We do NOT call SteamAPI_Init(). The game already initialized Steam; we
//    hook its loaded steam_api64.dll via GetModuleHandle + GetProcAddress and
//    piggyback on the game's callback pump (no SteamAPI_RunCallbacks here).
//  - All Steam calls happen on the game thread (from onDataChanged), where
//    PluginData is safe to read. No background thread needed - this is far
//    simpler than DiscordManager's threaded model and avoids the races it
//    has to guard against.
//  - Risky flat-API calls are wrapped in SEH helpers (.cpp) that return PODs,
//    because __try/__except cannot live in a function with C++ unwinding.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <chrono>
#include <vector>

// Forward declaration (avoid pulling plugin_data.h into the header)
enum class DataChangeType;

// One friend in our game, as the Friends HUD consumes it. All strings are the
// sanitized rich-presence values the friend published (empty when absent). The
// HUD does the display formatting (Where = server -> session -> Unknown, etc.).
struct SteamFriend {
    std::string name;      // Steam persona name (sanitized)
    std::string status;    // human one-liner ("In Menus" / "Track - Session ..."); present iff running our plugin
    std::string server;    // server name; empty when offline (solo)
    std::string track;     // track name
    std::string session;   // "Race 1" / "Open Practice" / "Testing" / "Replay"
    std::string state;     // session state, e.g. "In Progress" / "Waiting"
    std::string format;    // session format, e.g. "8:00 + 6L"
    std::string progress;  // session clock snapshot: "MM:SS" / "N TO GO" / "FINAL LAP" / "CHECKERED"
    bool hasData = false;  // false = in our game but published no plugin keys -> "Unknown"
    bool sameServer = false; // on the same server + track as us
};

class SteamFriendsManager {
public:
    // Presence labels for a friend who isn't in a session - shared by the
    // broadcast status and the Friends HUD display so they read identically.
    static constexpr const char* LABEL_IN_MENUS = "In Menus";  // running our plugin, between sessions
    static constexpr const char* LABEL_UNKNOWN  = "Unknown";   // in our game, but not running the plugin

    enum class Status {
        NOT_INITIALIZED,   // initialize() not called yet
        CONNECTED,         // hooked steam_api64.dll and got ISteamFriends
        NOT_AVAILABLE,     // steam_api64.dll not loaded (game launched without Steam)
        HOOK_FAILED        // DLL present but couldn't resolve interfaces/functions
    };

    static SteamFriendsManager& getInstance();

    // Hook the game's Steam API. Safe to call once at startup; logs the result.
    void initialize();

    // Clear our rich presence and drop the interface pointers.
    void shutdown();

    // Called from PluginData::notifyHudManager on the game thread. On session/
    // standings changes we refresh our own presence (write) and, throttled,
    // re-scan friends (read) and dump them to the log.
    void onDataChanged(DataChangeType changeType);

    Status getStatus() const { return m_status; }
    const char* getStatusString() const;

    // Global on/off (persisted as "steamFriends" in the global INI section,
    // mirroring DiscordManager). Default on: Steam rich presence is expected
    // behaviour for a Steam game, so we broadcast our presence and read friends
    // unless the user turns it off. All calls happen on the game thread.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // True if the game loaded steam_api64.dll (i.e. the Steam build). The
    // standalone (non-Steam) build never loads it, so the feature can't work
    // there. Cheap (GetModuleHandle, no hook) - safe to call from UI rendering
    // to decide whether to offer the control or show it disabled.
    static bool isSteamRuntimeAvailable();

    // Current roster of friends in our game, rebuilt each scan. Read by the
    // Friends HUD on the game thread (same thread the scan runs on, so no lock).
    const std::vector<SteamFriend>& getFriends() const { return m_roster; }

    // Our own presence as a SteamFriend row (name "You"), kept current by
    // updateLocalPresence() from the same fields it broadcasts. The Friends HUD
    // prepends this when "Show myself" is enabled; valid once CONNECTED.
    const SteamFriend& getSelf() const { return m_self; }

    // Timestamp of the last *friend* activity (a friend newly in our game, or one
    // that moved to a new server) - refreshed by the scan, read by the Friends
    // HUD's "On Join" mode. Default-constructed (epoch) means "no activity yet".
    std::chrono::steady_clock::time_point getLastActivityTime() const { return m_lastActivityTime; }

private:
    SteamFriendsManager();
    ~SteamFriendsManager();
    SteamFriendsManager(const SteamFriendsManager&) = delete;
    SteamFriendsManager& operator=(const SteamFriendsManager&) = delete;

    bool hookSteamApi();

    // Post-connection actions shared by initialize() and the deferred retry:
    // mark CONNECTED, publish presence, and do a first friend scan.
    void onConnected();

    // Write our own track/server keys from the current PluginData snapshot.
    void updateLocalPresence();

    // Enumerate friends, filter to those in our AppID, dump name + all rich
    // presence keys (and track/server specifically) to the log.
    void scanFriends();

    Status m_status;
    bool m_enabled;            // user toggle; default on, game thread only

    // steam_api64.dll interfaces + resolved functions
    void* m_friends;           // ISteamFriends*
    void* m_utils;             // ISteamUtils*
    uint32_t m_appId;          // our AppID (0 = unknown, no game filter)
    bool m_readsValidated;     // GetFriendCount passed on the acquired interface

    // Flat function pointers (resolved once in hookSteamApi)
    void* m_fnGetFriendCount;
    void* m_fnGetFriendByIndex;
    void* m_fnGetFriendPersonaName;
    void* m_fnGetFriendGamePlayed;
    void* m_fnGetFriendRichPresence;
    void* m_fnGetRPKeyCount;
    void* m_fnGetRPKeyByIndex;
    void* m_fnSetRichPresence;
    void* m_fnClearRichPresence;

    // Throttling / change detection
    std::string m_lastLocalStatus;                          // skip redundant writes
    int m_lastLoggedSessionGen = -1;                        // session gen of last logged write (first-write-per-session Release log)

    // Raw-input fingerprint for updateLocalPresence: Standings notifies fire
    // many times per second on a full grid, and the presence build allocates
    // ~10 strings before the write-dedup can kick in. These are exactly the
    // fields the strings are derived from; if none changed since last time,
    // the build is skipped outright. Session time is bucketed per second -
    // the finest granularity the self-row clock (m_self.progress) displays -
    // so the clock still ticks every second.
    struct PresenceInputs {
        char trackName[100] = {};
        char serverName[100] = {};
        int drawState = -1;
        int eventType = -1;
        int session = -1;
        int sessionState = -1;
        int sessionLength = -1;
        int sessionNumLaps = -1;
        bool online = false;
        bool multiRider = false;   // session has >1 rider -> "Online" when serverType is unknown
        int lapsToGo = -1;
        int timeSeconds = -1;

        bool operator==(const PresenceInputs& o) const {
            return drawState == o.drawState && eventType == o.eventType
                && session == o.session && sessionState == o.sessionState
                && sessionLength == o.sessionLength && sessionNumLaps == o.sessionNumLaps
                && online == o.online && multiRider == o.multiRider && lapsToGo == o.lapsToGo
                && timeSeconds == o.timeSeconds
                && strcmp(trackName, o.trackName) == 0
                && strcmp(serverName, o.serverName) == 0;
        }
    };
    PresenceInputs m_lastPresenceInputs;
    bool m_hasPresenceInputs = false;                       // fingerprint valid?
    std::string m_lastRosterSig;                            // only log a scan when its content changes
    std::vector<SteamFriend> m_roster;                      // current friends-in-game, for the HUD
    SteamFriend m_self;                                     // our own presence as a row ("Show myself")
    std::chrono::steady_clock::time_point m_lastActivityTime; // last new-friend / server-change (On Join)
    std::chrono::steady_clock::time_point m_lastScan;       // throttle the read scan
    std::chrono::steady_clock::time_point m_lastHookAttempt; // throttle deferred re-hook
    static constexpr int SCAN_INTERVAL_MS = 10000;          // re-scan / re-hook at most every 10s
};
