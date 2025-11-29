// ============================================================================
// handlers/run_handler.h
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RunHandler {
public:
    static RunHandler& getInstance();

    void handleRunInit(SPluginsBikeSession_t* psSessionData);
    void handleRunStart();
    void handleRunStop();
    void handleRunDeinit();

private:
    RunHandler() {}
    ~RunHandler() {}
    RunHandler(const RunHandler&) = delete;
    RunHandler& operator=(const RunHandler&) = delete;
};
