// ============================================================================
// core/plugin_utils.h
// Utility functions for formatting, string conversion, and calculations
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <sstream>

class PluginUtils {
public:
    static void formatTimeMinutesSeconds(int milliseconds, char* buffer, size_t bufferSize);

    // Format lap time as "M:SS.mmm" (or "MM:SS.mmm" for times >= 10 minutes)
    // Used for all time displays: lap times, sectors, splits
    static void formatLapTime(int lapTimeMs, char* buffer, size_t bufferSize);

    // Format time difference/gap as "+/-M:SS.mmm" (or "+/-MM:SS.mmm" for >= 10 minutes)
    // Handles positive, negative, and zero differences
    static void formatTimeDiff(char* buffer, size_t bufferSize, int diffMs);

    // Format time difference with tenths of seconds (M:SS.s format) for live gaps
    // Uses lower precision for cleaner display and reduced visual noise
    static void formatTimeDiffTenths(char* buffer, size_t bufferSize, int diffMs);

    // Format lap time with tenths of seconds (M:SS.s format) for pitboard display
    // Uses lower precision for cleaner display
    static void formatLapTimeTenths(int lapTimeMs, char* buffer, size_t bufferSize);

    // Format gap as compact string: "+13.3" for <1min, "+1:13.3" for >=1min
    // Single decimal (tenths) for cleaner pitboard-style display
    static void formatGapCompact(char* buffer, size_t bufferSize, int diffMs);

    // Format sector time as "SS.mmm" (for sectors under 1 minute)
    // Used by RecordsHud for MXB-Ranked sector times
    static void formatSectorTime(int sectorTimeMs, char* buffer, size_t bufferSize);

    static const char* getEventTypeString(int eventType);
    static const char* getSessionString(int eventType, int session);
    static const char* getSessionStateString(int sessionState);
    static const char* getRiderStateAbbreviation(int riderState);
    static const char* getConditionsString(int conditions);
    static const char* getCommunicationTypeString(int commType);
    static const char* getReasonString(int reason);
    static const char* getOffenceString(int offence);

    static const char* getBikeAbbreviationPtr(const char* bikeName);
    static unsigned long getBikeBrandColor(const char* bikeName);
    static float calculateMonospaceTextWidth(int numChars, float fontSize);

    // Match rider names, handling server-forced rating prefixes (e.g., "B1 | Thomas" matches "Thomas")
    // entryName: name from RaceAddEntry (may have prefix)
    // playerName: name from EventInit (original name)
    // maxEntryLen: maximum length for entry name comparison (handles game truncation)
    // Returns true if names match (exact or with prefix stripped)
    static bool matchRiderName(const char* entryName, const char* playerName, size_t maxEntryLen);

    // Column position helper - used by standings and lap log HUDs
    // Sets target column position if flag is enabled, or -1.0 if disabled
    // Advances current position by the column width if enabled
    static void setColumnPosition(uint32_t enabledColumns, uint32_t flag, int width,
                                   float scaledFontSize, float& current, float& target);

    // Color conversion utility
    static constexpr unsigned long makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return (static_cast<unsigned long>(a) << 24) |
            (static_cast<unsigned long>(b) << 16) |
            (static_cast<unsigned long>(g) << 8) |
            static_cast<unsigned long>(r);
    }

    // Apply opacity to an existing color (extracts RGB, applies alpha)
    // Example: applyOpacity(color, 0.5f) -> semi-transparent color
    static constexpr unsigned long applyOpacity(unsigned long baseColor, float opacity) {
        uint8_t r = baseColor & 0xFF;
        uint8_t g = (baseColor >> 8) & 0xFF;
        uint8_t b = (baseColor >> 16) & 0xFF;
        uint8_t a = static_cast<uint8_t>(opacity * 255.0f);
        return makeColor(r, g, b, a);
    }

    // Lighten a color by blending toward white
    // factor: 0.0 = original color, 1.0 = white
    static constexpr unsigned long lightenColor(unsigned long baseColor, float factor) {
        uint8_t r = baseColor & 0xFF;
        uint8_t g = (baseColor >> 8) & 0xFF;
        uint8_t b = (baseColor >> 16) & 0xFF;
        uint8_t a = (baseColor >> 24) & 0xFF;
        r = static_cast<uint8_t>(r + (255 - r) * factor);
        g = static_cast<uint8_t>(g + (255 - g) * factor);
        b = static_cast<uint8_t>(b + (255 - b) * factor);
        return makeColor(r, g, b, a);
    }

    // Darken a color by multiplying RGB values
    // factor: 1.0 = original color, 0.0 = black
    static constexpr unsigned long darkenColor(unsigned long baseColor, float factor) {
        uint8_t r = baseColor & 0xFF;
        uint8_t g = (baseColor >> 8) & 0xFF;
        uint8_t b = (baseColor >> 16) & 0xFF;
        uint8_t a = (baseColor >> 24) & 0xFF;
        r = static_cast<uint8_t>(r * factor);
        g = static_cast<uint8_t>(g * factor);
        b = static_cast<uint8_t>(b * factor);
        return makeColor(r, g, b, a);
    }

    // Format color as hex string (e.g., "0xFFFFFFFF")
    static std::string formatColorHex(uint32_t color) {
        std::ostringstream oss;
        oss << "0x" << std::hex << color;
        return oss.str();
    }

    // Parse color from hex string (e.g., "0xFFFFFFFF" or "4294967295")
    static uint32_t parseColorHex(const std::string& value) {
        return static_cast<uint32_t>(std::stoul(value, nullptr, 0));
    }

    // Get color for a rider based on their position relative to player
    // Returns: neutralColor (ahead), warningColor (behind), with lightened/darkened variants for lap differences
    static unsigned long getRelativePositionColor(int playerPosition, int riderPosition,
                                                   int playerLaps, int riderLaps,
                                                   unsigned long neutralColor, unsigned long warningColor,
                                                   unsigned long fallbackColor) {
        if (playerPosition <= 0 || riderPosition <= 0) {
            return fallbackColor;
        }

        int lapDiff = riderLaps - playerLaps;

        if (riderPosition < playerPosition) {
            // Rider is ahead
            return (lapDiff >= 1) ? lightenColor(neutralColor, 0.5f) : neutralColor;
        } else {
            // Rider is behind
            return (lapDiff <= -1) ? darkenColor(warningColor, 0.7f) : warningColor;
        }
    }

private:
    PluginUtils() = delete;
    ~PluginUtils() = delete;
    PluginUtils(const PluginUtils&) = delete;
    PluginUtils& operator=(const PluginUtils&) = delete;
};
