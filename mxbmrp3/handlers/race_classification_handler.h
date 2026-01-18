// ============================================================================
// handlers/race_classification_handler.h
// Processes race classification and standings calculations
// ============================================================================
#pragma once

#include "../game/unified_types.h"
#include "../core/plugin_constants.h"

class RaceClassificationHandler {
public:
    static RaceClassificationHandler& getInstance();

    void handleRaceClassification(
        Unified::RaceClassificationData* psRaceClassification,
        Unified::RaceClassificationEntry* pasRaceClassificationEntry,
        int iNumEntries
    );

private:
    RaceClassificationHandler() {}
    ~RaceClassificationHandler() {}
    RaceClassificationHandler(const RaceClassificationHandler&) = delete;
    RaceClassificationHandler& operator=(const RaceClassificationHandler&) = delete;
};
