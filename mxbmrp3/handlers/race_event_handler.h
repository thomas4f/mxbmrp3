// ============================================================================
// handlers/race_event_handler.h
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceEventHandler {
public:
    static RaceEventHandler& getInstance();

    void handleRaceEvent(Unified::RaceEventData* psRaceEvent);
    void handleRaceDeinit();

private:
    RaceEventHandler() {}
    ~RaceEventHandler() {}
    RaceEventHandler(const RaceEventHandler&) = delete;
    RaceEventHandler& operator=(const RaceEventHandler&) = delete;
};
