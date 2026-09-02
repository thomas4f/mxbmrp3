// ============================================================================
// core/event_log_types.h
// Data structures for the Event Log system - stores notable race events
// ============================================================================
#pragma once

#include <cstdint>
#include <chrono>

// Types of events that can be logged.
// APPEND-ONLY: the HTTP server emits the raw integer value of this enum as the
// event "type" (see http_server.cpp), and the web overlay's EVENT_TYPE_MAP
// (mxbmrp3_data/web/js/overlay-render.js) indexes that value POSITIONALLY to pick its filter
// key. Inserting a value mid-list silently shifts every later event to the wrong
// filter/label in the overlay. Add new types at the end and update EVENT_TYPE_MAP.
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

    // Broadcast
    Director,             // Auto-director decision/state change (cut, lock, manual, enable)
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
    EVENT_DIRECTOR           = 1 << 14,

    EVENT_DEFAULT = EVENT_SESSION_STARTED | EVENT_FASTEST_LAP
                  | EVENT_RIDER_RETIRED | EVENT_RIDER_DSQ
                  | EVENT_OVERTIME | EVENT_FINAL_LAP | EVENT_RIDER_FINISHED
                  | EVENT_LEADER_CHANGE | EVENT_SESSION_STATE,
                  // Note: EVENT_DIRECTOR is intentionally NOT in the defaults — it is an
                  // opt-in broadcaster tool (director cuts are frequent), enabled per-session.

    EVENT_ALL = (1 << 15) - 1  // All 15 event type bits
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
    case EventLogType::Director:           return EVENT_DIRECTOR;
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

    // Optional icon-color override: a ColorSlot value (stored as int) that replaces the
    // per-type default in EventLogHud::getIconColorForEvent(). -1 = use the type default.
    // Used to tint specific director state-transition entries (lock / enabled / manual / ...)
    // with the director button's state colors; see DirectorManager::logDirectorEvent().
    // In-game only: the web overlay picks colors client-side and ignores this.
    int iconColorSlot = -1;

    // The rider this event is ABOUT, or -1 for a session-level event (green flag, overtime,
    // time expired). Set purely so the Event Log can offer click-to-spectate on the row, the
    // way Standings and Map do on theirs; nothing about the rendered text depends on it. The
    // message is pre-formatted ("#42 fastest lap"), so the number could not be recovered
    // from it afterwards — hence a field rather than parsing.
    //
    // A rider number here does NOT mean the row is clickable: retirements and DSQs name a
    // rider who can no longer be spectated. PluginData::isRiderSpectatable() is the gate.
    int raceNum = -1;

    EventLogEntry()
        : type(EventLogType::SessionStarted), sessionTimeMs(0) {
        message[0] = '\0';
        detail[0] = '\0';
    }
};

// The NUMBERS behind an event, carried alongside the display strings rather
// than recovered from them. Every number here exists structurally in the
// handler that logs the event; parsing it back out of EventLogEntry::detail
// would make the display formats load-bearing for AUDIO (rewording the penalty
// column would change what a voice says), duplicate the lap-time reading across
// backends, and give every consumer a "not that shape" path for data that was
// never in doubt.
//
// Defaulted throughout: an event with no number attached is the common case,
// and -1 means "not applicable".
struct EventNumbers {
    int lapTimeMs = -1;   // FastestLap: the lap just set
    int penaltyMs = -1;   // Penalty: the amount, as the game reports it
    int bonusLaps = -1;   // OvertimeStarted: the bonus laps after the clock
    int position = -1;    // RiderFinished: where they finished

    // BUILD ONE THROUGH THESE, not with a braced list. Four same-typed ints in
    // a row means `{-1, 5000}` is a penalty only by counting commas, and
    // inserting a field in the middle would silently reassign every existing
    // site with no diagnostic — the compiler cannot tell a penalty from a lap
    // time. (Designated initialisers would pin it too, but this builds as
    // C++17 for MSVC, where they are not available.)
    static EventNumbers lapTime(int ms)     { EventNumbers n; n.lapTimeMs = ms;  return n; }
    static EventNumbers penalty(int ms)     { EventNumbers n; n.penaltyMs = ms;  return n; }
    static EventNumbers bonus(int laps)     { EventNumbers n; n.bonusLaps = laps; return n; }
    static EventNumbers finished(int pos)   { EventNumbers n; n.position = pos;  return n; }
};

// Note: ring buffer capacity is PluginConstants::HudLimits::MAX_EVENT_LOG_CAPACITY
