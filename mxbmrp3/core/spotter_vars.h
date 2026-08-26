// ============================================================================
// core/spotter_vars.h
// The {variables} a pack's cue templates can use, and the one table that
// defines them. Pure (values in, string out) so the unit suite drives it
// directly (test_spotter_vars.cpp); SpotterManager fills the values from
// PluginData at the moment a cue fires.
//
// WHY A TABLE. Variables used to be positional arguments threaded through
// emitCue - five of them, at twelve parameters and climbing, and each was
// filled only by the ONE emitter that happened to know it. So {position} worked in
// the position report and nowhere else, and writing
// `overtime_started = You're {position}, {overtime_laps} to go.` silently produced "You're , 2 laps
// to go." A pack author has no way to discover that from the outside.
//
// So the rule is now: EVERY variable is available in EVERY cue. They are
// resolved from live state when the cue fires, not carried by the event that
// fired it. Adding one is a row here plus a line where Vars is filled.
//
// "AVAILABLE" IS NOT "ALWAYS MEANINGFUL", and conflating the two is the trap
// this design has to avoid. P1 has no rider ahead; a first lap has no last lap
// time; a practice session has no laps remaining. A variable in that state
// resolves to EMPTY and the punctuation around it is tidied up (see
// SpotterCuePack::expand), so one template reads correctly in both cases:
//
//   lap_completed = P {position}, {gap_to_ahead} to {rider_ahead}.
//     leading   -> "P one."
//     mid-pack  -> "P four, one point two to rider sixty five."
//
// That is the same contract {event_rider} and {event_time} always had; it is now the
// contract for all of them. What must never happen is a template printing
// "{gap_to_ahead}" literally - an unknown name is left alone deliberately (it is
// probably a typo, and showing it is how the author finds out), but a KNOWN
// name always resolves, if only to nothing.
//
// TEXT ONLY. These feed the subtitle and text-to-speech. The wav-stitching
// path (spotter_mix.h) understands a smaller set - the ones with a numeric
// form it can assemble from num_*.wav - because a recorded pack can only say
// words it has clips for. A text pack, which is what ships, has no such limit.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace SpotterVars {

// Every value a template can name, already in SPOKEN form ("four seventy six",
// "one point two") because the composition happens where the numbers are.
// Empty = not applicable right now, which is a normal state for most of these.
struct Vars {
    // ---- what the EVENT carries --------------------------------------------
    // Prefixed because they belong to whatever fired the cue rather than to a
    // rider you could name: {event_rider} sits beside {rider_ahead} and your
    // own {rider_name}, and {event_time} is a lap time on some cues and a gap
    // on others.
    std::string eventRider;       // {event_rider}
    std::string eventTime;        // {event_time}
    std::string penaltySeconds;   // {penalty_seconds}
    // Your ACCUMULATED penalty, the figure the standings' penalty column
    // shows - as against {penalty_seconds}, which is the one penalty the cue
    // fired on.
    //
    // Read from the standings, which absorb a new penalty on the NEXT
    // classification rather than on the communication that announces it - so
    // on the crossing cues this is simply current, and on penalty_you the cue
    // supplies it itself, from a tally that includes the penalty being
    // announced (see the Penalty branch in SpotterManager::onRaceEvent). It is
    // deliberately EMPTY on a first penalty, where the total is the amount and
    // saying both reads as a stutter.
    std::string penaltyTotal;     // {penalty_total}
    std::string overtimeLaps;     // {overtime_laps}
    // What the cue fired ON: places against the grid, or against your last
    // crossing when you joined mid-race and there is no grid to measure from.
    // Deliberately the ambiguous one - it is "what just happened" - with the
    // three unambiguous references below it.
    std::string positionsChanged; // {positions_changed}  unsigned
    // How the EVENT's lap compares with your own - the rival's fastest lap
    // against your best and your last. The {gap_to_*} family below answers a
    // different question (YOUR last lap versus a reference), so these are the
    // event's, and carry the event_ prefix for the same reason {event_time}
    // does. Empty unless the cue carries another rider's LAP time: a sector
    // cue's time is a split, and comparing that with a lap would be nonsense.
    std::string eventGapToBestLap;  // {event_gap_to_best_lap}
    std::string eventGapToLastLap;  // {event_gap_to_last_lap}
    std::string sectorNumber;     // {sector_number}
    std::string sectorDuration;   // {sector_duration}  this sector on its own

    // ---- you ---------------------------------------------------------------
    std::string riderName;    // {rider_name}     your name as the game has it
    std::string position;     // {position}
    std::string lapNumber;    // {lap_number}     the lap you are on
    std::string lastLapTime;  // {last_lap_time}
    std::string gapToLeader;  // {gap_to_leader}
    std::string fuelLaps;     // {fuel_laps}  laps left in the tank
    // Your total race time, and empty until you have actually finished - it
    // is the figure the classification carries at the flag, not a running
    // clock, so a template asking for it mid-race correctly says nothing.
    std::string finishTime;   // {finish_time}
    // The setup you are on. Normalised so it is never empty while on track:
    // the game reports an unnamed setup as "" and a stock one as "Default",
    // which mean the same thing to a rider.
    std::string setupName;    // {setup_name}

    // Places made up or lost against each of the three references
    // StandingsHud's PosGain column offers, all unsigned - a template picks
    // the one it means instead of inheriting a choice. The race-start pair is
    // empty outside a race, having no grid to measure from.
    std::string positionsSinceStart;   // {positions_since_start}
    std::string positionsSinceLap;     // {positions_since_lap}
    std::string positionsSinceSector;  // {positions_since_sector}

    // ---- the reference laps ------------------------------------------------
    // The same five TimingHud compares against, each as a TIME and as your
    // last lap's gap TO it. The gap carries its own direction word ("point
    // three quicker") rather than a sign, because a bare number spoken aloud
    // does not say which way it went and every template would have to add the
    // word itself.
    //
    // Any of them can be empty and routinely is: no all-time best on a track
    // you have never ridden, no ideal lap until you have set every sector, no
    // record unless the provider has one for this track (MX Bikes only), no
    // gap at all until you have completed a lap.
    std::string bestLapTime;      // {best_lap_time}     your best this session
    std::string alltimeBestTime;  // {alltime_best_time} your best ever here
    std::string idealLapTime;     // {ideal_lap_time}    your best sectors summed
    std::string overallBestTime;  // {overall_best_time} anyone's best this session
    std::string recordTime;       // {record_time}       the track record
    std::string gapToBestLap;     // {gap_to_best_lap}
    std::string gapToAlltime;     // {gap_to_alltime}
    std::string gapToIdeal;       // {gap_to_ideal}
    std::string gapToOverall;     // {gap_to_overall}
    std::string gapToRecord;      // {gap_to_record}
    std::string gapToLastLap;     // {gap_to_last_lap}   the lap before it

    // ---- the same choice, one sector at a time -----------------------------
    // "Delta to WHAT" is the whole question, so each reference is its own
    // name rather than one variable with a hidden answer. Same four the
    // Timing HUD's sector view offers, and each empty until there is
    // something to compare against.
    // ACCUMULATED, not the sector on its own - the elapsed lap time at this
    // split, against the reference lap's elapsed time at the same split. That
    // is what TimingHud puts on screen ("S2: 60.00") and what players read a
    // split as, so a spotter using bare sector durations would be quoting a
    // different number from the one in front of them for the same crossing.
    // {sector_duration} is there for anyone who wants the sector alone.
    // Each carries its DIRECTION as a word - "zero point three quicker" -
    // because the number alone is the same string either way, and which way is
    // the only part a rider acts on. The sector_completed_faster/_slower cue
    // keys say it too, for a recorded pack that wants a different take per
    // direction; a text pack can use either.
    std::string sectorDeltaBestLap;  // {sector_delta_best_lap}  your best lap this session
    std::string sectorDeltaIdeal;    // {sector_delta_ideal}     your best sectors summed
    std::string sectorDeltaLastLap;  // {sector_delta_last_lap}
    std::string sectorDeltaAlltime;  // {sector_delta_alltime}   your best lap ever here
    std::string sectorDeltaRecord;   // {sector_delta_record}    the track record

    // The two that mark a MOMENT rather than describing every split. Both are
    // unsigned and carry no direction word, because their cue keys only fire
    // in the one direction: you are up, or you took time off. See the
    // on_pace_* and sector_best cues in spotter_manager.cpp.
    std::string paceMargin;      // {pace_margin}       how far up at the last split
    std::string sectorBestDelta; // {sector_best_delta} taken off your best for that sector

    // ---- the riders either side of you -------------------------------------
    std::string positionAhead;   // {position_ahead}
    std::string riderAhead;      // {rider_ahead}
    // Seconds while you are on the same lap, LAPS once you are not - a
    // lapped rider's millisecond gap is meaningless, and standings keep the
    // real answer in gapLaps. On the gap cues these carry that cue's own
    // stopwatch measurement instead of the live estimate.
    std::string gapToAhead;      // {gap_to_ahead}
    std::string gainedOnAhead;   // {gained_on_ahead}  since the last shared point
    std::string trendAhead;      // {trend_ahead}      "gained" / "lost"
    // What they actually just ran. A gap says where they are; this says whether
    // you can do anything about it -- and it is the one figure the standings show
    // for them (StandingsHud's Last column) that the spotter could not say.
    //
    // Of the rider the CUE names, which on the gap cues is the one its stopwatch
    // timed rather than whoever the classification lists in that slot by the time
    // the deferred report speaks. Same rule the name itself follows, for the same
    // reason: the number and the name have to be about one rider.
    std::string lastLapAhead;    // {last_lap_ahead}
    std::string positionBehind;  // {position_behind}
    std::string riderBehind;     // {rider_behind}
    std::string gapToBehind;     // {gap_to_behind}
    std::string gainedOnBehind;  // {gained_on_behind}
    std::string trendBehind;     // {trend_behind}     "closed" / "dropped back"
    std::string lastLapBehind;   // {last_lap_behind}

    // ---- the session --------------------------------------------------------
    // length/remaining answer in whatever unit this session is measured in, so
    // a line using them reads correctly everywhere; {laps_remaining} and
    // {time_remaining} answer only where they specifically apply.
    //
    // THREE shapes, and the third is the common one in MX Bikes: a clock
    // ("20 minutes"), a distance ("5 laps"), or BOTH - "10 minutes plus two
    // laps", a clock followed by that many bonus laps. Reporting only the
    // clock there drops half the format.
    //
    // Note that {laps_remaining} is a lap-RACE figure. In a time+laps race the
    // session's lap count is the bonus count rather than the distance, so
    // there is no laps-remaining until overtime starts, at which point it is
    // the leader's.
    //
    // All three remaining figures are empty until the session is IN PROGRESS -
    // before the start the whole distance is still ahead, and "what is left" is
    // {session_length}, said by session_prestart. See fillAmbientVars().
    std::string sessionName;       // {session_name}   "Race 1", "Warmup", ...
    std::string sessionState;      // {session_state}  "In Progress", "Cancelled", ...
    std::string sessionLength;     // {session_length}
    std::string sessionRemaining;  // {session_remaining}
    std::string lapsRemaining;     // {laps_remaining}
    std::string timeRemaining;     // {time_remaining}
    std::string leaderName;        // {leader_name}
    std::string trackName;         // {track_name}
};

// One row per variable: the name as written in a template (without braces) and
// where its value lives. Pointer-to-member so the table IS the mapping - a new
// variable cannot be added to the struct and forgotten here without the lookup
// simply not finding it, which the unit test's census catches.
struct Binding {
    const char* name;
    std::string Vars::* value;
    // The two fields docs/spotter-reference.md is generated FROM. They live
    // here rather than in the doc because a doc drifts and a table cannot: the
    // generator has no other source, so a variable added without a description
    // shows up as an empty cell in a reviewed file rather than as nothing at
    // all. `group` is the heading it sorts under; rows sharing a group must be
    // adjacent, which the census test checks.
    const char* group;
    const char* what;
    // A SAMPLE of what this variable expands to, in the words the plugin
    // actually produces. Prose cannot show that {gap_to_best_lap} already says
    // "quicker" while {gap_to_ahead} does not - a template that adds the word
    // itself then reads "up three tenths quicker", which is exactly what the
    // shipped pack's sector rows said for several releases. It is a column of
    // the same table so it cannot drift from the variable, and the shipped
    // pack's rendered transcript is generated from these values, so a wrong
    // one is visible in that file's diff rather than only here.
    const char* example;
};

// The headings, in the order the reference prints them.
constexpr const char* kGroupEvent   = "What the event carries";
constexpr const char* kGroupYou     = "You";
constexpr const char* kGroupRefLaps = "Your reference laps";
constexpr const char* kGroupSectors = "Sector deltas";
constexpr const char* kGroupSides   = "The riders either side";
constexpr const char* kGroupSession = "The session";

inline const std::vector<Binding>& bindings() {
    static const std::vector<Binding> kBindings = {
        { "event_rider", &Vars::eventRider,
          kGroupEvent, "who the cue is about - \"rider four seventy six\", or \"you\"",
          "rider four seventy six" },
        { "event_time", &Vars::eventTime,
          kGroupEvent, "the lap or sector time the cue carries - at a split that is the ACCUMULATED time, the elapsed lap time the Timing HUD shows",
          "one forty eight point two" },
        { "penalty_seconds", &Vars::penaltySeconds,
          kGroupEvent, "how long THIS penalty is",
          "five seconds" },
        { "overtime_laps", &Vars::overtimeLaps,
          kGroupEvent, "the BONUS laps added when the clock expires, not a countdown",
          "two laps" },
        { "positions_changed", &Vars::positionsChanged,
          kGroupEvent, "places made up or lost on this lap, unsigned",
          "three" },
        { "event_gap_to_best_lap", &Vars::eventGapToBestLap,
          kGroupEvent, "another rider's lap against your session best (their laps only)",
          "one point one slower" },
        { "event_gap_to_last_lap", &Vars::eventGapToLastLap,
          kGroupEvent, "...and against your last lap",
          "zero point four quicker" },
        { "sector_number", &Vars::sectorNumber,
          kGroupEvent, "which sector just ended",
          "two" },
        { "sector_duration", &Vars::sectorDuration,
          kGroupEvent, "that sector on its own, not the elapsed lap time",
          "twenty nine point eight" },
        { "sector_delta_best_lap", &Vars::sectorDeltaBestLap,
          kGroupSectors, "elapsed time at this split vs your best lap's, with \"quicker\" or \"slower\"",
          "zero point three quicker" },
        { "sector_delta_ideal", &Vars::sectorDeltaIdeal,
          kGroupSectors, "...vs your best sectors summed",
          "zero point six slower" },
        { "sector_delta_last_lap", &Vars::sectorDeltaLastLap,
          kGroupSectors, "...vs last lap's",
          "zero point two quicker" },
        { "sector_delta_alltime", &Vars::sectorDeltaAlltime,
          kGroupSectors, "...vs your best lap ever here",
          "one point four slower" },
        { "sector_delta_record", &Vars::sectorDeltaRecord,
          kGroupSectors, "...vs the track record's",
          "two point nine slower" },
        { "pace_margin", &Vars::paceMargin,
          kGroupSectors, "how far up you are at the last split, on the on_pace cues",
          "zero point seven" },
        { "sector_best_delta", &Vars::sectorBestDelta,
          kGroupSectors, "how much you took off your best for that sector",
          "zero point four" },
        { "rider_name", &Vars::riderName,
          kGroupYou, "your name, as the game has it",
          "Alex" },
        { "position", &Vars::position,
          kGroupYou, "your position",
          "four" },
        { "lap_number", &Vars::lapNumber,
          kGroupYou, "the lap you are on",
          "six" },
        { "last_lap_time", &Vars::lastLapTime,
          kGroupYou, "your last lap",
          "one forty eight point two" },
        { "gap_to_leader", &Vars::gapToLeader,
          kGroupYou, "your gap to P1 - seconds on the same lap, laps once you are not",
          "twelve point four" },
        { "penalty_total", &Vars::penaltyTotal,
          kGroupYou, "your penalties added up. Includes the one being announced, so penalty_you can say the running total the standings column has not caught up with yet",
          "ten seconds" },
        { "fuel_laps", &Vars::fuelLaps,
          kGroupYou, "laps left in the tank",
          "four laps" },
        { "finish_time", &Vars::finishTime,
          kGroupYou, "your total race time; empty until you have finished",
          "twenty three minutes, nine seconds" },
        { "setup_name", &Vars::setupName,
          kGroupYou, "the setup you are on (\"Default\" when it is the stock one)",
          "Default" },
        { "positions_since_start", &Vars::positionsSinceStart,
          kGroupYou, "places gained since the grid - empty outside a race",
          "three" },
        { "positions_since_lap", &Vars::positionsSinceLap,
          kGroupYou, "...since your last start/finish crossing",
          "one" },
        { "positions_since_sector", &Vars::positionsSinceSector,
          kGroupYou, "...since your last split",
          "two" },
        { "best_lap_time", &Vars::bestLapTime,
          kGroupRefLaps, "your best this session",
          "one forty seven point nine" },
        { "alltime_best_time", &Vars::alltimeBestTime,
          kGroupRefLaps, "your best ever here; empty on a track you have never ridden",
          "one forty six point three" },
        { "ideal_lap_time", &Vars::idealLapTime,
          kGroupRefLaps, "your best sectors summed; empty until you have set every one",
          "one forty five point eight" },
        { "overall_best_time", &Vars::overallBestTime,
          kGroupRefLaps, "anyone's best this session",
          "one forty five point one" },
        { "record_time", &Vars::recordTime,
          kGroupRefLaps, "the track record (MX Bikes only)",
          "one forty four point seven" },
        { "gap_to_best_lap", &Vars::gapToBestLap,
          kGroupRefLaps, "your last lap vs your session best",
          "zero point three slower" },
        { "gap_to_alltime", &Vars::gapToAlltime,
          kGroupRefLaps, "...vs your best ever here",
          "one point nine slower" },
        { "gap_to_ideal", &Vars::gapToIdeal,
          kGroupRefLaps, "...vs your ideal lap",
          "two point four slower" },
        { "gap_to_overall", &Vars::gapToOverall,
          kGroupRefLaps, "...vs the session's best",
          "three point one slower" },
        { "gap_to_record", &Vars::gapToRecord,
          kGroupRefLaps, "...vs the track record",
          "three point five slower" },
        { "gap_to_last_lap", &Vars::gapToLastLap,
          kGroupRefLaps, "...vs the lap before it",
          "zero point eight quicker" },
        { "position_ahead", &Vars::positionAhead,
          kGroupSides, "the position of the rider ahead",
          "three" },
        { "rider_ahead", &Vars::riderAhead,
          kGroupSides, "who is ahead",
          "rider sixty five" },
        { "gap_to_ahead", &Vars::gapToAhead,
          kGroupSides, "how far ahead they are - the STOPWATCH reading from the last timing point you both crossed, or a lap count once they are a lap up; empty until there has been one for this rider",
          "one point two" },
        { "gained_on_ahead", &Vars::gainedOnAhead,
          kGroupSides, "how much of that gap changed since the last shared point",
          "zero point three" },
        { "trend_ahead", &Vars::trendAhead,
          kGroupSides, "\"gained\" or \"lost\" - PAST tense: the number beside it is a completed change between two timing points, not a rate. Put the verb first: \"gained zero point four on rider twelve\"",
          "gained" },
        { "last_lap_ahead", &Vars::lastLapAhead,
          kGroupSides, "the lap they just ran - the gap says where they are, this says whether you are catching them. Empty until they have completed one",
          "one forty eight point two" },
        { "position_behind", &Vars::positionBehind,
          kGroupSides, "the position of the rider behind",
          "five" },
        { "rider_behind", &Vars::riderBehind,
          kGroupSides, "who is behind",
          "rider four seventy six" },
        { "gap_to_behind", &Vars::gapToBehind,
          kGroupSides, "how far behind they are, measured the same way",
          "two point one" },
        { "gained_on_behind", &Vars::gainedOnBehind,
          kGroupSides, "how much of that gap changed since the last shared point",
          "zero point four" },
        { "trend_behind", &Vars::trendBehind,
          kGroupSides, "\"closed\" or \"dropped back\" - past tense, same as {trend_ahead}",
          "closed" },
        { "last_lap_behind", &Vars::lastLapBehind,
          kGroupSides, "the lap they just ran, measured the same way",
          "one forty seven point nine" },
        { "session_name", &Vars::sessionName,
          kGroupSession, "\"Race 1\", \"Warmup\", \"Qualify Practice\"...",
          "Race 1" },
        { "session_state", &Vars::sessionState,
          kGroupSession, "\"In Progress\", \"Cancelled\", \"Sighting Lap\"...",
          "In Progress" },
        { "session_length", &Vars::sessionLength,
          kGroupSession, "how long it is: minutes, laps, or both",
          "twenty minutes plus two laps" },
        { "session_remaining", &Vars::sessionRemaining,
          kGroupSession, "what is left of it, in the same shape; empty until the session is running",
          "six minutes plus two laps" },
        { "laps_remaining", &Vars::lapsRemaining,
          kGroupSession, "lap races only, once running; in a time+laps race, the leader's once overtime starts",
          "three" },
        { "time_remaining", &Vars::timeRemaining,
          kGroupSession, "timed sessions only, once running",
          "six minutes" },
        { "leader_name", &Vars::leaderName,
          kGroupSession, "who is leading (\"you\" when that is you -- write "
                         "the line so both read well)",
          "rider sixty five" },
        { "track_name", &Vars::trackName,
          kGroupSession, "the track",
          "Farm 14" },
    };
    return kBindings;
}


// The value for a {name}, or nullptr when no such variable exists. The
// nullptr case is what lets expand() leave a typo visible instead of eating
// it: "{gap_ahed}" stays on screen, which is how the author finds the typo.
inline const std::string* lookup(const Vars& vars, const std::string& name) {
    for (const Binding& b : bindings()) {
        if (name == b.name) return &(vars.*(b.value));
    }
    return nullptr;
}

}  // namespace SpotterVars
