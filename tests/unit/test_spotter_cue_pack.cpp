// ============================================================================
// tests/unit/test_spotter_cue_pack.cpp
// Pins the spotter cue-pack format (core/spotter_cue_pack.h) — the contract
// shared with every pack a user writes or downloads:
//
//  - cueKeyFor's names are the pack API: a rename orphans every pack's
//    override of that cue, so the full mapping is spelled out here the way
//    the hotkey config names are pinned by their comment.
//  - parse() is fed hand-edited files: garbage lines skip, empty value =
//    explicit mute (distinct from absent = fall back to default), and the
//    "_wav" path-escape rejection is a trust boundary (packs are SHARED
//    files; "..\\..\\x.wav" naming something outside the pack folder must
//    never reach PlaySound).
//  - expand()'s punctuation tidy-up is what lets ONE template serve events
//    with and without a lap time; without it every pack author needs two.
// ============================================================================
#include "doctest.h"

#include "core/spotter_cue_pack.h"

using namespace SpotterCuePack;

TEST_CASE("cueKeyFor: every event maps to a stable key or is never spoken") {
    CHECK(std::string(cueKeyFor(EventLogType::FastestLap, true)) == "fastest_lap_you");
    CHECK(std::string(cueKeyFor(EventLogType::FastestLap, false)) == "fastest_lap_other");
    CHECK(std::string(cueKeyFor(EventLogType::SessionStarted, false)) == "session_started");
    CHECK(std::string(cueKeyFor(EventLogType::SessionStarted, true)) == "session_started");
    CHECK(std::string(cueKeyFor(EventLogType::LeaderChange, true)) == "leader_you");
    // TWO moments. The white flag is the LEADER starting the last lap and
    // fires with no raceNum, so it resolves session-level; YOUR last lap is a
    // separate emit carrying your number, and in a spread-out field lands a
    // lap or more later. This split was reverted once, correctly at the time:
    // the only emitter passed no raceNum, so "final_lap_you" could never fire
    // and a pack's mix had no {event_rider} to resolve — packs played silence.
    // The missing half was the EMITTER, which race_lap_handler now has.
    CHECK(std::string(cueKeyFor(EventLogType::FinalLap, false)) == "final_lap");
    CHECK(std::string(cueKeyFor(EventLogType::FinalLap, true)) == "final_lap_you");
    CHECK(std::string(cueKeyFor(EventLogType::PitEntry, false)) == "pit_entry_other");
    CHECK(std::string(cueKeyFor(EventLogType::RiderDNS, false)) == "did_not_start_other");
    // Director is broadcaster tooling: never spoken, not overridable.
    CHECK(cueKeyFor(EventLogType::Director, false) == nullptr);
    CHECK(cueKeyFor(EventLogType::Director, true) == nullptr);
}

TEST_CASE("parse: cues section, comments, garbage, empty-value mute") {
    const Pack p = parse(
        "; a pack\r\n"
        "[Meta]\n"
        "name = Test Pack\n"          // outside [Cues]: ignored
        "[Cues]\n"
        "fastest_lap_other = Fastest lap, {event_rider}, {event_time}.\n"
        "  final_lap_you =   Last lap, send it.  \r\n"
        "penalty_other =\n"            // explicit mute
        "# comment\n"
        "no equals sign here\n"
        "session_started_wav = green.wav\n"
        "[Other]\n"
        "leader_you = should not load\n");

    CHECK(p.phrases.at("fastest_lap_other") == "Fastest lap, {event_rider}, {event_time}.");
    CHECK(p.phrases.at("final_lap_you") == "Last lap, send it.");
    CHECK(p.phrases.at("penalty_other") == "");         // muted, not absent
    CHECK(p.phrases.count("name") == 0);
    CHECK(p.phrases.count("leader_you") == 0);
    CHECK(p.wavs.at("session_started") == "green.wav"); // _wav suffix stripped
    CHECK(p.phrases.count("session_started_wav") == 0);
}

TEST_CASE("parse: wav path escapes are rejected, never stored") {
    const Pack p = parse(
        "[Cues]\n"
        "a_wav = ..\\..\\windows\\bad.wav\n"
        "b_wav = sub/dir.wav\n"
        "c_wav = back\\slash.wav\n"
        "d_wav = fine.wav\n");
    CHECK(p.wavs.count("a") == 0);
    CHECK(p.wavs.count("b") == 0);
    CHECK(p.wavs.count("c") == 0);
    CHECK(p.wavs.at("d") == "fine.wav");
}

TEST_CASE("parse: mix sequences tokenize; one unsafe token rejects whole") {
    const Pack p = parse(
        "[Cues]\n"
        "fastest_lap_other_mix = fl_head.wav   {event_rider}  {event_time}\n"
        "leader_other_mix = head.wav {event_rider} ..\\evil.wav\n"   // rejected whole
        "clear_mix =\n");                                       // empty: no entry
    const auto& mix = p.mixes.at("fastest_lap_other");
    REQUIRE(mix.size() == 3);
    CHECK(mix[0] == "fl_head.wav");
    CHECK(mix[1] == "{event_rider}");
    CHECK(mix[2] == "{event_time}");
    CHECK(p.mixes.count("leader_other") == 0);   // no partial sequence
    CHECK(p.mixes.count("clear") == 0);
    CHECK(p.phrases.count("fastest_lap_other_mix") == 0);  // suffix stripped
}

TEST_CASE("variantKeys: base always present, contiguous scan, any map counts") {
    const Pack p = parse(
        "[Cues]\n"
        "rider_behind_2 = On your tail.\n"
        "rider_behind_3_wav = alt3.wav\n"     // wav-only variant still counts
        "rider_behind_5 = never reached\n"    // gap at _4 ends the scan
        "clear_2_mix = a.wav {event_rider}\n");     // mix-only variant counts too

    const auto rb = variantKeys(p, "rider_behind");
    REQUIRE(rb.size() == 3);
    CHECK(rb[0] == "rider_behind");
    CHECK(rb[1] == "rider_behind_2");
    CHECK(rb[2] == "rider_behind_3");

    const auto cl = variantKeys(p, "clear");
    REQUIRE(cl.size() == 2);
    CHECK(cl[1] == "clear_2");

    // No pack entries at all: just the base (built-in phrase serves it).
    CHECK(variantKeys(p, "blue_flag").size() == 1);
}

// [Mix] gap_ms — how tightly the pack's own clips want to be stitched. The
// ABSENT case is the one with teeth: absent must not read as 0, or a pack that
// never mentioned the key would be pinned to butt-joins and stop tracking the
// plugin's default.
TEST_CASE("parse: [Mix] gap_ms is optional, signed, and separate from absent") {
    const Pack none = parse("[Cues]\nx = y\n");
    CHECK_FALSE(none.hasGapMs);

    const Pack zero = parse("[Mix]\ngap_ms = 0\n");
    CHECK(zero.hasGapMs);
    CHECK(zero.gapMs == 0);

    const Pack neg = parse("[Mix]\ngap_ms = -40   ; overlap the joins\n[Cues]\nx = y\n");
    CHECK(neg.hasGapMs);
    CHECK(neg.gapMs == -40);          // comment stripped, sign kept
    CHECK(neg.phrases.at("x") == "y"); // and the section still ends properly

    CHECK(parse("[Mix]\ngap_ms = +25\n").gapMs == 25);

    // Junk keeps the default rather than becoming some number: this file is
    // hand-edited, and a pack that silently played its joins at 0 because of a
    // typo would be a puzzle to diagnose by ear.
    for (const char* bad : { "abc", "", "-", "4x", "1e3", "12.5" }) {
        const Pack p = parse(std::string("[Mix]\ngap_ms = ") + bad + "\n");
        CHECK_FALSE(p.hasGapMs);
    }
    // Unknown keys in the section are ignored like anywhere else.
    CHECK_FALSE(parse("[Mix]\nwidth = 3\n").hasGapMs);
    // A cue key is NOT reachable from [Mix], nor gap_ms from [Cues] — the
    // sections stay separate namespaces.
    CHECK(parse("[Mix]\nfinal_lap = nope\n").phrases.empty());
    CHECK_FALSE(parse("[Cues]\ngap_ms = -40\n").hasGapMs);
}

TEST_CASE("parse: never throws on hostile input") {
    CHECK(parse("").empty());
    CHECK(parse("[Cues]").empty());
    CHECK(parse("\n\n\n=\n===\n[\n]").empty());
    // Binary garbage on its own lines is skipped; the valid tail still loads.
    CHECK(parse(std::string(3, '\0') + "\xff\xfe\n[Cues]\nx=y").phrases.count("x") == 1);
}

namespace {
// A template's variables come from a Vars struct now; this builds one with
// just the fields a case cares about, so the cases stay about expansion.
SpotterVars::Vars vars(const std::string& rider = "",
                       const std::string& time = "",
                       const std::string& secs = "") {
    SpotterVars::Vars v;
    v.eventRider = rider;
    v.eventTime = time;
    v.penaltySeconds = secs;
    return v;
}
}  // namespace

TEST_CASE("expand: variables fill and empty ones tidy their punctuation") {
    CHECK(expand("Fastest lap, {event_rider}, {event_time}.",
                 vars("rider five", "one ten point two")) ==
          "Fastest lap, rider five, one ten point two.");
    // No time: the ", {event_time}" collapses instead of leaving ", ."
    CHECK(expand("Fastest lap, {event_rider}, {event_time}.", vars("rider five")) ==
          "Fastest lap, rider five.");
    // No rider either (session-scoped fastest lap): both collapse.
    CHECK(expand("Fastest lap, {event_rider}, {event_time}.", vars()) == "Fastest lap.");
    // Leading placeholder empty: no orphaned separator at the start. And the
    // line OPENS WITH A CAPITAL either way — the rider words are built for
    // mid-sentence, so a template leading with one used to render the subtitle
    // lowercase ("rider two oh six is out."). Fixed in expand() rather than by
    // an authoring rule, so it holds for packs nobody reviews.
    CHECK(expand("{event_rider} is out.", vars()) == "Is out.");
    CHECK(expand("{event_rider} is out.", vars("rider two oh six")) ==
          "Rider two oh six is out.");
    // No placeholders: text passes through untouched.
    CHECK(expand("Green green green.", vars("x", "y")) == "Green green green.");
    // {penalty_seconds} (penalty seconds): filled when known, tidied away when not —
    // the shipped packs' penalty templates rely on both directions.
    CHECK(expand("Penalty, penalty, {penalty_seconds}.", vars("", "", "five seconds")) ==
          "Penalty, penalty, five seconds.");
    CHECK(expand("Penalty, penalty, {penalty_seconds}.", vars()) == "Penalty, penalty.");
    CHECK(expand("Penalty for {event_rider}, {penalty_seconds}.",
                 vars("rider two oh six", "", "ten seconds")) ==
          "Penalty for rider two oh six, ten seconds.");
}

// The reason the variable set moved into a table: a template may name ANY
// variable in ANY cue. Before this, {position} was filled by the position report
// and by nothing else, so `overtime_started = You\'re {position}, {overtime_laps} to go.` produced
// "You\'re , 2 laps." — with no way to tell from the outside.
TEST_CASE("expand: any variable in any template, and typos stay visible") {
    SpotterVars::Vars v;
    v.position = "four";
    v.overtimeLaps = "two laps";
    v.gapToAhead = "one point two";
    v.riderAhead = "rider sixty five";
    CHECK(expand("You\'re P {position}, {overtime_laps}, {gap_to_ahead} to {rider_ahead}.", v) ==
          "You\'re P four, two laps, one point two to rider sixty five.");

    // Leading the field, with the pair in an OPTIONAL GROUP: the tidy-up can
    // drop an orphaned comma but cannot know the word "to" belongs to the
    // pair, so without the group this reads "P one, to."
    SpotterVars::Vars lead;
    lead.position = "one";
    CHECK(expand("P {position}[, {gap_to_ahead} to {rider_ahead}].", lead) == "P one.");
    CHECK(expand("P {position}[, {gap_to_ahead} to {rider_ahead}].", v) ==
          "P four, one point two to rider sixty five.");
    // Any empty variable drops the WHOLE group, not just its own text: a gap
    // with no rider to attribute it to is not half a sentence, it is wrong.
    SpotterVars::Vars half;
    half.position = "four";
    half.riderAhead = "rider sixty five";
    CHECK(expand("P {position}[, {gap_to_ahead} to {rider_ahead}].", half) == "P four.");
    // A group with no variables in it is just text, brackets removed.
    CHECK(expand("[hold station].", v) == "Hold station.");
    // Unpaired brackets are literal, like unpaired braces.
    CHECK(expand("a [ b", v) == "A [ b");

    // An unknown name is NOT a variable and is left exactly as written. This
    // is the only signal a pack author gets that they typed it wrong, so
    // swallowing it would be worse than printing it.
    CHECK(expand("gap {gap_ahed}.", v) == "Gap {gap_ahed}.");
    // An unpaired brace is literal text, not the start of a variable that
    // eats the rest of the line.
    CHECK(expand("100% {sure", v) == "100% {sure");
    CHECK(expand("}{}", v) == "}{}");
}

// Refinement keys falling back to the key they refine is what lets a pack
// write ONE line with {trend_behind} in it instead of three. Before it,
// defining `gap_behind` alone left the closing/dropping firings on the
// built-in wording — the pack looked complete and two thirds of the time it
// was not, with nothing to say so.
TEST_CASE("fallbackCueKey: refinements resolve to the key they refine") {
    CHECK(std::string(fallbackCueKey("gap_behind_closing")) == "gap_behind");
    CHECK(std::string(fallbackCueKey("gap_behind_dropping")) == "gap_behind");
    CHECK(std::string(fallbackCueKey("sector_completed_faster")) == "sector_completed");
    CHECK(std::string(fallbackCueKey("practice_started")) == "session_started");

    // A general key has no fallback of its own, which is what keeps the chain
    // one level deep and unable to loop.
    CHECK(fallbackCueKey("session_started") == nullptr);
    CHECK(fallbackCueKey("rider_behind") == nullptr);
    CHECK(fallbackCueKey("not_a_key") == nullptr);

    // Every fallback must name a real cue, and never itself.
    for (const CueKeyInfo& info : allCueKeys()) {
        if (const char* base = fallbackCueKey(info.key)) {
            CAPTURE(info.what);
            CHECK(isCueKey(base));
            CHECK(std::string(base) != info.key);
            CHECK(fallbackCueKey(base) == nullptr);   // one level only
        }
    }
}

// A mix recipe is resolved by SpotterMix, not by the variable expander, and
// the two have different vocabularies. A placeholder the mixer does not know
// used to fall through as a wav FILENAME: the file never exists, the whole
// recipe is dropped at playback and the cue lands on TTS — inaudible under
// Wine, where TTS does not exist, and with nothing anywhere to say why.
//
// It shipped that way. The pack generator baked `{gap_to_behind}` into the
// three gap recipes, which is the right name for the phrase and a name the
// mixer has never had, so every gap callout on a recorded pack was silent.
TEST_CASE("mix recipes: an unknown placeholder is rejected, not filed as a wav") {
    const SpotterCuePack::Pack p = SpotterCuePack::parse(
        "[Cues]\n"
        "gap_behind_mix = seg_behind.wav {gap_to_behind}\n"
        "fastest_lap_other_mix = fl.wav {event_rider} {event_time}\n");
    // The bad one is gone, and named — a dropped recipe with no diagnostic is
    // the failure this exists to end.
    CHECK(p.mixes.count("gap_behind") == 0);
    REQUIRE(p.rejectedMixes.size() == 1);
    CHECK(p.rejectedMixes[0].find("gap_behind") != std::string::npos);
    CHECK(p.rejectedMixes[0].find("{gap_to_behind}") != std::string::npos);
    // ...and a recipe naming only real placeholders is untouched.
    REQUIRE(p.mixes.count("fastest_lap_other") == 1);
    CHECK(p.mixes.at("fastest_lap_other").size() == 3);
}

// Every placeholder the mixer resolves must also pass the parser, or a recipe
// the plugin can play is thrown away at load. The two lists are one list now
// (SpotterMix::isMixToken); this is the check that says so.
TEST_CASE("mix recipes: every placeholder the mixer knows survives parsing") {
    for (const char* tok : { "{event_rider}", "{event_time}",
                             "{penalty_seconds}", "{overtime_laps}",
                             "{position}" }) {
        CAPTURE(tok);
        CHECK(SpotterMix::isMixToken(tok));
        const SpotterCuePack::Pack p = SpotterCuePack::parse(
            std::string("[Cues]\nrider_behind_mix = a.wav ") + tok + "\n");
        CHECK(p.rejectedMixes.empty());
        CHECK(p.mixes.count("rider_behind") == 1);
    }
}

// ============================================================================
// mergePhrases — the shipped pack under a selected one.
//
// THIS IS NOW A LIVE PATH. The shipped pack carries alternates on its
// most-heard cues, so every recorded pack that redefines one of them hits this
// merge. Before those alternates existed the rule was a comment guarding a
// case nothing could reach; the test is what makes it a rule.
// ============================================================================
TEST_CASE("mergePhrases: a selected cue owns its alternates") {
    Pack shipped;
    shipped.phrases = {
        { "rider_behind",   "Rider behind." },
        { "rider_behind_2", "Someone's tucked in behind you." },
        { "rider_behind_3", "Company behind." },
        { "blue_flag",      "Blue flag, faster rider closing." },
        { "blue_flag_2",    "Blue flag, let them by." },
    };
    // A recorded voice that redefines ONE of the two, as a real one does: it
    // has a clip for its own rider_behind and none for the shipped alternates.
    Pack sel;
    sel.phrases = { { "rider_behind", "On your tail." } };
    sel.wavs    = { { "rider_behind", "behind.wav" } };
    const auto merged = mergePhrases(shipped, sel);

    // The redefined cue speaks the selected words and ONLY those — rolling a
    // shipped alternate here would play no clip and drop to TTS, which on a
    // machine with no SAPI is silence for a cue the pack recorded properly.
    CHECK(merged.at("rider_behind") == "On your tail.");
    CHECK(merged.count("rider_behind_2") == 0);
    CHECK(merged.count("rider_behind_3") == 0);
    Pack rolled;
    rolled.phrases = merged;
    CHECK(variantKeys(rolled, "rider_behind").size() == 1);

    // The cue it left alone still inherits everything, alternates included —
    // which is the whole point of layering.
    CHECK(merged.at("blue_flag") == "Blue flag, faster rider closing.");
    CHECK(merged.at("blue_flag_2") == "Blue flag, let them by.");
}

// OWNERSHIP IS NOT ONLY ABOUT WORDS. A pack may hand a cue nothing but a clip
// and inherit its wording from the shipped file — spottergen writes both rows,
// but the format has never required it, and this is the pack with the MOST to
// lose: keeping the shipped alternates would have it roll a line it has no
// audio for, which is the exact inaudibility the rule exists to prevent.
TEST_CASE("mergePhrases: a wav-only cue owns its alternates too") {
    Pack shipped;
    shipped.phrases = {
        { "rider_behind",   "Rider behind." },
        { "rider_behind_2", "Company behind." },
        { "blue_flag",      "Blue flag." },
        { "blue_flag_2",    "Blue flag, let them by." },
    };
    Pack sel;
    sel.wavs  = { { "rider_behind", "behind.wav" } };
    sel.mixes = { { "blue_flag", { "bf.wav", "{event_rider}" } } };
    const auto merged = mergePhrases(shipped, sel);

    // Wording still inherited — that is what the pack asked for...
    CHECK(merged.at("rider_behind") == "Rider behind.");
    CHECK(merged.at("blue_flag") == "Blue flag.");
    // ...but not the alternates, which it has no audio for. A _mix row claims
    // the cue exactly as a _wav row does.
    CHECK(merged.count("rider_behind_2") == 0);
    CHECK(merged.count("blue_flag_2") == 0);
}

// An empty value is how a pack MUTES an inherited cue, so it has to survive a
// merge that would otherwise read it as "nothing to say, keep the shipped
// words". Same for the alternates it drags out with it.
TEST_CASE("mergePhrases: an empty selected value mutes rather than inherits") {
    Pack shipped;
    shipped.phrases = {
        { "rider_behind",   "Rider behind." },
        { "rider_behind_2", "Company behind." },
    };
    Pack sel;
    sel.phrases = { { "rider_behind", "" } };
    const auto merged = mergePhrases(shipped, sel);
    REQUIRE(merged.count("rider_behind") == 1);
    CHECK(merged.at("rider_behind").empty());
    CHECK(merged.count("rider_behind_2") == 0);
}

// A selected pack may ADD alternates to a cue it does not otherwise reword.
// The base row is then still the shipped one, and both must be selectable.
TEST_CASE("mergePhrases: a selected alternate joins an inherited base") {
    Pack shipped;
    shipped.phrases = { { "rider_behind", "Rider behind." } };
    Pack sel;
    sel.phrases = { { "rider_behind_2", "On your tail." } };
    const auto merged = mergePhrases(shipped, sel);
    CHECK(merged.at("rider_behind") == "Rider behind.");
    CHECK(merged.at("rider_behind_2") == "On your tail.");
}
