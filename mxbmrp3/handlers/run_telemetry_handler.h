// ============================================================================
// handlers/run_telemetry_handler.h
// Processes run telemetry data (input, controller, vehicle telemetry)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RunTelemetryHandler {
public:
    static RunTelemetryHandler& getInstance();

    void handleRunTelemetry(Unified::TelemetryData* psTelemetryData);

private:
    RunTelemetryHandler() = default;
    ~RunTelemetryHandler() = default;
    RunTelemetryHandler(const RunTelemetryHandler&) = delete;
    RunTelemetryHandler& operator=(const RunTelemetryHandler&) = delete;
};
