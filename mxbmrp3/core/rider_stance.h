// ============================================================================
// core/rider_stance.h
// Whether this game build sends the rider's stance at all.
//
// Newer MX Bikes builds append one int to the bike telemetry: 1 standing, 0
// seated (Adapter::readRiderStance). Older builds send the shorter struct, which
// reads as -1. Support is a property of the running game, so it is LATCHED here
// the first time a frame carries a stance, and never cleared: PluginData resets
// the telemetry itself at every session change, and the Widgets tab must keep
// offering the Stance widget in the menus once the game has shown it can.
//
// Game thread only, like everything that reads telemetry.
// ============================================================================
#pragma once

namespace RiderStance {

inline bool& sentFlag() {
    static bool sent = false;
    return sent;
}

// Called with every player telemetry frame's stance (-1, 0 or 1).
inline void noteFrame(int stance) {
    if (stance >= 0) sentFlag() = true;
}

inline bool gameSendsIt() { return sentFlag(); }

}  // namespace RiderStance
