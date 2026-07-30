// ============================================================================
// handlers/run_telemetry_handler.h
// Processes run telemetry data (input, controller, vehicle telemetry)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRunTelemetry(Unified::TelemetryData* psTelemetryData);

}  // namespace Handlers
