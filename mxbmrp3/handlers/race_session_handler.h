// ============================================================================
// handlers/race_session_handler.h
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceSessionHandler {
public:
    static RaceSessionHandler& getInstance();

    void handleRaceSession(Unified::RaceSessionData* psRaceSession);
    void handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState);

private:
    RaceSessionHandler() {}
    ~RaceSessionHandler() {}
    RaceSessionHandler(const RaceSessionHandler&) = delete;
    RaceSessionHandler& operator=(const RaceSessionHandler&) = delete;
};
