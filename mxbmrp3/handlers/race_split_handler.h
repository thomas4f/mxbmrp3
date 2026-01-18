// ============================================================================
// handlers/race_split_handler.h
// Processes race split timing data for current lap tracking
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceSplitHandler {
public:
    static RaceSplitHandler& getInstance();

    void handleRaceSplit(Unified::RaceSplitData* psRaceSplit);

private:
    RaceSplitHandler() {}
    ~RaceSplitHandler() {}
    RaceSplitHandler(const RaceSplitHandler&) = delete;
    RaceSplitHandler& operator=(const RaceSplitHandler&) = delete;
};
