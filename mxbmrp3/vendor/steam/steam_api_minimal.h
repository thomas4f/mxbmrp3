// ============================================================================
// vendor/steam/steam_api_minimal.h
// Minimal Steamworks SDK definitions for the friends rich-presence probe.
//
// This is a hand-rolled subset of the Steamworks flat API - just enough to
// hook the game's already-loaded steam_api64.dll and read/write friend rich
// presence. We do NOT call SteamAPI_Init(): MX Bikes already initializes
// Steam, so we resolve the interface accessors and flat functions via
// GetProcAddress and piggyback on the game's callback pump.
//
// All flat functions take the interface pointer (ISteamFriends* / ISteamUtils*)
// as their first argument. CSteamID / CGameID are passed by value as uint64.
// ============================================================================
#pragma once

#include <cstdint>

// Steam API calling convention (flat API is __cdecl on Win64)
#define S_CALLTYPE __cdecl

// Steam handle types
typedef int32_t HSteamUser;
typedef int32_t HSteamPipe;

// Friend flags - k_EFriendFlagImmediate selects "real" friends (not requests/blocked)
constexpr int STEAM_FRIENDFLAG_IMMEDIATE = 0x04;

// Interface version strings tried during acquisition. We can't know which one
// matches the layout the dll's flat SteamAPI_ISteamFriends_* wrappers were
// built against, so the probe validates each candidate with a test GetFriendCount
// and keeps the first that works. SteamFriends017 is listed first because it's
// the version confirmed working in the current MX Bikes build (018 acquires but
// its vtable doesn't line up); the others are forward/back-compat fallbacks.
#define STEAM_FRIENDS_VERSIONS { "SteamFriends017", "SteamFriends018", "SteamFriends016", "SteamFriends015", nullptr }
#define STEAM_UTILS_VERSIONS   { "SteamUtils010", "SteamUtils009", nullptr }

// FriendGameInfo_t - populated by GetFriendGamePlayed.
// Layout must match the Steamworks SDK exactly (it's filled by steam_api64.dll).
#pragma pack(push, 8)
struct FriendGameInfo_t {
    uint64_t m_gameID;         // CGameID; for a plain Steam app (type/mod bits = 0) the low 32 bits equal the AppID
    uint32_t m_unGameIP;
    uint16_t m_usGamePort;
    uint16_t m_usQueryPort;
    uint64_t m_steamIDLobby;   // CSteamID of the lobby, 0 if none
};
#pragma pack(pop)

// ============================================================================
// Flat-API function pointer typedefs
// ============================================================================

// Modern interface acquisition (stable exports used by the SDK's own inline
// accessors). FindOrCreateUserInterface needs a valid HSteamUser, which also
// tells us whether the game has actually initialized Steam (0 = not ready).
typedef HSteamUser (S_CALLTYPE *SteamAPI_GetHSteamUser_FnPtr)();
typedef HSteamPipe (S_CALLTYPE *SteamAPI_GetHSteamPipe_FnPtr)();
typedef void*      (S_CALLTYPE *SteamInternal_FindOrCreateUserInterface_FnPtr)(HSteamUser, const char*);

// ISteamClient flat getters. SteamClient() is the most universally exported
// accessor, and the steam_experiment branch proved this path works in MX Bikes.
// NOTE the signatures differ: GetISteamFriends takes (user, pipe, version) but
// GetISteamUtils takes (pipe, version) only - no user handle.
typedef void* (S_CALLTYPE *ISteamClient_GetFriends_FnPtr)(void* self, HSteamUser, HSteamPipe, const char*);
typedef void* (S_CALLTYPE *ISteamClient_GetUtils_FnPtr)(void* self, HSteamPipe, const char*);

// Legacy unversioned accessors are called through an inline cast in the probe,
// so no dedicated typedef is needed for them.

// ISteamUtils
typedef uint32_t (S_CALLTYPE *ISteamUtils_GetAppID_FnPtr)(void* self);

// ISteamFriends - read
typedef int         (S_CALLTYPE *ISteamFriends_GetFriendCount_FnPtr)(void* self, int iFriendFlags);
typedef uint64_t    (S_CALLTYPE *ISteamFriends_GetFriendByIndex_FnPtr)(void* self, int iFriend, int iFriendFlags);
typedef const char* (S_CALLTYPE *ISteamFriends_GetFriendPersonaName_FnPtr)(void* self, uint64_t steamIDFriend);
typedef bool        (S_CALLTYPE *ISteamFriends_GetFriendGamePlayed_FnPtr)(void* self, uint64_t steamIDFriend, FriendGameInfo_t* pFriendGameInfo);
typedef const char* (S_CALLTYPE *ISteamFriends_GetFriendRichPresence_FnPtr)(void* self, uint64_t steamIDFriend, const char* pchKey);
typedef int         (S_CALLTYPE *ISteamFriends_GetFriendRichPresenceKeyCount_FnPtr)(void* self, uint64_t steamIDFriend);
typedef const char* (S_CALLTYPE *ISteamFriends_GetFriendRichPresenceKeyByIndex_FnPtr)(void* self, uint64_t steamIDFriend, int iKey);

// ISteamFriends - write (our own presence)
typedef bool (S_CALLTYPE *ISteamFriends_SetRichPresence_FnPtr)(void* self, const char* pchKey, const char* pchValue);
typedef void (S_CALLTYPE *ISteamFriends_ClearRichPresence_FnPtr)(void* self);
