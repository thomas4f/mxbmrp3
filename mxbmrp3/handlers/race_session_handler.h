// ============================================================================
// handlers/race_session_handler.h
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceSessionHandler {
public:
    static RaceSessionHandler& getInstance();

    void handleRaceSession(SPluginsRaceSession_t* psRaceSession);
    void handleRaceSessionState(SPluginsRaceSessionState_t* psRaceSessionState);

private:
    RaceSessionHandler() {}
    ~RaceSessionHandler() {}
    RaceSessionHandler(const RaceSessionHandler&) = delete;
    RaceSessionHandler& operator=(const RaceSessionHandler&) = delete;
};
