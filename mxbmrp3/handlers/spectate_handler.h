// ============================================================================
// handlers/spectate_handler.h
// Manages spectate mode vehicle and camera selection
// ============================================================================
#pragma once

#include "../game/unified_types.h"
#include <atomic>

class SpectateHandler {
public:
    static SpectateHandler& getInstance();

    // Semantic camera roles. The SpectateCameras list is dynamic per track (track
    // cameras like Trackside/Pits/Start appear only when defined, shifting indices),
    // so we select by NAME, never by a fixed index. Each role maps to one or more
    // candidate camera names; the first one present on the current track is used,
    // falling back to Auto (which is always available, index 0).
    enum class CameraRole {
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

    // Handle spectate vehicle selection callback
    // Returns 1 if selection changed, 0 otherwise
    int handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect);

    // Handle spectate camera selection callback
    // Returns 1 if selection changed, 0 otherwise
    int handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect);

    // Request to spectate a specific rider by race number
    void requestSpectateRider(int raceNum);

    // Request a camera by semantic role (resolved to an index by name on the next
    // SpectateCameras callback). One-shot, mirroring requestSpectateRider().
    void requestSpectateCamera(CameraRole role);

    // True while the broadcaster is hand-flying the camera (Orbit / Free /
    // Free-Roam). The auto-director pauses entirely in this state so it doesn't
    // yank a manual shot. Detected from the live SpectateCameras selection.
    bool isManualCameraActive() const { return m_manualCameraActive.load(std::memory_order_relaxed); }

    // Clear the per-session camera tracking so the next SpectateCameras callback
    // re-resolves the manual flag from scratch (never trusts a stale value from a
    // previous spectate session). Called on a session change / director re-enable.
    void resetCameraTracking() {
        m_manualCameraActive = false;
        m_lastCameraSelection = -999;
        m_lastCameraCount = -1;
    }

private:
    SpectateHandler() {}
    ~SpectateHandler() {}
    SpectateHandler(const SpectateHandler&) = delete;
    SpectateHandler& operator=(const SpectateHandler&) = delete;

    // All five are atomic because SpectateVehicles/SpectateCameras answer the game
    // SYNCHRONOUSLY on the GAME thread (they can't be queued) while the values are also
    // written on the plugin-worker thread in threaded mode: the director /
    // click-to-spectate set m_requested* via request*(), and session/director changes
    // call resetCameraTracking(); isManualCameraActive() is read by the director. In
    // legacy mode everything is the game thread and these are just plain int/bool loads.
    std::atomic<int> m_requestedSpectateRaceNum{ -1 };  // Requested race number to spectate (-1 = none)
    std::atomic<int> m_requestedCameraRole{ -1 };       // Requested CameraRole as int (-1 = none)
    std::atomic<int> m_lastCameraSelection{ -999 };     // Last seen SpectateCameras curSelection
    std::atomic<int> m_lastCameraCount{ -1 };           // Last seen SpectateCameras count (re-resolve on session/track change)
    std::atomic<bool> m_manualCameraActive{ false };    // Broadcaster is on a manual camera (Orbit/Free/Free-Roam)
};
