// ============================================================================
// core/plugin_utils.cpp
// Utility functions for formatting, string conversion, and calculations
// ============================================================================
#include "plugin_utils.h"
#include "plugin_constants.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>

void PluginUtils::formatTimeMinutesSeconds(int milliseconds, char* buffer, size_t bufferSize) {
    // Validate buffer exists and has space for at least null terminator
    if (bufferSize < 1) return;

    if (bufferSize < 8) {
        buffer[0] = '\0';
        return;
    }

    if (milliseconds <= 0) {
        strcpy_s(buffer, bufferSize, "00:00");
        return;
    }

    using namespace PluginConstants::TimeConversion;
    int minutes = milliseconds / MS_PER_MINUTE;
    int seconds = (milliseconds % MS_PER_MINUTE) / MS_PER_SECOND;

    snprintf(buffer, bufferSize, "%02d:%02d", minutes, seconds);
}

void PluginUtils::formatLapTime(int lapTimeMs, char* buffer, size_t bufferSize) {
    if (lapTimeMs >= 0) {
        using namespace PluginConstants::TimeConversion;
        int minutes = lapTimeMs / MS_PER_MINUTE;
        int seconds = (lapTimeMs % MS_PER_MINUTE) / MS_PER_SECOND;
        int ms = lapTimeMs % MS_PER_SECOND;

        // Use M:SS.mmm by default (single-digit minutes is the common case)
        // Format will naturally expand to MM:SS.mmm for >= 10 minutes
        snprintf(buffer, bufferSize, "%d:%02d.%03d", minutes, seconds, ms);
    }
    else {
        buffer[0] = '\0';
    }
}

void PluginUtils::formatTimeDiff(char* buffer, size_t bufferSize, int diffMs) {
    using namespace PluginConstants::TimeConversion;

    // Handle sign and protect against INT_MIN overflow
    char sign = '+';
    int absDiff = diffMs;
    if (diffMs < 0) {
        sign = '-';
        // Protect against overflow: -INT_MIN would overflow, cap at INT_MAX
        absDiff = (diffMs == INT_MIN) ? INT_MAX : -diffMs;
    }

    // Always use M:SS.mmm format (consistent with formatLapTime)
    int minutes = absDiff / MS_PER_MINUTE;
    int seconds = (absDiff % MS_PER_MINUTE) / MS_PER_SECOND;
    int ms = absDiff % MS_PER_SECOND;

    snprintf(buffer, bufferSize, "%c%d:%02d.%03d", sign, minutes, seconds, ms);
}

void PluginUtils::formatTimeDiffTenths(char* buffer, size_t bufferSize, int diffMs) {
    using namespace PluginConstants::TimeConversion;

    // Handle sign and protect against INT_MIN overflow
    char sign = '+';
    int absDiff = diffMs;
    if (diffMs < 0) {
        sign = '-';
        // Protect against overflow: -INT_MIN would overflow, cap at INT_MAX
        absDiff = (diffMs == INT_MIN) ? INT_MAX : -diffMs;
    }

    // Use M:SS.s format (tenths of seconds) for live gaps
    int minutes = absDiff / MS_PER_MINUTE;
    int seconds = (absDiff % MS_PER_MINUTE) / MS_PER_SECOND;
    int tenths = (absDiff % MS_PER_SECOND) / 100;  // Convert ms to tenths

    snprintf(buffer, bufferSize, "%c%d:%02d.%d", sign, minutes, seconds, tenths);
}

void PluginUtils::formatLapTimeTenths(int lapTimeMs, char* buffer, size_t bufferSize) {
    if (lapTimeMs >= 0) {
        using namespace PluginConstants::TimeConversion;
        int minutes = lapTimeMs / MS_PER_MINUTE;
        int seconds = (lapTimeMs % MS_PER_MINUTE) / MS_PER_SECOND;
        int tenths = (lapTimeMs % MS_PER_SECOND) / 100;  // Convert ms to tenths

        // Use M:SS.s format (tenths of seconds) for pitboard display
        snprintf(buffer, bufferSize, "%d:%02d.%d", minutes, seconds, tenths);
    }
    else {
        buffer[0] = '\0';
    }
}

void PluginUtils::formatGapCompact(char* buffer, size_t bufferSize, int diffMs) {
    using namespace PluginConstants::TimeConversion;

    // Handle sign and protect against INT_MIN overflow
    char sign = '+';
    int absDiff = diffMs;
    if (diffMs < 0) {
        sign = '-';
        absDiff = (diffMs == INT_MIN) ? INT_MAX : -diffMs;
    }

    int minutes = absDiff / MS_PER_MINUTE;
    int seconds = (absDiff % MS_PER_MINUTE) / MS_PER_SECOND;
    int ms = absDiff % MS_PER_SECOND;

    if (minutes > 0) {
        // >= 1 minute: show "+1:13.345"
        snprintf(buffer, bufferSize, "%c%d:%02d.%03d", sign, minutes, seconds, ms);
    } else {
        // < 1 minute: show "+13.345" (compact format)
        snprintf(buffer, bufferSize, "%c%d.%03d", sign, seconds, ms);
    }
}

void PluginUtils::formatSectorTime(int sectorTimeMs, char* buffer, size_t bufferSize) {
    if (sectorTimeMs >= 0) {
        using namespace PluginConstants::TimeConversion;

        if (sectorTimeMs >= MS_PER_MINUTE) {
            // M:SS.mmm format for sectors >= 1 minute
            int minutes = sectorTimeMs / MS_PER_MINUTE;
            int seconds = (sectorTimeMs % MS_PER_MINUTE) / MS_PER_SECOND;
            int ms = sectorTimeMs % MS_PER_SECOND;
            snprintf(buffer, bufferSize, "%d:%02d.%03d", minutes, seconds, ms);
        } else {
            // SS.mmm format for sectors < 1 minute
            int seconds = sectorTimeMs / MS_PER_SECOND;
            int ms = sectorTimeMs % MS_PER_SECOND;
            snprintf(buffer, bufferSize, "%02d.%03d", seconds, ms);
        }
    } else {
        buffer[0] = '\0';
    }
}

const char* PluginUtils::getEventTypeString(int eventType) {
    namespace Enum = PluginConstants::EventType;
    namespace Str = PluginConstants::DisplayStrings::EventType;

    switch (eventType) {
    case Enum::TESTING: return Str::TESTING;  // Note: Shows as "Open Practice" when online
    case Enum::RACE: return Str::RACE;
    case Enum::STRAIGHT_RHYTHM: return Str::STRAIGHT_RHYTHM;
    default: return Str::UNKNOWN;
    }
}

const char* PluginUtils::getSessionString(int eventType, int session) {
    namespace EventEnum = PluginConstants::EventType;
    namespace SessionEnum = PluginConstants::Session;
    namespace Str = PluginConstants::DisplayStrings::Session;

    if (eventType == EventEnum::TESTING) { // Testing / Open Practice
        switch (session) {
        case SessionEnum::WAITING: return Str::WAITING;
        case SessionEnum::PRACTICE: return Str::PRACTICE;
        default: return Str::UNKNOWN;
        }
    }
    else if (eventType == EventEnum::RACE) { // Race
        switch (session) {
        case SessionEnum::WAITING: return Str::WAITING;
        case SessionEnum::PRACTICE: return Str::PRACTICE;
        case SessionEnum::PRE_QUALIFY: return Str::PRE_QUALIFY;
        case SessionEnum::QUALIFY_PRACTICE: return Str::QUALIFY_PRACTICE;
        case SessionEnum::QUALIFY: return Str::QUALIFY;
        case SessionEnum::WARMUP: return Str::WARMUP;
        case SessionEnum::RACE_1: return Str::RACE_1;
        case SessionEnum::RACE_2: return Str::RACE_2;
        default: return Str::UNKNOWN;
        }
    }
    else if (eventType == EventEnum::STRAIGHT_RHYTHM) { // Straight Rhythm
        switch (session) {
        case SessionEnum::WAITING: return Str::WAITING;
        case SessionEnum::PRACTICE: return Str::PRACTICE;
        case SessionEnum::SR_ROUND: return Str::SR_ROUND;
        case SessionEnum::SR_QUARTER_FINALS: return Str::SR_QUARTER_FINALS;
        case SessionEnum::SR_SEMI_FINALS: return Str::SR_SEMI_FINALS;
        case SessionEnum::SR_FINAL: return Str::SR_FINAL;
        default: return Str::UNKNOWN;
        }
    }
    return Str::UNKNOWN;
}

const char* PluginUtils::getSessionStateString(int sessionState) {
    namespace StateEnum = PluginConstants::SessionState;
    namespace Str = PluginConstants::DisplayStrings::SessionState;

    if (sessionState & StateEnum::CANCELLED) return Str::CANCELLED;
    if (sessionState & StateEnum::RACE_OVER) return Str::RACE_OVER;
    if (sessionState & StateEnum::PRE_START) return Str::PRE_START;
    if (sessionState & StateEnum::SIGHTING_LAP) return Str::SIGHTING_LAP;
    if (sessionState & StateEnum::FINISHED) return Str::COMPLETE;
    if (sessionState & StateEnum::IN_PROGRESS) return Str::IN_PROGRESS;
    return Str::WAITING;
}

const char* PluginUtils::getRiderStateAbbreviation(int riderState) {
    namespace StateEnum = PluginConstants::RiderState;
    namespace Str = PluginConstants::DisplayStrings::RiderState;

    switch (riderState) {
    case StateEnum::DNS: return Str::DNS;
    case StateEnum::UNKNOWN: return Str::UNKNOWN;
    case StateEnum::RETIRED: return Str::RETIRED;
    case StateEnum::DSQ: return Str::DISQUALIFIED;
    default: return "";
    }
}

const char* PluginUtils::getConditionsString(int conditions) {
    namespace CondEnum = PluginConstants::Conditions;
    namespace Str = PluginConstants::DisplayStrings::Conditions;

    switch (conditions) {
    case CondEnum::CLEAR: return Str::CLEAR;
    case CondEnum::CLOUDY: return Str::CLOUDY;
    case CondEnum::RAINY: return Str::RAINY;
    default: return Str::UNKNOWN;
    }
}

const char* PluginUtils::getCommunicationTypeString(int commType) {
    namespace TypeEnum = PluginConstants::CommunicationType;
    namespace Str = PluginConstants::DisplayStrings::CommunicationType;

    switch (commType) {
    case TypeEnum::STATE_CHANGE: return Str::STATE_CHANGE;
    case TypeEnum::PENALTY: return Str::PENALTY;
    default: return Str::UNKNOWN;
    }
}

const char* PluginUtils::getReasonString(int reason) {
    namespace ReasonEnum = PluginConstants::Reason;
    namespace Str = PluginConstants::DisplayStrings::Reason;

    switch (reason) {
    case ReasonEnum::JUMP_START: return Str::JUMP_START;
    case ReasonEnum::TOO_MANY_OFFENCES: return Str::TOO_MANY_OFFENCES;
    case ReasonEnum::DIRECTOR: return Str::DIRECTOR;
    default: return Str::NONE;
    }
}

const char* PluginUtils::getOffenceString(int offence) {
    namespace OffenceEnum = PluginConstants::Offence;
    namespace Str = PluginConstants::DisplayStrings::Offence;

    switch (offence) {
    case OffenceEnum::JUMP_START: return Str::JUMP_START;
    case OffenceEnum::CUTTING: return Str::CUTTING;
    default: return Str::NONE;
    }
}

float PluginUtils::calculateMonospaceTextWidth(int numChars, float fontSize) {
    if (numChars <= 0) return 0.0f;

    using namespace PluginConstants::FontMetrics;

    float cellWidth = fontSize * MONOSPACE_CHAR_WIDTH_RATIO;

    // Width = numChars * cellWidth (no separate spacing - included in cell width)
    return numChars * cellWidth;
}

void PluginUtils::setColumnPosition(uint32_t enabledColumns, uint32_t flag, int width,
                                     float scaledFontSize, float& current, float& target) {
    if (enabledColumns & flag) {
        target = current;
        current += calculateMonospaceTextWidth(width, scaledFontSize);
    } else {
        target = -1.0f;  // Sentinel value for disabled column
    }
}

const char* PluginUtils::getBikeAbbreviationPtr(const char* bikeName) {
    // std::string_view provides zero-allocation lookup
    // Map keys are string_view (just pointer + length), map values are string literals
    // The const char* parameter implicitly converts to string_view at zero cost
    static const std::unordered_map<std::string_view, const char*> bikeMap = {
        // FACTORY bikes
        {"FACTORY 125SX", "125SX"},
        {"FACTORY 150SX", "150SX"},
        {"FACTORY 250SX", "250SX"},
        {"FACTORY CR125", "CR125"},
        {"FACTORY CR144", "CR144"},
        {"FACTORY CR250", "CR250"},
        {"FACTORY CRF250R", "CRF250R"},
        {"FACTORY CRF450R", "CRF450R"},
        {"FACTORY FC250", "FC250"},
        {"FACTORY FC450", "FC450"},
        {"FACTORY KX125", "KX125"},
        {"FACTORY KX144", "KX144"},
        {"FACTORY KX250", "KX250"},
        {"FACTORY KX250F", "KX250F"},
        {"FACTORY KX450F", "KX450F"},
        {"FACTORY MC250", "MC250"},
        {"FACTORY MC450", "MC450"},
        {"FACTORY RM125", "RM125"},
        {"FACTORY RM144", "RM144"},
        {"FACTORY RM250", "RM250"},
        {"FACTORY RMZ250", "RMZ250"},
        {"FACTORY RMZ450", "RMZ450"},
        {"FACTORY TF250", "TF250"},
        {"FACTORY TF450", "TF450"},
        {"FACTORY TM250Fi", "TM250Fi"},
        {"FACTORY TM450Fi", "TM450Fi"},
        {"FACTORY XXF250", "XXF250"},
        {"FACTORY XXF450", "XXF450"},
        {"FACTORY YZ125", "YZ125"},
        {"FACTORY YZ144", "YZ144"},
        {"FACTORY YZ250", "YZ250"},
        {"FACTORY YZ250F", "YZ250F"},
        {"FACTORY YZ450F", "YZ450F"},

        // MX1OEM
        {"Honda CR250 1996", "CR250"},
        {"Honda CR250 1997", "CR250"},
        {"Kawasaki KX250 2002", "KX250"},
        {"Suzuki RM250 2003", "RM250"},
        {"Fantic XX250 2023", "XX250"},
        {"Honda CRF450R 2023", "CRF450R"},
        {"Husqvarna FC 350 2023", "FC_350"},
        {"Husqvarna FC 450 2023", "FC_450"},
        {"Husqvarna TC 250 2023", "TC_250"},
        {"Kawasaki KX450 2023", "KX450"},
        {"KTM 250 SX 2023", "250_SX"},
        {"KTM 350 SX-F 2023", "350_SX-F"},
        {"KTM 450 SX-F 2023", "450_SX-F"},
        {"Suzuki RM-Z450 2023", "RM-Z450"},
        {"TM MX 144 2023", "MX_144"},
        {"TM MX 250 2023", "MX_250"},
        {"TM MX 300 Fi 2023", "MX_300_Fi"},
        {"TM MX 450 Fi 2023", "MX_450_Fi"},
        {"Yamaha YZ250 2023", "YZ250"},
        {"Yamaha YZ450F 2023", "YZ450F"},
        {"Beta RX 450 2024", "RX_450"},
        {"GasGas MC 250 2024", "MC_250"},
        {"GasGas MC 350F 2024", "MC_350F"},
        {"GasGas MC 450F 2024", "MC_450F"},
        {"Fantic XXF 450 2025", "XXF_450"},
        {"Triumph TF 450-X 2025", "TF_450-RC"},

        // MX2OEM
        {"Honda CR125 1996", "CR125"},
        {"Kawasaki KX125 2002", "KX125"},
        {"Suzuki RM125 2003", "RM125"},
        {"Fantic XX125 2023", "XX125"},
        {"Honda CRF250R 2023", "CRF250R"},
        {"Husqvarna FC 250 2023", "FC_250"},
        {"Husqvarna TC 125 2023", "TC_125"},
        {"Kawasaki KX250 2023", "KX250"},
        {"KTM 125 SX 2023", "125_SX"},
        {"KTM 250 SX-F 2023", "250_SX-F"},
        {"Suzuki RM-Z250 2023", "RM-Z250"},
        {"TM MX 125 2023", "MX_125"},
        {"TM MX 250 Fi 2023", "MX_250_Fi"},
        {"Yamaha YZ125 2023", "YZ125"},
        {"GasGas MC 125 2024", "MC_125"},
        {"GasGas MC 250F 2024", "MC_250F"},
        {"Triumph TF 250-X 2024", "TF_250-X"},
        {"Yamaha YZ250F 2024", "YZ250F"},
        {"Fantic XXF 250 2025", "XXF_250"},

        // MX3OEM
        {"KTM Black Knight 2021", "KX_500"},
        {"Beta 300 RX 2022", "300_RX"},
        {"KTM 300 SX 2023", "300_SX"},
        {"TM MX 300 2023", "MX_300"},
        {"TM MX530Fi 2023", "MX_530_Fi"},
        {"Service Honda CR500AF", "CR500AF"},

        // MXEOEM
        {"Alta Redshift MXR 2018", "MXR"},
        {"2023 Stark VARG", "VARG"},

        // MSM bikes
        {"MSM 450SM", "450SM"},
        {"MSM 250XF", "250XF"},
        {"MSM 250X", "250X"},
        {"MSM 350XF", "350XF"},
        {"MSM 450XF", "450XF"}
    };

    // Look up the bike name in the map
    // bikeName (const char*) implicitly converts to string_view at zero cost
    auto it = bikeMap.find(bikeName);
    if (it != bikeMap.end()) {
        return it->second;  // Returns const char* directly (string literal), NO ALLOCATION
    }

    // If not found in map, return static "Unknown" string
    return "Unknown";
}

unsigned long PluginUtils::getBikeBrandColor(const char* bikeName) {
    // std::string_view provides zero-allocation lookup
    // Map keys are string_view (just pointer + length), map values are pre-computed colors
    // The const char* parameter implicitly converts to string_view at zero cost

    // Use brand colors from PluginConstants (single source of truth)
    using namespace PluginConstants::BrandColors;

    static const std::unordered_map<std::string_view, unsigned long> colorMap = {
        // FACTORY bikes
        {"FACTORY 125SX", KTM},
        {"FACTORY 150SX", KTM},
        {"FACTORY 250SX", KTM},
        {"FACTORY CR125", HONDA},
        {"FACTORY CR144", HONDA},
        {"FACTORY CR250", HONDA},
        {"FACTORY CRF250R", HONDA},
        {"FACTORY CRF450R", HONDA},
        {"FACTORY FC250", HUSQVARNA},
        {"FACTORY FC450", HUSQVARNA},
        {"FACTORY KX125", KAWASAKI},
        {"FACTORY KX144", KAWASAKI},
        {"FACTORY KX250", KAWASAKI},
        {"FACTORY KX250F", KAWASAKI},
        {"FACTORY KX450F", KAWASAKI},
        {"FACTORY MC250", GASGAS},
        {"FACTORY MC450", GASGAS},
        {"FACTORY RM125", SUZUKI},
        {"FACTORY RM144", SUZUKI},
        {"FACTORY RM250", SUZUKI},
        {"FACTORY RMZ250", SUZUKI},
        {"FACTORY RMZ450", SUZUKI},
        {"FACTORY TF250", TRIUMPH},
        {"FACTORY TF450", TRIUMPH},
        {"FACTORY TM250Fi", TM},
        {"FACTORY TM450Fi", TM},
        {"FACTORY XXF250", FANTIC},
        {"FACTORY XXF450", FANTIC},
        {"FACTORY YZ125", YAMAHA},
        {"FACTORY YZ144", YAMAHA},
        {"FACTORY YZ250", YAMAHA},
        {"FACTORY YZ250F", YAMAHA},
        {"FACTORY YZ450F", YAMAHA},

        // MX1OEM
        {"Honda CR250 1996", HONDA},
        {"Honda CR250 1997", HONDA},
        {"Kawasaki KX250 2002", KAWASAKI},
        {"Suzuki RM250 2003", SUZUKI},
        {"Fantic XX250 2023", FANTIC},
        {"Honda CRF450R 2023", HONDA},
        {"Husqvarna FC 350 2023", HUSQVARNA},
        {"Husqvarna FC 450 2023", HUSQVARNA},
        {"Husqvarna TC 250 2023", HUSQVARNA},
        {"Kawasaki KX450 2023", KAWASAKI},
        {"KTM 250 SX 2023", KTM},
        {"KTM 350 SX-F 2023", KTM},
        {"KTM 450 SX-F 2023", KTM},
        {"Suzuki RM-Z450 2023", SUZUKI},
        {"TM MX 144 2023", TM},
        {"TM MX 250 2023", TM},
        {"TM MX 300 Fi 2023", TM},
        {"TM MX 450 Fi 2023", TM},
        {"Yamaha YZ250 2023", YAMAHA},
        {"Yamaha YZ450F 2023", YAMAHA},
        {"Beta RX 450 2024", BETA},
        {"GasGas MC 250 2024", GASGAS},
        {"GasGas MC 350F 2024", GASGAS},
        {"GasGas MC 450F 2024", GASGAS},
        {"Fantic XXF 450 2025", FANTIC},
        {"Triumph TF 450-X 2025", TRIUMPH},

        // MX2OEM
        {"Honda CR125 1996", HONDA},
        {"Kawasaki KX125 2002", KAWASAKI},
        {"Suzuki RM125 2003", SUZUKI},
        {"Fantic XX125 2023", FANTIC},
        {"Honda CRF250R 2023", HONDA},
        {"Husqvarna FC 250 2023", HUSQVARNA},
        {"Husqvarna TC 125 2023", HUSQVARNA},
        {"Kawasaki KX250 2023", KAWASAKI},
        {"KTM 125 SX 2023", KTM},
        {"KTM 250 SX-F 2023", KTM},
        {"Suzuki RM-Z250 2023", SUZUKI},
        {"TM MX 125 2023", TM},
        {"TM MX 250 Fi 2023", TM},
        {"Yamaha YZ125 2023", YAMAHA},
        {"GasGas MC 125 2024", GASGAS},
        {"GasGas MC 250F 2024", GASGAS},
        {"Triumph TF 250-X 2024", TRIUMPH},
        {"Yamaha YZ250F 2024", YAMAHA},
        {"Fantic XXF 250 2025", FANTIC},

        // MX3OEM
        {"KTM Black Knight 2021", KTM},
        {"Beta 300 RX 2022", BETA},
        {"KTM 300 SX 2023", KTM},
        {"TM MX 300 2023", TM},
        {"TM MX530Fi 2023", TM},
        {"Service Honda CR500AF", HONDA},

        // MXEOEM
        {"Alta Redshift MXR 2018", ALTA},
        {"2023 Stark VARG", STARK},

        // MSM bikes
        {"MSM 450SM", DEFAULT},
        {"MSM 250XF", DEFAULT},
        {"MSM 250X", DEFAULT},
        {"MSM 350XF", DEFAULT},
        {"MSM 450XF", DEFAULT}
    };

    // Look up the bike name in the map
    auto it = colorMap.find(bikeName);
    if (it != colorMap.end()) {
        return it->second;  // Returns pre-computed color value, NO ALLOCATION
    }

    // If not found in map, return default gray
    return DEFAULT;
}

bool PluginUtils::matchRiderName(const char* entryName, const char* playerName, size_t maxEntryLen) {
    if (!entryName || !playerName || entryName[0] == '\0' || playerName[0] == '\0') {
        return false;
    }

    size_t entryNameLen = strlen(entryName);

    // Try exact match first (existing behavior with truncation handling)
    // The game truncates names in RaceAddEntry to ~31 chars, but EventInit sends full name.
    // Use strncmp with the entry name length to handle long nicknames correctly.
    if (strncmp(entryName, playerName, entryNameLen) == 0
        && (playerName[entryNameLen] == '\0' || entryNameLen >= maxEntryLen)) {
        return true;
    }

    // Fallback: Check for server-forced rating prefix pattern (e.g., "B1 | Thomas")
    // Pattern: alphanumeric rating + " | " + original name
    const char* pipePos = strstr(entryName, " | ");
    if (pipePos) {
        const char* nameAfterPrefix = pipePos + 3;  // Skip " | "

        // Verify prefix is alphanumeric (rating like "B1", "A3", "C2", etc.)
        bool validPrefix = true;
        for (const char* p = entryName; p < pipePos; ++p) {
            char c = *p;
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                validPrefix = false;
                break;
            }
        }

        if (validPrefix && nameAfterPrefix[0] != '\0') {
            size_t nameAfterPrefixLen = strlen(nameAfterPrefix);

            // Try matching the name after the prefix
            if (strncmp(nameAfterPrefix, playerName, nameAfterPrefixLen) == 0
                && (playerName[nameAfterPrefixLen] == '\0' || nameAfterPrefixLen >= maxEntryLen)) {
                return true;
            }
        }
    }

    return false;
}
