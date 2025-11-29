// ============================================================================
// handlers/race_event_handler.h
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceEventHandler {
public:
    static RaceEventHandler& getInstance();

    void handleRaceEvent(SPluginsRaceEvent_t* psRaceEvent);
    void handleRaceDeinit();

private:
    RaceEventHandler() {}
    ~RaceEventHandler() {}
    RaceEventHandler(const RaceEventHandler&) = delete;
    RaceEventHandler& operator=(const RaceEventHandler&) = delete;
};
