// ============================================================================
// handlers/race_split_handler.h
// Processes race split timing data for current lap tracking
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceSplitHandler {
public:
    static RaceSplitHandler& getInstance();

    void handleRaceSplit(SPluginsRaceSplit_t* psRaceSplit);

private:
    RaceSplitHandler() {}
    ~RaceSplitHandler() {}
    RaceSplitHandler(const RaceSplitHandler&) = delete;
    RaceSplitHandler& operator=(const RaceSplitHandler&) = delete;
};
