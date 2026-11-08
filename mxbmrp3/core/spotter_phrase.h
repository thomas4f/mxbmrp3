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
#include <unordered_map>

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
//   Tricks     YOUR freestyle tricks — the name, the landing, the crash, the
//              combo (games with FMX detection; never in a race session)
//   Achievements  an achievement you just earned, each with its own line
//              (MX Bikes)
//
// The last two are for fun rather than for racing. Tricks is the one category
// that ships OFF (the default mask is in spotter_manager.h).
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
    Tricks,        // your FMX tricks: name, landing, crash, combo
    Achievements,  // an achievement you just earned
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

// THE SPOKEN WORDS a language replaces. The sentences translate in a language's
// own spotter.ini (SpotterManager::reloadCuePack layers it over the shipped
// pack); the words the plugin ASSEMBLES -- the numbers, "rider", "quicker",
// a trick's name -- cannot be written there, so that file's [Words] section
// maps each English word below to its translation, and say() is the one place
// they are looked up. An empty table is English.
//
// kWordKeys is the whole vocabulary, and it is a census, not documentation:
// test_spotter_language.cpp fails on a word say() is asked for that it does not
// list, on a key nothing says, and on a shipped language that leaves a key
// untranslated.
// Single-word keys on purpose, except the singular nouns ("one lap"): a
// language whose "one" agrees with the noun (Swedish "ett varv", "en minut")
// needs the pair, and English needs nothing.
//
// Game thread only, like every caller (the cue is composed there; the worker
// receives finished text).
inline std::unordered_map<std::string, std::string>& spokenWords() {
    static std::unordered_map<std::string, std::string> words;
    return words;
}
inline const char* say(const char* english) {
    const std::unordered_map<std::string, std::string>& words = spokenWords();
    if (words.empty() || !english) return english;
    const auto it = words.find(english);
    return it == words.end() ? english : it->second.c_str();
}

inline constexpr const char* kNumberOnes[] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
    "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
inline constexpr const char* kNumberTens[] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
    "eighty", "ninety"};

// Every word say() is ever asked for (the numbers above are words too). A
// language may also give 21..99 whole, keyed by their digits (numberWords
// says why); those are optional and not listed here.
inline constexpr const char* kWordKeys[] = {
    "zero", "one", "two", "three", "four", "five", "six", "seven",
    "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
    "fifteen", "sixteen", "seventeen", "eighteen", "nineteen",
    "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty",
    "ninety", "hundred", "oh", "point",
    "one minute", "minutes", "one second", "seconds", "one lap", "laps",
    "plus", "rider", "you", "quicker", "slower",
    "gained", "lost", "closed", "dropped back",
    // core/spotter_tricks.h
    "double", "triple", "quad",
    "big", "flat", "deep", "huge", "endless",
    "wheelie", "coaster wheelie", "endo", "stoppie", "burnout", "donut",
    "pivot", "backflip", "frontflip", "barrel roll", "scrub", "whip", "spin",
    "oppo", "turn down", "flat three sixty",
};

// Racing-style number words: 56 -> "fifty six", 234 -> "two thirty four",
// 206 -> "two oh six", 400 -> "four hundred". Valid for 0..999 (rider
// numbers); out-of-range falls back to digits via std::to_string so a bad
// input is audible rather than silent.
inline std::string numberWords(int n) {
    if (n < 0 || n > 999) return std::to_string(n);
    auto ones = [](int v) { return say(kNumberOnes[v]); };

    auto twoDigit = [&](int v) -> std::string {
        if (v < 20) return ones(v);
        // A language whose 21..99 are not "tens ones" - French 76 is
        // soixante-seize, German sechsundsiebzig - lists them whole in
        // [Words] by their digits ("76 = soixante-seize"). Optional, so the
        // others never do.
        if (v % 10) {
            const std::unordered_map<std::string, std::string>& words = spokenWords();
            if (!words.empty()) {
                const auto whole = words.find(std::to_string(v));
                if (whole != words.end()) return whole->second;
            }
        }
        std::string out = say(kNumberTens[v / 10]);
        if (v % 10) { out += " "; out += ones(v % 10); }
        return out;
    };

    if (n < 100) return twoDigit(n);
    const int hundreds = n / 100;
    const int rest = n % 100;
    const std::string lead = ones(hundreds);
    if (rest == 0) return lead + " " + say("hundred");
    if (rest < 10) return lead + " " + say("oh") + " " + ones(rest);
    return lead + " " + twoDigit(rest);
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
        return numberWords(composed / 100) + " " + say("oh") + " " + say("oh");
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
    return lapTimeNumberWords(parts.composed) + " " + say("point") + " " +
           numberWords(parts.tenths);
}

// A count with its noun: "five seconds" / "one second"; "" when n < 0 (no
// amount known). The singular is one word key, not "one" + "second", so a
// language whose "one" agrees with the noun can say so.
inline std::string countWords(int n, const char* one, const char* many) {
    if (n < 0) return std::string();
    if (n == 1) return say(one);
    return numberWords(n) + " " + say(many);
}
inline std::string secondsWords(int secs) {
    return countWords(secs, "one second", "seconds");
}
inline std::string minutesWords(int minutes) {
    return countWords(minutes, "one minute", "minutes");
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
        out = minutesWords(minutes);
        if (seconds > 0) out += ", ";
    }
    if (seconds > 0 || minutes == 0) {
        out += secondsWords(seconds);
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
    return numberWords(sec) + " " + say("point") + " " +
           numberWords((ms % 1000) / 100);
}

// A gap with the way it went: "zero point four quicker". The word rides WITH
// the number because spoken aloud a bare "point three" does not say which way
// it went. Zero is the caller's business (a dead heat says nothing).
inline std::string paceWords(int deltaMs) {
    return gapWordsMs(deltaMs) + " " + say(deltaMs < 0 ? "quicker" : "slower");
}

// A penalty's amount in WHOLE seconds, -1 when none was reported. Rounded the
// way the event log's own detail column rounds it, so the voice and the row
// never disagree by a second on the same penalty.
inline int penaltyWholeSeconds(const EventNumbers& nums) {
    return nums.penaltyMs > 0 ? (nums.penaltyMs + 500) / 1000 : -1;
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
    return deltaMs < 0 ? say("gained") : say("lost");
}
inline const char* trendBehindWord(int deltaMs) {
    return deltaMs < 0 ? say("closed") : say("dropped back");
}

inline std::string lapsWords(int laps) {
    return countWords(laps, "one lap", "laps");
}

// The rider reference used in phrases: "you" for the focused rider (player,
// or spectate target while spectating), else "rider fifty six". raceNum < 0
// (session-level event) yields "".
inline std::string riderRef(int raceNum, int focusedRaceNum) {
    if (raceNum < 0) return std::string();
    if (raceNum == focusedRaceNum) return say("you");
    return std::string(say("rider")) + " " + numberWords(raceNum);
}


}  // namespace SpotterPhrase
