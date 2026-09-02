// ============================================================================
// core/proximity_tuning.h
// The INI-only tuning knobs for the two proximity detectors — blue flags
// (someone a lap up is catching you) and hazards (a stationary or wrong-way
// rider ahead).
//
// WHY THIS EXISTS. None of these values is state the rest of PluginData reasons
// about — they are configuration, read by the detectors and written once at
// settings load. As a struct they cost PluginData two accessors instead of one
// pair per field, and the clamp RANGE lives next to the field it clamps rather
// than in a setter body far away.
//
// The clamps are the contract, not decoration: the INI is hand-editable (an
// explicitly supported workflow), so every value here is attacker-adjacent in
// the mundane sense that a user can type anything into it. Setting through
// these methods is what guarantees the detectors never see a zero track-length
// divisor or a negative duration. Assign to the fields directly only where the
// value is already known-clamped.
// ============================================================================
#pragma once

#include <algorithm>

struct ProximityTuning {
    // --- blue flag --------------------------------------------------------
    // How close a lapper must be, in metres, before the rider ahead is shown a
    // blue flag. Converted to a fraction of a lap using the track length.
    float blueFlagAwarenessDistance = 100.0f;

    // --- hazards ----------------------------------------------------------
    float hazardStationaryTolerance = 5.0f;   // movement below this (m) = "not moving"
    int   hazardStationaryDurationMs = 2000;  // time stationary before flagged
    int   hazardWrongWayDurationMs = 1500;    // time going backward before flagged
    float hazardAwarenessDistance = 100.0f;   // metres ahead to scan for STATIONARY hazards

    // Wrong-way hazards get their own, longer reach. 100 m is right for a bike lying on
    // the track: you close it at your own speed, so it is ~2s of warning. A rider coming
    // AT you closes the same gap at roughly double that rate, and the wrong-way state
    // still has to survive hazardWrongWayDurationMs before it counts — which ate most of
    // the warning, so the notice landed as they passed you, or never. Same detector, same
    // "ahead within X" test; only the reach differs, because the threat does.
    float hazardWrongWayAwarenessDistance = 250.0f;

    int   hazardCooldownMs = 1000;            // hysteresis before clearing a hazard
    int   hazardGracePeriodMs = 10000;        // per-rider pit-exit grace

    // Clamped setters.
    void setBlueFlagAwarenessDistance(float m) { blueFlagAwarenessDistance = std::clamp(m, 10.0f, 500.0f); }
    void setHazardStationaryTolerance(float m) { hazardStationaryTolerance = std::clamp(m, 1.0f, 50.0f); }
    void setHazardStationaryDurationMs(int ms) { hazardStationaryDurationMs = std::clamp(ms, 1000, 30000); }
    void setHazardWrongWayDurationMs(int ms)   { hazardWrongWayDurationMs = std::clamp(ms, 100, 10000); }
    void setHazardAwarenessDistance(float m)   { hazardAwarenessDistance = std::clamp(m, 10.0f, 500.0f); }
    void setHazardWrongWayAwarenessDistance(float m) { hazardWrongWayAwarenessDistance = std::clamp(m, 10.0f, 500.0f); }
    void setHazardCooldownMs(int ms)           { hazardCooldownMs = std::clamp(ms, 0, 30000); }
    void setHazardGracePeriodMs(int ms)        { hazardGracePeriodMs = std::clamp(ms, 0, 60000); }
};
