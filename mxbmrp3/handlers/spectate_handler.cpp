// ============================================================================
// handlers/spectate_handler.cpp
// Manages spectate mode vehicle and camera selection
// ============================================================================
#include "spectate_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_thread.h"
#include "../diagnostics/logger.h"

#include <cstring>

// Camera-name matching and role->index resolution live in camera_resolve.h: they
// are pure functions over the opaque blob, so they are unit-tested directly
// (tests/unit/test_camera_resolve.cpp) rather than only through a Wine DLL load.
// What stays here is what genuinely needs the handler — the atomics, the one-shot
// request consumption, and the PluginData writes.

DEFINE_HANDLER_SINGLETON(SpectateHandler)

int SpectateHandler::handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect) {
    // Track the currently spectated rider. setSpectatedRaceNum() cascades into
    // clearTelemetryData() + a HudManager/HttpServer notify, i.e. real PluginData
    // mutation. This callback must ANSWER the game synchronously on the game thread,
    // but the tracking write must not race the worker in plugin-thread mode — so route
    // just that write onto the worker (the PluginData owner). The synchronous answer
    // below reads only the atomic request + the game's own array, so it stays inline.
    if (iCurSelection >= 0 && iCurSelection < iNumVehicles && pasVehicleData != nullptr) {
        int spectatedRaceNum = pasVehicleData[iCurSelection].raceNum;
        PluginThread& pt = PluginThread::getInstance();
        if (pt.enabled() && !pt.onWorkerThread()) {
            pt.enqueue([spectatedRaceNum]() {
                PluginData::getInstance().setSpectatedRaceNum(spectatedRaceNum);
            });
        } else {
            PluginData::getInstance().setSpectatedRaceNum(spectatedRaceNum);
        }
    }

    // Check if a spectate switch was requested. Consume the request with a single
    // exchange() up front: the director (worker thread) may post a NEW request at
    // any moment, and the old read-then-clear pattern could clobber it — e.g. the
    // "rider not found" clear overwrote a valid request posted after the loop read.
    if (m_requestedSpectateRaceNum >= 0 && pasVehicleData != nullptr && piSelect != nullptr) {
        const int requested = m_requestedSpectateRaceNum.exchange(-1, std::memory_order_relaxed);
        // Find the rider with the requested race number
        for (int i = 0; i < iNumVehicles; ++i) {
            if (pasVehicleData[i].raceNum == requested) {
                // Found the rider - switch to them
                DEBUG_INFO_F("Spectating rider #%d (%s)", requested, pasVehicleData[i].name);
                *piSelect = i;
                return 1;  // Selection changed
            }
        }
        // Rider not found - the request was already consumed by the exchange
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
        char name[Cameras::kMaxName];
        if (Cameras::nameAtIndex(pCameraData, iNumCameras, iCurSelection, name, sizeof(name))) {
            manual = Cameras::isManualName(name);
        }
        if (manual != m_manualCameraActive) {
            m_manualCameraActive = manual;
            DEBUG_INFO_F("Director: broadcaster manual camera %s", manual ? "ON (paused)" : "OFF");
        }
    }

    // The game invokes this callback every frame during spectate (~140/s, confirmed
    // in-game), so a pending camera request lands within a frame - same as the rider
    // path. Honor a pending camera request (from the auto-director); the role -> index
    // resolution (name matching, Auto fallback, Free-Roam's deliberate lack of one)
    // is Cameras::resolveIndexForRole, which returns -1 for "leave the camera alone".
    if (m_requestedCameraRole >= 0 && pCameraData != nullptr && piSelect != nullptr && iNumCameras > 0) {
        // Consume the request atomically up front (same rationale as the rider path
        // above): a plain read followed by a separate "= -1" would silently discard
        // a newer request the director posted between the two statements.
        const int reqRoleRaw = m_requestedCameraRole.exchange(-1, std::memory_order_relaxed);
        const int idx = Cameras::resolveIndexForRole(pCameraData, iNumCameras,
                                                     static_cast<CameraRole>(reqRoleRaw));
        if (idx < 0) return 0;              // nothing usable on this track
        if (idx != iCurSelection) {
            DEBUG_INFO_F("Director: camera role %d -> index %d", reqRoleRaw, idx);
            *piSelect = idx;
            return 1;
        }
        return 0;                           // already on the desired camera
    }

    return 0;
}

void SpectateHandler::requestSpectateRider(int raceNum) {
    m_requestedSpectateRaceNum = raceNum;
}

void SpectateHandler::requestSpectateCamera(CameraRole role) {
    m_requestedCameraRole = static_cast<int>(role);
}
