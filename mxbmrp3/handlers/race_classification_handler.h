// ============================================================================
// handlers/race_classification_handler.h
// Processes race classification and standings calculations
// ============================================================================
#pragma once

#include "../game/unified_types.h"
#include "../core/plugin_constants.h"

namespace Handlers {

void handleRaceClassification(
    Unified::RaceClassificationData* psRaceClassification,
    Unified::RaceClassificationEntry* pasRaceClassificationEntry,
    int iNumEntries
);

}  // namespace Handlers
