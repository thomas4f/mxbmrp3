// ============================================================================
// handlers/race_classification_handler.h
// Processes race classification and standings calculations
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"
#include "../core/plugin_constants.h"

class RaceClassificationHandler {
public:
    static RaceClassificationHandler& getInstance();

    void handleRaceClassification(
        SPluginsRaceClassification_t* psRaceClassification,
        SPluginsRaceClassificationEntry_t* pasRaceClassificationEntry,
        int iNumEntries
    );

private:
    RaceClassificationHandler() {}
    ~RaceClassificationHandler() {}
    RaceClassificationHandler(const RaceClassificationHandler&) = delete;
    RaceClassificationHandler& operator=(const RaceClassificationHandler&) = delete;
};
