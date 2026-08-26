// ============================================================================
// tests/unit/test_spotter_phrase.cpp
// Pins the spotter's number wording (core/spotter_phrase.h) — the part of what
// the audio backends speak that a pack cannot express. Two contracts matter
// beyond "reads nicely":
//
//  1. The RACING number style ("four seventy six", "two oh six") is load-
//     bearing: the planned wav backend bakes one clip per number with this
//     exact wording, so a drive-by change to numberWords silently desyncs
//     the TTS backend from every baked pack.
//  2. An amount that is not known reads as "" and not as a spoken zero, so a
//     template's [optional group] can drop rather than saying "no seconds".
// ============================================================================
#include "doctest.h"

#include <fstream>
#include <string>

#include "core/spotter_phrase.h"

using namespace SpotterPhrase;

TEST_CASE("numberWords: racing style across the rider-number range") {
    CHECK(numberWords(0) == "zero");
    CHECK(numberWords(7) == "seven");
    CHECK(numberWords(13) == "thirteen");
    CHECK(numberWords(56) == "fifty six");
    CHECK(numberWords(90) == "ninety");
    CHECK(numberWords(100) == "one hundred");
    CHECK(numberWords(206) == "two oh six");     // the zero-tens case
    CHECK(numberWords(234) == "two thirty four");
    CHECK(numberWords(400) == "four hundred");
    CHECK(numberWords(476) == "four seventy six");
    CHECK(numberWords(999) == "nine ninety nine");
    // Out of rider-number range: audible digits, not silence.
    CHECK(numberWords(1000) == "1000");
    CHECK(numberWords(-3) == "-3");
}

// The full 0..999 wording against the fixture BOTH sides assert — the pack
// generator's num_words must produce these exact strings or every baked
// num_<N>.wav says something different from TTS and the subtitles. The
// generator's side of this handshake is `generate.py --selftest` (the
// spottergen-selftest gate).
TEST_CASE("numberWords: matches the shared generator fixture verbatim") {
    std::ifstream fix(SPOTTER_NUMBERS_FIXTURE);
    REQUIRE_MESSAGE(fix.is_open(), "missing " SPOTTER_NUMBERS_FIXTURE);
    std::string line;
    int checked = 0;
    while (std::getline(fix, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        REQUIRE(tab != std::string::npos);
        const int n = std::stoi(line.substr(0, tab));
        CHECK_MESSAGE(numberWords(n) == line.substr(tab + 1),
                      "wording drift at " << n);
        ++checked;
    }
    CHECK(checked == 1000);
}

TEST_CASE("lapTimeWordsMs: tenths only, minutes composed racing-style") {
    CHECK(lapTimeWordsMs(108231) == "one forty eight point two");
    CHECK(lapTimeWordsMs(68905) == "one oh eight point nine");
    CHECK(lapTimeWordsMs(48231) == "forty eight point two");
    CHECK(lapTimeWordsMs(125007) == "two oh five point zero");
    // Seconds of exactly 00 compose to a multiple of a hundred, and a bare
    // 100 is "one hundred" — right for rider #100, wrong for a lap time,
    // which reads the same way its neighbours do. Both spellings of the same
    // minute, so the odd one out is visible:
    CHECK(lapTimeWordsMs(60412) == "one oh oh point four");
    CHECK(lapTimeWordsMs(68905) == "one oh eight point nine");
    CHECK(lapTimeWordsMs(120300) == "two oh oh point three");
    // ...while the NUMBER is left alone, because rider 100 is rider one
    // hundred and the wav packs' whole-number clip says so too.
    CHECK(numberWords(100) == "one hundred");
    // No time, nothing said.
    CHECK(lapTimeWordsMs(0) == "");
    CHECK(lapTimeWordsMs(-1) == "");
    // Past what a pack can say: composed would exceed three digits.
    CHECK(lapTimeWordsMs(10 * 60000) == "");
}

// The decomposition the mixer is handed and the words the TTS speaks come from
// ONE function now, so the two backends cannot read a time two ways. They used
// to be separate readings — one parsing the event log's "1:48.231", one from
// milliseconds — which is why the exact-minute bug above had to be fixed twice.
TEST_CASE("lapTimePartsMs: one decomposition for both backends") {
    const LapTimeParts a = lapTimePartsMs(108231);
    REQUIRE(a.valid);
    CHECK(a.composed == 148);
    CHECK(a.tenths == 2);
    // The whole time, undegraded: truncating to tenths first and subtracting
    // loses up to 99ms, which is a whole spoken tenth.
    CHECK(a.totalMs == 108231);

    // A leading zero in the fraction is 0 tenths, not 7.
    const LapTimeParts b = lapTimePartsMs(58007);
    REQUIRE(b.valid);
    CHECK(b.composed == 58);
    CHECK(b.tenths == 0);
    CHECK(b.totalMs == 58007);

    // Whatever the mixer plays, the voice says — checked against each other
    // rather than against a literal, since agreeing is the whole point.
    for (const int ms : {60412, 68905, 120300, 108231, 45500}) {
        const LapTimeParts p = lapTimePartsMs(ms);
        REQUIRE(p.valid);
        CHECK(lapTimeWordsMs(ms) ==
              lapTimeNumberWords(p.composed) + " point " +
                  numberWords(p.tenths));
    }

    CHECK_FALSE(lapTimePartsMs(0).valid);
    CHECK_FALSE(lapTimePartsMs(-5).valid);
    CHECK(lapTimePartsMs(0).totalMs == -1);
}

TEST_CASE("penalty and overtime amounts read as words") {
    // The amount arrives as MILLISECONDS, the way the game reports it, and is
    // rounded to whole seconds exactly as the event log's own detail column
    // rounds it — the voice and the row must not differ by a second on the
    // same penalty. It used to arrive as that column's text ("5s") and be
    // parsed back, which made the column's format load-bearing for audio.
    CHECK(penaltyWholeSeconds({-1, 5000}) == 5);
    CHECK(penaltyWholeSeconds({-1, 5400}) == 5);    // rounds down
    CHECK(penaltyWholeSeconds({-1, 5500}) == 6);    // ...and up, at the half
    CHECK(penaltyWholeSeconds({-1, 30000}) == 30);
    CHECK(penaltyWholeSeconds({-1, 1000}) == 1);
    CHECK(penaltyWholeSeconds({}) == -1);           // no amount reported
    CHECK(penaltyWholeSeconds({-1, 0}) == -1);
    CHECK(penaltyWholeSeconds({-1, -5000}) == -1);

    CHECK(secondsWords(5) == "five seconds");
    CHECK(secondsWords(1) == "one second");
    CHECK(secondsWords(30) == "thirty seconds");
    CHECK(secondsWords(-1) == "");

    CHECK(lapsWords(2) == "two laps");
    CHECK(lapsWords(1) == "one lap");
    CHECK(lapsWords(-1) == "");

    // The wording these feed is the shipped pack's business now
    // (overtime_started names {overtime_laps}); what stays pinned here is the
    // AMOUNT, and that an unknown one says nothing at all rather than "zero".
    CHECK(lapsWords(0) == "zero laps");
    CHECK(secondsWords(0) == "zero seconds");
}

TEST_CASE("subjectOf: session / you / another rider") {
    CHECK(subjectOf(-1, 12) == Subject::Session);
    CHECK(subjectOf(12, 12) == Subject::You);
    CHECK(subjectOf(56, 12) == Subject::Other);
    // No focused rider (spectating with no camera target): everyone is
    // somebody else, and -1 == -1 must not read as "you".
    CHECK(subjectOf(-1, -1) == Subject::Session);
    CHECK(subjectOf(56, -1) == Subject::Other);
}

TEST_CASE("categoryFor: a cue is filed by WHO it is about, then what it is") {
    // Your own race: pace and clock are Timing...
    CHECK(categoryFor(EventLogType::FastestLap, Subject::You) ==
          Category::Timing);
    CHECK(categoryFor(EventLogType::FinalLap, Subject::Session) ==
          Category::Timing);
    CHECK(categoryFor(EventLogType::OvertimeStarted, Subject::Session) ==
          Category::Timing);

    // ...and your own STATUS changes are General, not Opponents. This is the
    // bug the subject argument exists for: muting rival chatter used to
    // swallow your own checkered flag, DSQ and pit calls, because those
    // event TYPES describe other riders most of the time.
    CHECK(categoryFor(EventLogType::RiderFinished, Subject::You) ==
          Category::General);
    CHECK(categoryFor(EventLogType::RiderDSQ, Subject::You) ==
          Category::General);
    CHECK(categoryFor(EventLogType::PitEntry, Subject::You) ==
          Category::General);
    CHECK(categoryFor(EventLogType::PitExit, Subject::You) ==
          Category::General);
    CHECK(categoryFor(EventLogType::LeaderChange, Subject::You) ==
          Category::General);
    CHECK(categoryFor(EventLogType::Penalty, Subject::You) ==
          Category::General);

    // Another rider is the subject: Opponents, whatever the event carries —
    // including the two that used to leak past a muted Opponents toggle
    // (their fastest lap was filed Timing, their penalty General).
    CHECK(categoryFor(EventLogType::FastestLap, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::Penalty, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::PenaltyClear, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::RiderFinished, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::RiderRetired, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::LeaderChange, Subject::Other) ==
          Category::Opponents);
    CHECK(categoryFor(EventLogType::PitEntry, Subject::Other) ==
          Category::Opponents);

    // Session-wide lifecycle stays General.
    CHECK(categoryFor(EventLogType::SessionStarted, Subject::Session) ==
          Category::General);
    CHECK(categoryFor(EventLogType::SessionComplete, Subject::Session) ==
          Category::General);
    // Director cuts are never spoken; they must not be reachable through the
    // Opponents toggle either (compose() returns "" and cueKeyFor is null).
    CHECK(categoryFor(EventLogType::Director, Subject::Other) ==
          Category::General);
}

// A race total is not a lap time. lapTimeWordsMs folds minutes and seconds
// into one racing-style number and gives up past 9:59, returning EMPTY — so
// using it for a 22-minute race would drop the figure silently rather than
// say something obviously wrong, which is the harder failure to notice.
TEST_CASE("durationWords: race-length times, where a lap time would give up") {
    CHECK(durationWords(22 * 60000 + 18000) == "twenty two minutes, eighteen seconds");
    CHECK(durationWords(60000) == "one minute");
    CHECK(durationWords(61000) == "one minute, one second");
    CHECK(durationWords(45000) == "forty five seconds");
    CHECK(durationWords(1000) == "one second");
    // The case that motivates it: past 9:59 the lap-time reading is empty.
    CHECK(lapTimeWordsMs(22 * 60000).empty());
    CHECK_FALSE(durationWords(22 * 60000).empty());
    // Not finished / no time: nothing to say, distinct from "zero seconds".
    CHECK(durationWords(0).empty());
    CHECK(durationWords(-1).empty());
    // Sub-second is empty too, not "zero seconds". The committed one-lap race
    // reports a 30 ms finish time, and this used to read it back as "Checkered
    // flag, P one, zero seconds" — a wrong number where the template's optional
    // group should simply drop.
    CHECK(durationWords(30).empty());
    CHECK(durationWords(999).empty());
}

// totalMs is what makes a rival's lap comparable with yours. The composed /
// tenths pair is what a pack SAYS; subtracting those instead loses up to 99ms,
// which is a whole spoken tenth — enough to call a gap "zero point zero" when
// it is really a tenth, in the direction that flatters nobody.
