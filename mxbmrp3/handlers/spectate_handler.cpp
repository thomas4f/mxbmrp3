// ============================================================================
// handlers/spectate_handler.cpp
// Manages spectate mode vehicle and camera selection
// ============================================================================
#include "spectate_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/seh_compat.h"   // SEH_TRY / SEH_EXCEPT_ALL (portable SEH)
#include "../diagnostics/logger.h"

#include <cstring>   // _stricmp

DEFINE_HANDLER_SINGLETON(SpectateHandler)

// Crash-safe search of the opaque SpectateCameras blob for a camera whose name
// matches any of `candidates` (case-insensitive). The camera names are packed
// null-terminated strings at the front of the blob (confirmed in-game across
// multiple tracks: the list is dynamic, e.g. 11/13/14 cameras, and track cameras
// shift the indices, which is exactly why we match by name). The callback passes
// no element size, so the blob's bounds are unknown -> __try/__except guards any
// over-read. POD-only body (no C++ objects needing unwinding) to avoid MSVC C2712.
// Returns the camera index, or -1 if no candidate is present.
static int findCameraIndexByName(const void* data, int numCameras,
                                 const char* const* candidates, int numCandidates) {
    int found = -1;
    if (data == nullptr || numCameras <= 0 || candidates == nullptr) return -1;
    // SEH_TRY guards a genuine over-read past the blob's real allocation. On the
    // non-MSVC test build it runs unguarded — the walk is bounded (kMaxBytes) and
    // POD-only, and that build never sees a live over-sized blob. See seh_compat.h.
    SEH_TRY {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        const int kMaxBytes = 4096;     // hard cap on how far we walk
        const int kMaxName = 64;
        // `candidates` is a PRIORITY list: a camera matching candidate[0] must beat a
        // (lower-index) camera matching candidate[1]. Track the best candidate rank
        // seen so the chosen camera is the highest-priority name present, not just the
        // first camera that matches anything. (Matters for FREE_ROAM, where Orbit sits
        // at a lower index than Free-Roam but should lose to it.)
        int bestRank = numCandidates;   // lower = higher priority; sentinel = no match
        int pos = 0;
        for (int idx = 0; idx < numCameras && pos < kMaxBytes; ++idx) {
            // Skip zero padding between records (none for packed layouts).
            while (pos < kMaxBytes && p[pos] == 0) pos++;
            if (pos >= kMaxBytes) break;

            char name[kMaxName];
            int nl = 0;
            while (pos < kMaxBytes && p[pos] != 0 && nl < kMaxName - 1) {
                unsigned char c = p[pos];
                name[nl++] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
                pos++;
            }
            name[nl] = '\0';
            if (pos < kMaxBytes && p[pos] == 0) pos++;   // step past terminator

            for (int c = 0; c < numCandidates && c < bestRank; ++c) {
                if (candidates[c] && _stricmp(name, candidates[c]) == 0) {
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

// Crash-safe: copy the name of camera `targetIndex` from the packed name table into
// `out`. Returns true if a name was read. Same SEH-guarded walk as above.
static bool cameraNameAtIndex(const void* data, int numCameras, int targetIndex,
                              char* out, int outSize) {
    if (out == nullptr || outSize < 1) return false;
    out[0] = '\0';
    if (data == nullptr || targetIndex < 0 || targetIndex >= numCameras) return false;
    bool ok = false;
    SEH_TRY {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        const int kMaxBytes = 4096;
        int pos = 0;
        for (int idx = 0; idx <= targetIndex && pos < kMaxBytes; ++idx) {
            while (pos < kMaxBytes && p[pos] == 0) pos++;   // skip padding
            if (pos >= kMaxBytes) break;
            int start = pos;
            while (pos < kMaxBytes && p[pos] != 0) pos++;   // walk to terminator
            if (idx == targetIndex) {
                int n = 0;
                for (int i = start; i < pos && n < outSize - 1; ++i) {
                    unsigned char c = p[i];
                    out[n++] = (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
                }
                out[n] = '\0';
                ok = true;
                break;
            }
            if (pos < kMaxBytes && p[pos] == 0) pos++;       // step past terminator
        }
    } SEH_EXCEPT_ALL {
        ok = false;
    }
    return ok;
}

int SpectateHandler::handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect) {
    // Track the currently spectated rider
    if (iCurSelection >= 0 && iCurSelection < iNumVehicles && pasVehicleData != nullptr) {
        int spectatedRaceNum = pasVehicleData[iCurSelection].raceNum;
        PluginData::getInstance().setSpectatedRaceNum(spectatedRaceNum);
    }

    // Check if a spectate switch was requested
    if (m_requestedSpectateRaceNum >= 0 && pasVehicleData != nullptr && piSelect != nullptr) {
        // Find the rider with the requested race number
        for (int i = 0; i < iNumVehicles; ++i) {
            if (pasVehicleData[i].raceNum == m_requestedSpectateRaceNum) {
                // Found the rider - switch to them
                DEBUG_INFO_F("Spectating rider #%d (%s)", m_requestedSpectateRaceNum, pasVehicleData[i].name);
                *piSelect = i;
                m_requestedSpectateRaceNum = -1;  // Clear the request
                return 1;  // Selection changed
            }
        }
        // Rider not found - clear the invalid request
        m_requestedSpectateRaceNum = -1;
    }

    // No change requested or no match found
    return 0;
}

int SpectateHandler::handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect) {
    // Detect broadcaster manual-camera control. Orbit/Free/Free-Roam let the caster
    // fly the camera by hand; while one is active the director pauses entirely (it
    // checks isManualCameraActive()) so it doesn't yank a manual shot. Re-resolve
    // only when the selection changes - cheap despite the ~140/s call rate.
    // Re-resolve on a selection change OR a camera-list change (new session/track,
    // which can reshuffle indices) - the latter also clears a stale manual flag from
    // a previous session.
    if (iCurSelection != m_lastCameraSelection || iNumCameras != m_lastCameraCount) {
        m_lastCameraSelection = iCurSelection;
        m_lastCameraCount = iNumCameras;
        bool manual = false;
        char name[64];
        if (cameraNameAtIndex(pCameraData, iNumCameras, iCurSelection, name, sizeof(name))) {
            manual = (_stricmp(name, "Orbit") == 0 ||
                      _stricmp(name, "Free") == 0 ||
                      _stricmp(name, "Free-Roam") == 0 ||
                      _stricmp(name, "Free Roam") == 0 ||
                      _stricmp(name, "Freeroam") == 0);
        }
        if (manual != m_manualCameraActive) {
            m_manualCameraActive = manual;
            DEBUG_INFO_F("Director: broadcaster manual camera %s", manual ? "ON (paused)" : "OFF");
        }
    }

    // The game invokes this callback every frame during spectate (~140/s, confirmed
    // in-game), so a pending camera request lands within a frame - same as the rider
    // path. Honor a pending camera request (from the auto-director). Resolve the
    // requested role to an index by matching camera names on this track; fall back
    // to Auto (index 0, always present) if the desired camera isn't defined here.
    if (m_requestedCameraRole >= 0 && pCameraData != nullptr && piSelect != nullptr && iNumCameras > 0) {
        static const char* kAuto[]      = { "Auto" };
        static const char* kTrackside[] = { "Trackside", "Camera Set" };
        static const char* kStart[]     = { "Start" };
        static const char* kFront[]     = { "Front Fender" };
        // Two distinct helmet cams: ONBOARD_HELMET prefers "Helmet 1" (on-head),
        // ONBOARD_HELMET2 prefers "Helmet 2" (side). Each falls back to the other so a
        // track that exposes only one still resolves rather than dead-ending.
        static const char* kHelmet[]    = { "Helmet 1", "Helmet 2" };
        static const char* kHelmet2[]   = { "Helmet 2", "Helmet 1" };
        static const char* kRear[]      = { "Rear Fender" };
        static const char* kForks[]     = { "Forks" };
        static const char* kFreeRoam[]  = { "Free-Roam", "Free Roam", "Freeroam", "Free", "Orbit" };

        const CameraRole reqRole = static_cast<CameraRole>(m_requestedCameraRole);
        const bool isFreeRoam = (reqRole == CameraRole::FREE_ROAM);

        const char* const* cand = kAuto;
        int nCand = 1;
        switch (reqRole) {
            case CameraRole::AUTO:           cand = kAuto;      nCand = 1; break;
            case CameraRole::TRACKSIDE:      cand = kTrackside; nCand = 2; break;
            case CameraRole::START:          cand = kStart;     nCand = 1; break;
            case CameraRole::ONBOARD_FRONT:  cand = kFront;     nCand = 1; break;
            case CameraRole::ONBOARD_HELMET: cand = kHelmet;    nCand = 2; break;
            case CameraRole::ONBOARD_HELMET2: cand = kHelmet2;  nCand = 2; break;
            case CameraRole::REAR:           cand = kRear;      nCand = 1; break;
            case CameraRole::FORKS:          cand = kForks;     nCand = 1; break;
            case CameraRole::FREE_ROAM:      cand = kFreeRoam;  nCand = 5; break;
        }

        int idx = findCameraIndexByName(pCameraData, iNumCameras, cand, nCand);

        int role = m_requestedCameraRole;
        m_requestedCameraRole = -1;  // one-shot

        // Free-Roam must resolve to a real manual camera - falling back to Auto would
        // defeat the takeover (no hand control). If this track exposes none, leave the
        // current camera untouched.
        if (isFreeRoam) {
            if (idx < 0) return 0;
        } else {
            if (idx < 0 && reqRole != CameraRole::AUTO) {
                idx = findCameraIndexByName(pCameraData, iNumCameras, kAuto, 1);  // fall back to Auto by name
            }
            if (idx < 0) idx = 0;  // Auto is always index 0 in every list we observed
        }

        if (idx >= 0 && idx < iNumCameras) {
            if (idx != iCurSelection) {
                DEBUG_INFO_F("Director: camera role %d -> index %d", role, idx);
                *piSelect = idx;
                return 1;
            }
            return 0;  // already on the desired camera
        }
    }

    return 0;
}

void SpectateHandler::requestSpectateRider(int raceNum) {
    m_requestedSpectateRaceNum = raceNum;
}

void SpectateHandler::requestSpectateCamera(CameraRole role) {
    m_requestedCameraRole = static_cast<int>(role);
}
