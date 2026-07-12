// ============================================================================
// hud/event_log_hud.h
// Event Log HUD - displays a scrolling log of notable race events
// Shows timestamped events like fastest laps, penalties, retirements, etc.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/event_log_types.h"
#include <chrono>

class EventLogHud : public BaseHud {
public:
    // Display mode (visibility behavior)
    enum class DisplayMode : uint8_t {
        OFF = 0,        // Event log hidden
        ON = 1,         // Always visible
        AUTO_HIDE = 2   // Show when new events arrive, fade after timeout
    };

    // Display order for event entries
    enum class DisplayOrder : uint8_t {
        OLDEST_FIRST = 0,  // Oldest at top, newest at bottom
        NEWEST_FIRST = 1   // Newest at top, oldest at bottom
    };

    // Timestamp display mode
    enum class TimestampMode : uint8_t {
        OFF = 0,        // No timestamp
        SESSION = 1,    // Session timer (MM:SS)
        CLOCK = 2       // Wall clock (HH:MM:SS)
    };

    // Row count limits
    static constexpr int MIN_DISPLAY_EVENTS = 1;
    static constexpr int MAX_DISPLAY_EVENTS = 50;

    // Auto-hide duration bounds (milliseconds)
    static constexpr int MIN_AUTO_HIDE_MS = 1000;
    static constexpr int DEFAULT_AUTO_HIDE_MS = 5000;
    static constexpr int MAX_AUTO_HIDE_MS = 30000;
    static constexpr int AUTO_HIDE_STEP_MS = 1000;

    EventLogHud();
    virtual ~EventLogHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-eventlog"; }
    void resetToDefaults();

    // Enable/disable a single event type's display-filter bit (used by test hooks; the
    // settings UI edits m_enabledEvents directly via its friend access).
    void setEventTypeEnabled(EventLogType type, bool on) {
        uint32_t flag = eventLogTypeToFlag(type);
        if (on) m_enabledEvents |= flag; else m_enabledEvents &= ~flag;
        setDataDirty();
    }

    // Allow settings system to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Calculate dynamic background width (in characters)
    int getBackgroundWidthChars() const;

    // HUD positioning
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int ICON_COL_WIDTH = 3;      // Icon column (matches StandingsHud COL_TRACKED_WIDTH)
    static constexpr int TIMESTAMP_WIDTH = 9;    // "05:00   " (session) or "14:32:01 " (clock) + gap
    static constexpr int MESSAGE_WIDTH = 31;     // Event message text (total = icon 3 + timestamp 9 + message 31 = 43)

    // Icon rendering (matches StandingsHud sizing: 0.006f * scale)
    static constexpr float ICON_BASE_SIZE = 0.006f;

    // Get icon sprite index for an event type (0 = no icon)
    int getIconForEvent(EventLogType type) const;

    // Get icon color for an event type
    unsigned long getIconColorForEvent(EventLogType type) const;

    // Settings
    DisplayMode m_displayMode = DisplayMode::ON;
    DisplayOrder m_displayOrder = DisplayOrder::OLDEST_FIRST;
    uint32_t m_enabledEvents = EVENT_DEFAULT;
    int m_maxDisplayEvents = 6;
    int m_autoHideDurationMs = DEFAULT_AUTO_HIDE_MS;
    TimestampMode m_timestampMode = TimestampMode::CLOCK;
    bool m_showIcons = true;

    // Tracked icon quads for rebuildLayout repositioning (avoids lag during drag)
    // Each entry stores the quad index in m_quads and the row index (0-based from first data row)
    struct IconQuadInfo {
        size_t quadIndex;
        int rowIndex;
    };
    std::vector<IconQuadInfo> m_iconQuads;

    // Auto-hide state
    std::chrono::steady_clock::time_point m_lastEventTime;
    bool m_hasEvents = false;

    // Cached state for layout optimization
    int m_cachedNumDataRows = 0;

    // Cached icon sprite indices (avoid string-based AssetManager map lookups per
    // row per rebuild; resolved once, lazily, via ensureInitialized()).
    struct CachedIcons {
        int flag = 0;
        int flagCheckered = 0;
        int hourglassHalf = 0;
        int stopwatch = 0;
        int exclamation = 0;
        int circleXmark = 0;
        int xmark = 0;
        int ban = 0;
        int crown = 0;
        int wrench = 0;
        int video = 0;
        bool initialized = false;

        void ensureInitialized();
    };
    mutable CachedIcons m_iconCache;
};
