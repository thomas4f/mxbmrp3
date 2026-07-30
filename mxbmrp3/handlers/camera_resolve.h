// ============================================================================
// handlers/camera_resolve.h
// Pure camera-name resolution for the SpectateCameras callback: walk the opaque
// camera blob, match names, and turn a semantic CameraRole into an index.
//
// WHY THIS IS A SEPARATE HEADER. This is the whole decision the director makes
// about which camera to cut to, and every input is a byte buffer plus an enum —
// no singletons, no game state, no threads. Split out of spectate_handler.cpp so
// it can be exercised by tests/unit/test_camera_resolve.cpp (doctest, ~1s, no
// mingw/Wine) instead of only through a DLL load under Wine. The handler keeps
// what genuinely needs the handler: the atomics, the one-shot request consumption
// and the PluginData writes.
//
// The blob is opaque. SpectateCameras passes no element size, so the buffer's
// real bounds are unknown; every walk is capped at kMaxBytes and guarded by
// SEH_TRY, which catches a hardware over-read on MSVC (see seh_compat.h). Bodies
// are POD-only — no C++ object needing unwinding — as native SEH requires
// (MSVC C2712).
//
// Layout, confirmed in-game across multiple tracks: the camera names are packed
// null-terminated strings at the front of the blob. The list is DYNAMIC (11/13/14
// cameras seen) and optional track cameras shift the indices, which is exactly
// why every lookup here is by name and never by a fixed index.
// ============================================================================
#pragma once

#include "../core/seh_compat.h"   // SEH_TRY / SEH_EXCEPT_ALL (portable SEH)

namespace Cameras {

// How far we are willing to walk into a blob of unknown length, and the longest
// camera name we will materialize.
constexpr int kMaxBytes = 4096;
constexpr int kMaxName  = 64;

// Semantic camera roles the director requests. Each maps to one or more
// candidate camera NAMES (candidatesForRole); the first one present on the
// current track wins.
enum class Role {
    AUTO,            // "Auto" - game's own trackside auto-director for the subject
    TRACKSIDE,       // "Trackside" / "Camera Set" - track TV cameras
    START,           // "Start" - grid/start camera
    ONBOARD_FRONT,   // "Front Fender" - forward onboard
    ONBOARD_HELMET,  // "Helmet 1" - on-head POV (forward)
    ONBOARD_HELMET2, // "Helmet 2" - side/secondary helmet cam (forward)
    REAR,            // "Rear Fender" - rearward onboard (shows a chaser)
    FORKS,           // "Forks" - down the front suspension
    FREE_ROAM        // "Free-Roam" / "Free" / "Orbit" - manual hand-flown camera
                     // (used by the director's gamepad-takeover grab)
};

// ASCII case-insensitive compare. Deliberately not _stricmp: that is an
// MSVC/mingw extension and this header is compiled by the Linux unit build too.
// Camera names are ASCII by construction (readName below maps every byte outside
// 32..126 to '?'), so an ASCII fold is exact here, not an approximation.
inline bool iequals(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    for (;; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
}

// Read one packed name starting at `pos`, skipping any zero padding in front of
// it (packed layouts have none). Advances `pos` past the terminator. Returns
// false once the walk runs out of buffer. Non-printable bytes become '?' so a
// garbage blob can never inject control characters into a log line.
//
// The cursor ALWAYS advances to the terminator; only the OUTPUT is truncated at
// outCap. Stopping the cursor at the buffer limit instead — which the two
// pre-extraction copies of this walk did inconsistently — leaves the tail of an
// over-long name to be read as another camera and shifts every index after it,
// so a lookup silently returns the wrong camera. Pinned by the over-long-name
// case in tests/unit/test_camera_resolve.cpp.
inline bool readName(const unsigned char* p, int& pos, char* out, int outCap) {
    if (out == nullptr || outCap < 1) return false;
    out[0] = '\0';
    while (pos < kMaxBytes && p[pos] == 0) pos++;   // skip padding
    if (pos >= kMaxBytes) return false;
    int n = 0;
    while (pos < kMaxBytes && p[pos] != 0) {
        unsigned char c = p[pos];
        if (n < outCap - 1) {
            out[n++] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
        }
        pos++;
    }
    out[n] = '\0';
    if (pos < kMaxBytes && p[pos] == 0) pos++;      // step past terminator
    return true;
}

// Copy the name of camera `targetIndex` into `out`. Returns true if a name was read.
inline bool nameAtIndex(const void* data, int numCameras, int targetIndex,
                        char* out, int outSize) {
    if (out == nullptr || outSize < 1) return false;
    out[0] = '\0';
    if (data == nullptr || targetIndex < 0 || targetIndex >= numCameras) return false;
    bool ok = false;
    SEH_TRY {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        int pos = 0;
        for (int idx = 0; idx <= targetIndex; ++idx) {
            char name[kMaxName];
            if (!readName(p, pos, name, kMaxName)) break;
            if (idx == targetIndex) {
                int n = 0;
                for (int i = 0; name[i] != '\0' && n < outSize - 1; ++i) out[n++] = name[i];
                out[n] = '\0';
                ok = true;
                break;
            }
        }
    } SEH_EXCEPT_ALL {
        ok = false;
    }
    return ok;
}

// Index of the highest-PRIORITY candidate name present in the blob, or -1.
//
// `candidates` is a priority list, not a set: a camera matching candidate[0] must
// beat a lower-INDEXED camera matching candidate[1]. Tracking the best rank seen
// (rather than returning the first camera that matches anything) is what makes
// FREE_ROAM correct, since "Orbit" typically sits at a lower index than
// "Free-Roam" but should lose to it.
inline int findIndexByName(const void* data, int numCameras,
                           const char* const* candidates, int numCandidates) {
    int found = -1;
    if (data == nullptr || numCameras <= 0 || candidates == nullptr) return -1;
    SEH_TRY {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        int bestRank = numCandidates;   // lower = higher priority; sentinel = no match
        int pos = 0;
        for (int idx = 0; idx < numCameras; ++idx) {
            char name[kMaxName];
            if (!readName(p, pos, name, kMaxName)) break;
            for (int c = 0; c < numCandidates && c < bestRank; ++c) {
                if (candidates[c] && iequals(name, candidates[c])) {
                    bestRank = c;
                    found = idx;
                    break;
                }
            }
            if (bestRank == 0) break;   // matched the top-priority name; can't do better
        }
    } SEH_EXCEPT_ALL {
        // Leave `found` as whatever was resolved before the fault.
    }
    return found;
}

// The candidate name list for a role, in priority order. Sets outCount.
inline const char* const* candidatesForRole(Role role, int& outCount) {
    static const char* const kAuto[]      = { "Auto" };
    static const char* const kTrackside[] = { "Trackside", "Camera Set" };
    static const char* const kStart[]     = { "Start" };
    static const char* const kFront[]     = { "Front Fender" };
    // Two distinct helmet cams: ONBOARD_HELMET prefers "Helmet 1" (on-head),
    // ONBOARD_HELMET2 prefers "Helmet 2" (side). Each falls back to the other so a
    // track that exposes only one still resolves rather than dead-ending.
    static const char* const kHelmet[]    = { "Helmet 1", "Helmet 2" };
    static const char* const kHelmet2[]   = { "Helmet 2", "Helmet 1" };
    static const char* const kRear[]      = { "Rear Fender" };
    static const char* const kForks[]     = { "Forks" };
    static const char* const kFreeRoam[]  = { "Free-Roam", "Free Roam", "Freeroam", "Free", "Orbit" };

    switch (role) {
        case Role::TRACKSIDE:       outCount = 2; return kTrackside;
        case Role::START:           outCount = 1; return kStart;
        case Role::ONBOARD_FRONT:   outCount = 1; return kFront;
        case Role::ONBOARD_HELMET:  outCount = 2; return kHelmet;
        case Role::ONBOARD_HELMET2: outCount = 2; return kHelmet2;
        case Role::REAR:            outCount = 1; return kRear;
        case Role::FORKS:           outCount = 1; return kForks;
        case Role::FREE_ROAM:       outCount = 5; return kFreeRoam;
        case Role::AUTO:            break;
    }
    outCount = 1;
    return kAuto;
}

// True for the hand-flown cameras. While one is selected the auto-director pauses
// entirely, so it never yanks a shot the broadcaster is composing by hand.
inline bool isManualName(const char* name) {
    return iequals(name, "Orbit")
        || iequals(name, "Free")
        || iequals(name, "Free-Roam")
        || iequals(name, "Free Roam")
        || iequals(name, "Freeroam");
}

// Resolve a requested role to a camera index for this track's blob.
// Returns the index to select, or -1 meaning "leave the current camera alone".
//
// Two fallback rules, and they differ on purpose:
//  - FREE_ROAM must resolve to a REAL manual camera. Falling back to Auto would
//    defeat the takeover (the caster would have no hand control), so a track that
//    exposes none yields -1 and the camera is left untouched.
//  - Every other role falls back to Auto by name, then to index 0 (Auto is
//    present in every camera list we have observed).
inline int resolveIndexForRole(const void* data, int numCameras, Role role) {
    if (data == nullptr || numCameras <= 0) return -1;

    int nCand = 0;
    const char* const* cand = candidatesForRole(role, nCand);
    int idx = findIndexByName(data, numCameras, cand, nCand);

    if (role == Role::FREE_ROAM) {
        if (idx < 0) return -1;
    } else {
        if (idx < 0 && role != Role::AUTO) {
            int nAuto = 0;
            const char* const* autoCand = candidatesForRole(Role::AUTO, nAuto);
            idx = findIndexByName(data, numCameras, autoCand, nAuto);
        }
        if (idx < 0) idx = 0;   // Auto is always index 0 in every list we observed
    }

    if (idx < 0 || idx >= numCameras) return -1;
    return idx;
}

}  // namespace Cameras
