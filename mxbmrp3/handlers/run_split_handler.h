// ============================================================================
// handlers/run_split_handler.h
// Processes player split timing data (RunSplit events are player-only)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RunSplitHandler {
public:
    static RunSplitHandler& getInstance();

    void handleRunSplit(Unified::PlayerSplitData* psSplitData);

private:
    RunSplitHandler() {}
    ~RunSplitHandler() {}
    RunSplitHandler(const RunSplitHandler&) = delete;
    RunSplitHandler& operator=(const RunSplitHandler&) = delete;
};
