// ============================================================================
// core/spotter_phrase.h
// Pure NUMBER-to-words helpers for the spotter, plus the cue categories.
// Header-only and Windows-free so the unit suite exercises it directly
// (test_spotter_phrase.cpp); SpotterManager is the only production caller.
//
// SENTENCES ARE NOT HERE. The wording lives in the cue pack
// (mxbmrp3_data/spotters/default/spotter.ini), and composing phrases in the
// plugin would be a second copy of it — see SpotterManager::reloadCuePack.
// What is here is the part a pack CANNOT express: how to say a number out loud.
//
// Design notes, in decreasing order of likelihood someone "fixes" them:
//  - Rider numbers are read RACING style ("four seventy six", "two oh six"),
//    matching the baked chunk packs the wav backend uses — the same wording
//    must come out of both backends or a pack swap changes what the spotter
//    says.
//  - Lap times speak tenths only ("one forty eight point two"): a spotter
//    saying milliseconds is noise at race pace. The full time stays on the
//    HUD/subtitle.
//  - Events about the focused rider (the player, or the spectate target when
//    spectating) phrase as "you"; everyone else is "rider N". Focus is an
//    input, not a lookup, to keep this pure.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "event_log_types.h"

namespace SpotterPhrase {

// Cue categories for the user's filter toggles.
//
// THE RULE, because a toggle has to be predictable to be usable: a cue is
// filed by WHO IT IS ABOUT first, and only then by what kind of information
// it carries.
//
//   General    the session, race control, and YOUR OWN status changes —
//              green flag, your penalty, your pit lane, your checkered flag
//   Timing     numbers about YOUR race — lap times, gaps, position readouts,
//              the session clock and its milestones
//   Opponents  anything whose subject is ANOTHER RIDER — their fastest lap,
//              their penalty, leader changes, proximity, traffic
//   Hazard     safety — blue flag, rider down ahead, wrong-way rider
//
// This is not how it started, and the difference was a real bug: the map
// keyed off the event TYPE alone, so RiderFinished/RiderDSQ/PitEntry/PitExit
// were Opponents no matter who they were about — muting "Opponents" to stop
// rival chatter also swallowed your own checkered flag, your own DSQ and
// your own pit calls, while "Penalty for rider 56" kept talking because
// penalties were filed General. Subject is an input for exactly that reason.
enum class Category : uint8_t {
    General = 0,   // session lifecycle, race control, your own status
    Timing,        // your lap times, gaps, position, session clock
    Opponents,     // the rest of the field's race NEWS: their laps, penalties,
                   // retirements, pit stops, finishes
    Proximity,     // who is around you RIGHT NOW: behind, alongside, traffic
    Hazard,        // blue flag, incident ahead, wrong way
    COUNT
};

// Who a cue is about. The caller knows: raceNum < 0 is session-wide, else
// it is you or somebody else.
enum class Subject : uint8_t { Session, You, Other };

inline Subject subjectOf(int raceNum, int focusedRaceNum) {
    if (raceNum < 0) return Subject::Session;
    return raceNum == focusedRaceNum ? Subject::You : Subject::Other;
}

inline Category categoryFor(EventLogType type, Subject subj) {
    // Another rider is the subject: Opponents, whatever the event carries.
    // A rival's fastest lap is a lap time, but it is first of all news about
    // a rival — and someone who muted Opponents asked not to hear it.
    // (Director cuts are broadcaster tooling, never spoken by anyone, so
    // they stay out of a user-facing category.)
    if (subj == Subject::Other && type != EventLogType::Director) {
        return Category::Opponents;
    }

    switch (type) {
        // Your own pace and the session's clock.
        case EventLogType::FastestLap:
        case EventLogType::FinalLap:
        case EventLogType::OvertimeStarted:
        case EventLogType::SessionTimeExpired:
            return Category::Timing;
        // Session lifecycle, race control, and your own status changes
        // (finish, DSQ, pit lane, penalties, taking the lead) all land in
        // General via the default.
        default:
            return Category::General;
    }
}

// Racing-style number words: 56 -> "fifty six", 234 -> "two thirty four",
// 206 -> "two oh six", 400 -> "four hundred". Valid for 0..999 (rider
// numbers); out-of-range falls back to digits via std::to_string so a bad
// input is audible rather than silent.
inline std::string numberWords(int n) {
    static const char* kOnes[] = {
        "zero", "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
        "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* kTens[] = {
        "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
        "eighty", "ninety"};

    if (n < 0 || n > 999) return std::to_string(n);

    auto twoDigit = [&](int v) -> std::string {
        if (v < 20) return kOnes[v];
        std::string out = kTens[v / 10];
        if (v % 10) { out += " "; out += kOnes[v % 10]; }
        return out;
    };

    if (n < 100) return twoDigit(n);
    const int hundreds = n / 100;
    const int rest = n % 100;
    if (rest == 0) return std::string(kOnes[hundreds]) + " hundred";
    if (rest < 10) return std::string(kOnes[hundreds]) + " oh " + kOnes[rest];
    return std::string(kOnes[hundreds]) + " " + twoDigit(rest);
}

// The racing-style reading of a lap time's COMPOSED value (minutes*100 +
// seconds), which is numberWords() everywhere except one case it cannot know
// about: a time whose seconds are exactly 00 composes to a multiple of a
// hundred, and 100 as a NUMBER is "one hundred" — right for rider #100, wrong
// for 1:00.4, which a spotter reads "one oh oh point four" exactly as they
// read 1:08.2 "one oh eight point two". numberWords() stays honest about
// numbers; a lap time asks through here.
//
// The mixed-audio backend has the same split for the same reason — see
// SpotterMix::resolveTokens's {event_time} branch, which emits num_1 + oh + oh
// rather than the num_100 clip a full pack recorded as "one hundred".
inline std::string lapTimeNumberWords(int composed) {
    if (composed >= 100 && composed % 100 == 0) {
        return numberWords(composed / 100) + " oh oh";
    }
    return numberWords(composed);
}

// A lap time reduced to the two numbers the spotter speaks: `composed` is
// minutes*100+seconds (the value lapTimeNumberWords reads racing-style — 148
// for 1:48, plain seconds when under a minute) and `tenths` the first decimal
// digit. Also the wav chunk mixer's numeric input, so one decomposition feeds
// both backends and they cannot disagree about a time.
struct LapTimeParts {
    int composed = -1;
    int tenths = -1;
    int totalMs = -1;   // what it can be COMPARED with, undegraded by tenths
    bool valid = false;
};

// The one place a lap time is broken into what a voice says. It takes
// MILLISECONDS, because that is what the game gives every producer of one —
// never the event log's formatted text parsed back, which would make a
// display format load-bearing for audio and put a second copy of this reading
// beside lapTimeWordsMs.
inline LapTimeParts lapTimePartsMs(int ms) {
    if (ms <= 0) return {};
    const int totalSec = ms / 1000;
    const int minutes = totalSec / 60;
    const int seconds = totalSec % 60;
    LapTimeParts parts;
    parts.composed = minutes > 0 ? minutes * 100 + seconds : seconds;
    if (parts.composed > 999) return {};   // past what a pack can say
    parts.tenths = (ms % 1000) / 100;
    parts.totalMs = ms;
    parts.valid = true;
    return parts;
}


// Milliseconds -> the words for a LAP TIME: "one forty eight point two" for
// 1:48.2. The words the mixer's chunks say, from the same decomposition the
// mixer is handed, so the two backends cannot read one time two ways.
inline std::string lapTimeWordsMs(int ms) {
    const LapTimeParts parts = lapTimePartsMs(ms);
    if (!parts.valid) return std::string();
    return lapTimeNumberWords(parts.composed) + " point " +
           numberWords(parts.tenths);
}

// Milliseconds -> the words for a DURATION that may run to hours of racing:
// "twenty two minutes, eighteen seconds". Not lapTimeWordsMs, which folds
// minutes and seconds into one racing-style number ("one forty eight") and
// gives up past 9:59 — a race total is routinely longer than that, and would
// come out EMPTY rather than wrong, which is harder to notice.
inline std::string durationWords(int ms) {
    // Under a second is not a duration anybody means. Replaying the committed
    // one-lap race, the classification reported a finish time of 30 ms and this
    // read it back as "Checkered flag, P one, ZERO SECONDS" — a confidently
    // wrong number where saying nothing lets the optional group drop instead.
    if (ms < 1000) return std::string();
    const int totalSec = ms / 1000;
    const int minutes = totalSec / 60;
    const int seconds = totalSec % 60;
    std::string out;
    if (minutes > 0) {
        out = numberWords(minutes) + (minutes == 1 ? " minute" : " minutes");
        if (seconds > 0) out += ", ";
    }
    if (seconds > 0 || minutes == 0) {
        out += numberWords(seconds) + (seconds == 1 ? " second" : " seconds");
    }
    return out;
}

// Milliseconds -> the words for a GAP: "one point two". NOT folded into
// minutes like a lap time — a 90-second gap is "ninety point zero", because
// "one thirty" would be heard as a lap time.
inline std::string gapWordsMs(int ms) {
    if (ms < 0) ms = -ms;
    const int sec = ms / 1000;
    if (sec > 999) return std::string();
    return numberWords(sec) + " point " + numberWords((ms % 1000) / 100);
}

// A penalty's amount in WHOLE seconds, -1 when none was reported. Rounded the
// way the event log's own detail column rounds it, so the voice and the row
// never disagree by a second on the same penalty.
inline int penaltyWholeSeconds(const EventNumbers& nums) {
    return nums.penaltyMs > 0 ? (nums.penaltyMs + 500) / 1000 : -1;
}

// "five seconds" / "one second"; "" when secs < 0 (no amount known).
inline std::string secondsWords(int secs) {
    if (secs < 0) return std::string();
    return numberWords(secs) + (secs == 1 ? " second" : " seconds");
}

// What a spotter actually says at that moment: how many laps are LEFT, which is
// one more than the bonus count above. The clock expires with the leader
// mid-lap, so they must finish the lap in progress and THEN run the bonus laps
// — race_classification_handler sets finishLap = leaderLaps + bonus, and a
// rider finishes on numLaps > finishLap, so from the trigger there are exactly
// bonus + 1 laps to run. Saying the bonus count instead announced "Overtime,
// one lap to go" on Farm 14 while the leader still had two, and contradicted
// the session clock, which holds its countdown until the leader crosses into
// the bonus laps (PluginData::getLeaderLapsToGo).
//
// The event log's DETAIL column keeps the bonus count — that is the event's own
// truth and what the race feed shows. This is the spoken reading of it.
// "two laps" / "one lap"; "" when laps < 0.
//
// NOT "two laps to go", and not the bonus count plus one either — both are
// wrong for the same reason. A countdown from the moment overtime starts
// depends on where the leader IS: the clock expires mid-lap, so with them 100m
// from the line "two laps to go" undercounts and "three" overcounts, and no
// single number is true for a race that is 1 minute plus 2 laps.
//
// What IS exactly true, wherever the leader happens to be, is the STRUCTURE:
// this lap, then the bonus laps. So the number stays the well-defined one —
// the bonus count the event carries — and the sentence says what it counts
// ("two laps after this one"). The session clock is left to do the counting
// down, which it starts once the leader crosses into the bonus laps
// (PluginData::getLeaderLapsToGo).
// The two trend vocabularies, so the words live in one place rather than being
// spelled out at each of the six sites that needed them (two measurement
// points, the behind cue, and the ambient fill for each side). AHEAD is about
// closing a gap you are chasing; BEHIND is about one you are defending.
// PAST TENSE, because the number beside them is a COMPLETED change, not a rate:
// the delta is how much the gap moved between two timing points you have both
// already crossed. "gaining zero point four" reads as a speed; "gained zero
// point four" is what happened, which is what a spotter says. The templates put
// the verb first for the same reason -- "gained zero point four on rider twelve".
inline const char* trendAheadWord(int deltaMs) {
    return deltaMs < 0 ? "gained" : "lost";
}
inline const char* trendBehindWord(int deltaMs) {
    return deltaMs < 0 ? "closed" : "dropped back";
}

inline std::string lapsWords(int laps) {
    if (laps < 0) return std::string();
    return numberWords(laps) + (laps == 1 ? " lap" : " laps");
}

// The rider reference used in phrases: "you" for the focused rider (player,
// or spectate target while spectating), else "rider fifty six". raceNum < 0
// (session-level event) yields "".
inline std::string riderRef(int raceNum, int focusedRaceNum) {
    if (raceNum < 0) return std::string();
    if (raceNum == focusedRaceNum) return "you";
    return "rider " + numberWords(raceNum);
}


}  // namespace SpotterPhrase
