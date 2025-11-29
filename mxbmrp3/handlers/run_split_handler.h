// ============================================================================
// handlers/run_split_handler.h
// Processes player split timing data (RunSplit events are player-only)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RunSplitHandler {
public:
    static RunSplitHandler& getInstance();

    void handleRunSplit(SPluginsBikeSplit_t* psSplitData);

private:
    RunSplitHandler() {}
    ~RunSplitHandler() {}
    RunSplitHandler(const RunSplitHandler&) = delete;
    RunSplitHandler& operator=(const RunSplitHandler&) = delete;
};
