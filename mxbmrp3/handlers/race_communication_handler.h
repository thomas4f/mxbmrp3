// ============================================================================
// handlers/race_communication_handler.h
// Processes race communication messages (penalties, warnings)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceCommunicationHandler {
public:
    static RaceCommunicationHandler& getInstance();

    void handleRaceCommunication(SPluginsRaceCommunication_t* psRaceCommunication, int dataSize);

private:
    RaceCommunicationHandler() {}
    ~RaceCommunicationHandler() {}
    RaceCommunicationHandler(const RaceCommunicationHandler&) = delete;
    RaceCommunicationHandler& operator=(const RaceCommunicationHandler&) = delete;
};
