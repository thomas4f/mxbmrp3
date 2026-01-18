// ============================================================================
// handlers/event_handler.h
// Processes event lifecycle data (event init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class EventHandler {
public:
    static EventHandler& getInstance();

    void handleEventInit(Unified::VehicleEventData* psEventData);
    void handleEventDeinit();

private:
    EventHandler() {}
    ~EventHandler() {}
    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;
};
