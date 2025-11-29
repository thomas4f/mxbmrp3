// ============================================================================
// handlers/event_handler.h
// Processes event lifecycle data (event init/deinit)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class EventHandler {
public:
    static EventHandler& getInstance();

    void handleEventInit(SPluginsBikeEvent_t* psEventData);
    void handleEventDeinit();

private:
    EventHandler() {}
    ~EventHandler() {}
    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;
};
