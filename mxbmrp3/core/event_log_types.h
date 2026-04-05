// ============================================================================
// core/event_log_types.h
// Data structures for the Event Log system - stores notable race events
// ============================================================================
#pragma once

#include <cstdint>
#include <chrono>

// Types of events that can be logged
enum class EventLogType : uint8_t {
    // Session lifecycle
    SessionStarted,       // Session went to green / in-progress
    SessionStateChange,   // Cancelled, etc.
    SessionPreStart,      // Pre-start / sighting lap (waiting for green)
    SessionComplete,      // Session finished / race over

    // Lap events
    FastestLap,           // Rider set overall fastest lap (bestFlag == 2)

    // Penalties and state changes
    Penalty,              // Rider received a penalty
    PenaltyClear,         // Penalty cleared (GP Bikes, WRS, KRP)
    PenaltyChange,        // Penalty modified (GP Bikes, WRS, KRP)
    RiderRetired,         // Rider retired from session
    RiderDSQ,             // Rider disqualified
    RiderDNS,             // Rider did not start

    // Race progress
    OvertimeStarted,      // Time+laps race entered overtime
    SessionTimeExpired,   // Non-race session timer reached zero (practice/warmup/qualifying)
    FinalLap,             // Rider started final lap
    RiderFinished,        // Rider finished the race
    LeaderChange,         // Race lead changed to a different rider

    // Pit events
    PitEntry,             // Rider entered pits
    PitExit,              // Rider left pits
};

// Bitfield flags for enabling/disabling event types in settings
enum EventLogFlags : uint32_t {
    EVENT_SESSION_STARTED    = 1 << 0,
    EVENT_SESSION_STATE      = 1 << 1,
    EVENT_FASTEST_LAP        = 1 << 2,
    EVENT_PENALTY            = 1 << 3,
    EVENT_PENALTY_CLEAR      = 1 << 4,
    EVENT_RIDER_RETIRED      = 1 << 5,
    EVENT_RIDER_DSQ          = 1 << 6,
    EVENT_RIDER_DNS          = 1 << 7,
    EVENT_OVERTIME           = 1 << 8,
    EVENT_FINAL_LAP          = 1 << 9,
    EVENT_RIDER_FINISHED     = 1 << 10,
    EVENT_LEADER_CHANGE      = 1 << 11,
    EVENT_PIT_ENTRY          = 1 << 12,
    EVENT_PIT_EXIT           = 1 << 13,

    EVENT_DEFAULT = EVENT_SESSION_STARTED | EVENT_FASTEST_LAP
                  | EVENT_RIDER_RETIRED | EVENT_RIDER_DSQ
                  | EVENT_OVERTIME | EVENT_FINAL_LAP | EVENT_RIDER_FINISHED
                  | EVENT_LEADER_CHANGE | EVENT_SESSION_STATE,

    EVENT_ALL = (1 << 14) - 1  // All 14 event type bits
};

// Map EventLogType to its corresponding flag bit
inline uint32_t eventLogTypeToFlag(EventLogType type) {
    switch (type) {
    case EventLogType::SessionStarted:    return EVENT_SESSION_STARTED;
    case EventLogType::SessionStateChange:return EVENT_SESSION_STATE;
    case EventLogType::SessionPreStart:   return EVENT_SESSION_STATE;
    case EventLogType::SessionComplete:   return EVENT_SESSION_STATE;
    case EventLogType::FastestLap:        return EVENT_FASTEST_LAP;
    case EventLogType::Penalty:           return EVENT_PENALTY;
    case EventLogType::PenaltyClear:      return EVENT_PENALTY_CLEAR;
    case EventLogType::PenaltyChange:     return EVENT_PENALTY_CLEAR;  // Shares flag with PenaltyClear — both controlled by "Penalties" toggle
    case EventLogType::RiderRetired:      return EVENT_RIDER_RETIRED;
    case EventLogType::RiderDSQ:          return EVENT_RIDER_DSQ;
    case EventLogType::RiderDNS:          return EVENT_RIDER_DNS;
    case EventLogType::OvertimeStarted:   return EVENT_OVERTIME;
    case EventLogType::SessionTimeExpired:return EVENT_OVERTIME;  // Intentionally shares flag — one "Time expired" toggle
    case EventLogType::FinalLap:          return EVENT_FINAL_LAP;
    case EventLogType::RiderFinished:     return EVENT_RIDER_FINISHED;
    case EventLogType::LeaderChange:      return EVENT_LEADER_CHANGE;
    case EventLogType::PitEntry:          return EVENT_PIT_ENTRY;
    case EventLogType::PitExit:           return EVENT_PIT_EXIT;
    default: return 0;
    }
}

// A single event log entry
struct EventLogEntry {
    EventLogType type;
    int sessionTimeMs;                  // Session time when event occurred (milliseconds)
    std::chrono::steady_clock::time_point steadyTime;  // Monotonic time for auto-hide timing
    std::chrono::system_clock::time_point systemTime;  // Wall clock time for display formatting
    char message[64];                   // Event text (e.g., "#42 set fastest lap")
    char detail[20];                    // Optional detail in PRIMARY color (e.g., "1:48.231", "03:00 + 2L")

    EventLogEntry()
        : type(EventLogType::SessionStarted), sessionTimeMs(0) {
        message[0] = '\0';
        detail[0] = '\0';
    }
};

// Note: ring buffer capacity is PluginConstants::HudLimits::MAX_EVENT_LOG_CAPACITY
