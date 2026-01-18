// ============================================================================
// handlers/race_communication_handler.h
// Processes race communication messages (penalties, warnings)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceCommunicationHandler {
public:
    static RaceCommunicationHandler& getInstance();

    void handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication);

private:
    RaceCommunicationHandler() {}
    ~RaceCommunicationHandler() {}
    RaceCommunicationHandler(const RaceCommunicationHandler&) = delete;
    RaceCommunicationHandler& operator=(const RaceCommunicationHandler&) = delete;
};
