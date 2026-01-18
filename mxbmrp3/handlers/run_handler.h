// ============================================================================
// handlers/run_handler.h
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RunHandler {
public:
    static RunHandler& getInstance();

    void handleRunInit(Unified::SessionData* psSessionData);
    void handleRunStart();
    void handleRunStop();
    void handleRunDeinit();

private:
    RunHandler() {}
    ~RunHandler() {}
    RunHandler(const RunHandler&) = delete;
    RunHandler& operator=(const RunHandler&) = delete;
};
