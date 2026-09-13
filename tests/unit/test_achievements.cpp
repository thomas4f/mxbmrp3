// ============================================================================
// tests/unit/test_achievements.cpp
// The achievement catalogue and its pure tier arithmetic (core/achievements.h):
// the table's own invariants (unique ids, ascending thresholds, a %s in every
// description that needs one), the tier/progress math the settings bar and the
// toasts are built on, and the value formatting.
//
// Compiles the REAL table. A row added with its thresholds out of order, or a
// description template with no %s, fails here in a second rather than in a
// player's toast reading "Finish %s races".
// ============================================================================
#include "doctest.h"

#include "core/achievement_text.h"
#include "core/achievements.h"
#include "core/exploration_signals.h"

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Achievements;

TEST_CASE("achievements: the catalogue is well-formed") {
    std::set<std::string> ids;
    for (const Entry& e : kCatalogue) {
        INFO("entry " << e.id);
        CHECK(e.id != nullptr);
        CHECK(std::strlen(e.id) > 0);
        CHECK(ids.insert(e.id).second);           // unique, it is a file key
        CHECK(e.title != nullptr);
        if (e.icon) CHECK(std::strlen(e.icon) > 0);   // nullptr = no marker art yet
        CHECK(e.tierCount >= 1);
        CHECK(e.tierCount <= TIER_COUNT);
        if (e.tierCount > 1) {
            CHECK(e.descMany != nullptr);
            CHECK(std::strstr(e.descMany, "%s") != nullptr);
        } else {
            // A one-shot is ONE sentence, but it may be either of them: a row
            // whose single tier sits at a number (Weakest Link's 2,500 points,
            // Frame Perfect's 480 FPS) says it through descMany's %s rather
            // than spelling the figure out beside the threshold, where the two
            // drift apart. Rows that spell it - Back Marker, 99 Problems,
            // Baker's Dozen - carry exactly that risk and predate this.
            CHECK((e.descOne != nullptr || e.descMany != nullptr));
        }
        if (e.descOne) CHECK(std::strstr(e.descOne, "%s") == nullptr);   // no threshold in the singular
        for (int i = 1; i < e.tierCount; ++i) {
            CHECK(e.thresholds[i] > e.thresholds[i - 1]);
        }
        CHECK(e.thresholds[0] > 0.0);
        CHECK(static_cast<int>(e.metric) < static_cast<int>(Metric::COUNT));
        // The signal is the row's whole identity for Metric::Exploration and
        // meaningless otherwise.
        CHECK((e.metric == Metric::Exploration) == (e.signal != Exploration::Signal::None));
    }
    CHECK(COUNT == static_cast<int>(ids.size()));
}

// THE REMOVAL RULE (achievements.h): every metric and every signal feeds
// exactly one row. Delete a row and its metric case or signal line has to go
// too -- which takes the one feed call with it -- or this says so. The two
// generic metrics (Exploration, Completion) are the exception on the metric
// side, since the signal carries the one-to-one there.
TEST_CASE("achievements: every id fits the usage ping's key cap with its prefix") {
    // analytics_manager.cpp sends "ach_<id>" = tier for each earned row, and
    // Aptabase rejects the whole event when any prop key passes 40 characters
    // (its EventBody.ValidateProps). A long id would silently cost every launch
    // ping of everyone who earned that row.
    for (const Entry& e : kCatalogue) {
        INFO(e.id);
        CHECK(std::strlen(e.id) + 4 <= 40);
    }
}

TEST_CASE("achievements: every metric and every signal feeds exactly one row") {
    int perMetric[static_cast<int>(Metric::COUNT)] = {};
    int perSignal[Exploration::SIGNAL_COUNT] = {};
    for (const Entry& e : kCatalogue) {
        perMetric[static_cast<int>(e.metric)]++;
        if (e.signal != Exploration::Signal::None) perSignal[static_cast<int>(e.signal)]++;
    }
    for (int m = 0; m < static_cast<int>(Metric::COUNT); ++m) {
        INFO("metric " << m);
        if (m == static_cast<int>(Metric::Exploration)) continue;
        CHECK(perMetric[m] == 1);
    }
    for (int i = 1; i < Exploration::SIGNAL_COUNT; ++i) {
        INFO("signal " << Exploration::signalKey(static_cast<Exploration::Signal>(i)));
        CHECK(perSignal[i] == 1);
        CHECK(std::strlen(Exploration::signalKey(static_cast<Exploration::Signal>(i))) > 0);
    }
    // kSignals keys are unique file keys, in enum order.
    std::set<std::string> keys;
    for (const Exploration::SignalInfo& si : Exploration::kSignals) {
        CHECK(keys.insert(si.key).second);
        CHECK(std::string(Exploration::signalKey(si.signal)) == si.key);
    }
}

TEST_CASE("achievements: a disabled id names a real row") {
    for (const char* id : kDisabledIds) {
        if (!id) continue;
        INFO(id);
        CHECK(findById(id) != nullptr);
        CHECK(isDisabledId(id));
    }
    CHECK_FALSE(isDisabledId("races"));
    CHECK_FALSE(isDisabledId(nullptr));
}

TEST_CASE("achievements: every group has rows and a name; hidden rows live on the Hidden page, and few") {
    int perGroup[static_cast<int>(Group::COUNT)] = {};
    int hidden = 0;
    for (const Entry& e : kCatalogue) {
        CHECK(static_cast<int>(e.group) < static_cast<int>(Group::COUNT));
        perGroup[static_cast<int>(e.group)]++;
        // The flag and the page agree, both ways. TWO groups carry it now -- see
        // Achievements::isUnlistedGroup -- and they are different ideas sharing
        // one rule, so this is where the sharing is pinned: a Misfortune row
        // left counted would sit on a page that says it is not, and a row filed
        // anywhere else with the flag set would vanish from its own page.
        CHECK(e.hidden == isUnlistedGroup(e.group));
        if (e.group == Group::Hidden) ++hidden;
    }
    // Hidden is for surprises, used sparingly (the platform guides' word), so a
    // fresh install's tab is not a quarter empty.
    //
    // COUNTS THE PAGE, not the flag, and that is not a loosening: the flag now
    // also covers Misfortune, whose rows are unlisted for a different reason
    // and are not competing for the surprise budget at all. Counting the flag
    // would have this fail at twenty-five the moment those ten moved, on a
    // Hidden page that had not gained a single row - which is the tripwire
    // firing at the one thing it was never watching for.
    //
    // THIS NUMBER HAS BEEN RAISED THREE TIMES AND HAS NEVER ONCE CHANGED A
    // DECISION - 12 -> 17 -> 20, each time to admit rows that had already been
    // chosen for the Hidden page on their merits. A budget that only ever moves
    // to fit what was going to ship anyway is not a budget; it is a step in the
    // way. It is kept at 20 as a TRIPWIRE against the page doubling by accident,
    // not as a design rule - if it fails again, the question to ask is whether
    // the Hidden page has become a dumping ground, and if the answer is no,
    // delete this check rather than raise it a fourth time.
    //   12: nine of eighty-five, after Backup Plan went hidden.
    //   17: fourteen of eighty-eight, once the rows a player cannot aim at from
    //       inside the game joined them - the dev-only switches (Test Pilot,
    //       Developer, Homebrew), the hardware-gated Frame Perfect, Good
    //       Vibrations while an unseen pad blocks it, and Version Hopper, which
    //       waits on our release cadence rather than on the player.
    //   20: eighteen of a hundred and two - Stylist (nobody has ever earned it),
    //       Completionist, Just Me Then and Big Hit, all chosen deliberately.
    // Fifteen of a hundred and six today, and it has not moved since.
    CHECK(hidden * 100 <= COUNT * 20);
    for (int g = 0; g < static_cast<int>(Group::COUNT); ++g) {
        INFO(groupName(static_cast<Group>(g)));
        CHECK(perGroup[g] >= 1);
        CHECK(std::strlen(groupName(static_cast<Group>(g))) > 0);
    }
    // The Tricks page is exactly a page: five named flips and rotations plus
    // the three wheel-up rows. Tyre Shredder and One-Wheel Tour are not tricks
    // and sit in Freestyle, which is what left room for Somersault and Endo.
    CHECK(perGroup[static_cast<int>(Group::Tricks)] == 8);
    CHECK(findById("fmx_frontflips")->group == Group::Tricks);
    CHECK(findById("fmx_endos")->group == Group::Tricks);
    CHECK(findById("tyre_shredder")->group == Group::Freestyle);
    CHECK(findById("fmx_wheelie_km")->group == Group::Freestyle);
}

TEST_CASE("achievements: a one-shot has one tier, a plain tag, and still counts") {
    const Entry& phoenix = *findById("phoenix");
    CHECK(isOneShot(phoenix));
    CHECK(tierFor(phoenix, 0) == 0);
    CHECK(tierFor(phoenix, 1) == 1);
    CHECK(tierFor(phoenix, 7) == 1);          // never past its one tier
    CHECK(std::string(tierLabel(phoenix, 0)) == "Locked");
    CHECK(std::string(tierLabel(phoenix, 1)) == "Earned");
    CHECK(progressFraction(phoenix, 0) == doctest::Approx(0.0f));
    CHECK(progressFraction(phoenix, 1) == doctest::Approx(1.0f));
    char buf[48];
    // A one-shot counts like every other row, its single-event bar included: a
    // flag row reads "0 / 1" then "1 / 1", and a row with something behind it
    // keeps going past the bar, which is the only place that number is shown.
    formatProgress(phoenix, 0, buf, sizeof(buf));
    CHECK(std::string(buf) == "0 / 1");
    formatProgress(phoenix, 1, buf, sizeof(buf));
    CHECK(std::string(buf) == "1 / 1");
    const Entry& quits = *findById("rage_quit");
    REQUIRE(isOneShot(quits));
    formatProgress(quits, 100, buf, sizeof(buf));
    CHECK(std::string(buf) == "100 / 1");     // asked for one, walked away from a hundred
    const Entry& steady = *findById("steady_hands");
    REQUIRE(isOneShot(steady));
    formatProgress(steady, 3600, buf, sizeof(buf));
    CHECK(std::string(buf) == "1h / 30min");  // and the same in its own unit
    formatDescription(phoenix, 1, buf, sizeof(buf));
    CHECK(std::string(buf) == "Come back after a crash to desktop");
    // A tiered row keeps its metals.
    const Entry& races = *findById("races");
    CHECK_FALSE(isOneShot(races));
    CHECK(std::string(tierLabel(races, 2)) == "Silver");
}

TEST_CASE("achievements: a singular description is only there when tier one IS one") {
    // descOne renders ONLY when the first threshold is 1 (or there is no
    // descMany). A row that opens at ten and still carries a singular is
    // carrying text no player can ever see, and the text says the opposite of
    // the row: Whip It and Scrubber both said "Land a whip" / "Land a scrub"
    // while Bronze was ten of them. Harmless on screen, which is exactly why
    // it sat there - the only reader it misled was the next person editing the
    // catalogue.
    for (const Entry& e : kCatalogue) {
        if (!e.descOne || !e.descMany) continue;
        const std::string where = std::string(e.id) + " (" + e.title + ")";
        INFO(where);
        CHECK(e.thresholds[0] == 1);
    }
}

TEST_CASE("achievements: findById is exact and null-safe") {
    CHECK(std::string(findById("races")->id) == "races");
    CHECK(findById("races")->group == Group::Racing);
    CHECK(findById("race") == nullptr);
    CHECK(findById("") == nullptr);
    CHECK(findById(nullptr) == nullptr);
}

TEST_CASE("achievements: tiers step at the thresholds, inclusive") {
    const Entry& races = *findById("races");   // 1, 10, 100, 500
    CHECK(tierFor(races, 0) == 0);
    CHECK(tierFor(races, 1) == 1);
    CHECK(tierFor(races, 9) == 1);
    CHECK(tierFor(races, 10) == 2);
    CHECK(tierFor(races, 99.999) == 2);
    CHECK(tierFor(races, 100) == 3);
    CHECK(tierFor(races, 500) == 4);
    CHECK(tierFor(races, 1e9) == 4);
    CHECK(std::string(tierName(0)) == "Locked");
    CHECK(std::string(tierName(1)) == "Bronze");
    CHECK(std::string(tierName(4)) == "Platinum");
    CHECK(std::string(tierName(5)) == "Locked");   // out of range reads as nothing earned
}

TEST_CASE("achievements: the bar measures the current step, not the whole ladder") {
    const Entry& dist = *findById("distance");   // 100, 1000, 5000, 25000 km
    CHECK(progressFraction(dist, 0) == doctest::Approx(0.0f));
    CHECK(progressFraction(dist, 50) == doctest::Approx(0.5f));
    // Just earned Bronze: the bar restarts toward Silver.
    CHECK(progressFraction(dist, 100) == doctest::Approx(0.0f));
    // Silver-to-Gold at 4,000 of 5,000: three quarters of the 1,000..5,000 step.
    CHECK(progressFraction(dist, 4000) == doctest::Approx(3000.0f / 4000.0f));
    // Everything earned: full, and stays full.
    CHECK(progressFraction(dist, 25000) == doctest::Approx(1.0f));
    CHECK(progressFraction(dist, 90000) == doctest::Approx(1.0f));
    CHECK(targetThreshold(dist, 0) == 100.0);
    CHECK(targetThreshold(dist, 2) == 5000.0);
    CHECK(targetThreshold(dist, 4) == 25000.0);   // complete rows aim at the last one
}

TEST_CASE("achievements: value formatting per unit") {
    char buf[32];
    formatWithCommas(1234567, buf, sizeof(buf));  CHECK(std::string(buf) == "1,234,567");
    formatWithCommas(999, buf, sizeof(buf));      CHECK(std::string(buf) == "999");
    formatWithCommas(-5, buf, sizeof(buf));       CHECK(std::string(buf) == "0");
    formatValue(Unit::Count, 10000, buf, sizeof(buf));   CHECK(std::string(buf) == "10,000");
    formatValue(Unit::Km, 2.34, buf, sizeof(buf));       CHECK(std::string(buf) == "2.3km");
    formatValue(Unit::Km, 2.3, buf, sizeof(buf));        CHECK(std::string(buf) == "2.3km");   // not "2.2": the x10 artifact
    formatValue(Unit::Km, 2340.7, buf, sizeof(buf));     CHECK(std::string(buf) == "2,340km"); // truncated, never rounded up
    formatValue(Unit::Hours, 3.5, buf, sizeof(buf));     CHECK(std::string(buf) == "3.5h");
    formatValue(Unit::Hours, 500, buf, sizeof(buf));     CHECK(std::string(buf) == "500h");
    formatValue(Unit::Seconds, 45, buf, sizeof(buf));    CHECK(std::string(buf) == "45s");
    formatValue(Unit::Seconds, 90, buf, sizeof(buf));    CHECK(std::string(buf) == "1min 30s");
    formatValue(Unit::Seconds, 3900, buf, sizeof(buf));  CHECK(std::string(buf) == "1h 5min");
    formatValue(Unit::SecondsTenths, 2.74, buf, sizeof(buf)); CHECK(std::string(buf) == "2.7s");
    formatValue(Unit::SecondsTenths, 2.96, buf, sizeof(buf)); CHECK(std::string(buf) == "2.9s");   // 2.96 s is not "3.0s"
    // A too-small buffer is a truncation, never an overrun or a garbage string.
    char tiny[4];
    formatWithCommas(1234567, tiny, sizeof(tiny));
    CHECK(std::strlen(tiny) < sizeof(tiny));
}

TEST_CASE("achievements: a credit is a handle, appended to every tier's sentence") {
    // The dedication ends the sentence ("Bind 15 hotkeys (@Vegi)") on the tab,
    // the toast and the doc; the row-budget case below is what keeps a
    // credited sentence short enough with the handle on it.
    int credited = 0;
    for (const Entry& e : kCatalogue) {
        if (!e.credit) continue;
        ++credited;
        INFO(e.id);
        CHECK(e.credit[0] == '@');
        CHECK(std::strstr(e.descOne ? e.descOne : "", "@") == nullptr);   // in the field, not typed in
        CHECK(std::strstr(e.descMany ? e.descMany : "", "@") == nullptr);
        char buf[64];
        for (int t = 1; t <= e.tierCount; ++t) {
            formatDescription(e, t, buf, sizeof(buf));
            char want[48];
            snprintf(want, sizeof(want), "(%s)", e.credit);
            CHECK(std::strstr(buf, want) != nullptr);
        }
    }
    CHECK(credited > 0);          // the rule is exercised; the count itself lives in the overview
}

TEST_CASE("achievements: descriptions read as sentences at every tier") {
    char buf[64];
    const Entry& races = *findById("races");
    formatDescription(races, 1, buf, sizeof(buf));  CHECK(std::string(buf) == "Finish a race");
    formatDescription(races, 2, buf, sizeof(buf));  CHECK(std::string(buf) == "Finish 10 races");
    formatDescription(races, 4, buf, sizeof(buf));  CHECK(std::string(buf) == "Finish 500 races");
    formatDescription(races, 0, buf, sizeof(buf));  CHECK(std::string(buf) == "Finish a race");    // clamped
    formatDescription(races, 9, buf, sizeof(buf));  CHECK(std::string(buf) == "Finish 500 races"); // clamped
    const Entry& dist = *findById("distance");
    formatDescription(dist, 2, buf, sizeof(buf));   CHECK(std::string(buf) == "Ride 1,000km in total");
    // A SECONDS row with real tiers, for the minutes form. Penalty Time was
    // this example until it became a one-shot; Tyre Shredder is the same unit
    // and still climbs, so the case reads the same thing.
    const Entry& shred = *findById("tyre_shredder");
    formatDescription(shred, 2, buf, sizeof(buf));  CHECK(std::string(buf) == "10min in burnouts and drifts");
    // Every row, every tier: no template leaks through unformatted, and the
    // sentence shares the tab's task row with the widest numbers it can show
    // while that tier is the next one (Achievements::TAB_ROW_CHARS). The
    // toast's detail row is wider than that, so this is the one cap.
    for (const Entry& e : kCatalogue) {
        for (int t = 1; t <= e.tierCount; ++t) {
            formatDescription(e, t, buf, sizeof(buf));
            INFO(e.id << " tier " << t << ": " << buf);
            CHECK(std::strstr(buf, "%s") == nullptr);
            CHECK(std::strlen(buf) > 0);
            char numbers[48];
            // Just under the tier's threshold ("9,999 / 10,000"), and, for the
            // top tier, the complete row ("10,000 / 10,000").
            const double under = e.thresholds[t - 1] > 1.0 ? e.thresholds[t - 1] - 1.0 : 0.0;
            for (double value : { under, t == e.tierCount ? e.thresholds[t - 1] : under }) {
                formatProgress(e, value, numbers, sizeof(numbers));
                const size_t row = std::strlen(buf) + (numbers[0] ? std::strlen(numbers) + 1 : 0);
                INFO("with numbers \"" << numbers << "\": " << row << " chars");
                CHECK(row <= static_cast<size_t>(TAB_ROW_CHARS));
            }
        }
    }
}

TEST_CASE("achievements: progress text is current / target, unit once") {
    char buf[48];
    const Entry& dist = *findById("distance");
    formatProgress(dist, 2340.5, buf, sizeof(buf));   CHECK(std::string(buf) == "2,340 / 5,000km");
    formatProgress(dist, 0, buf, sizeof(buf));        CHECK(std::string(buf) == "0.0 / 100km");
    // What it has, at the unit's own resolution: the first hour in minutes,
    // the first ten in tenths, a kilometre in tenths -- so a row is seen to
    // move (an hour row used to read "0 / 1 h" for fifty-nine minutes).
    const Entry& hours = *findById("ride_time");
    formatProgress(hours, 0.4, buf, sizeof(buf));     CHECK(std::string(buf) == "24min / 10h");
    formatProgress(hours, 1.45, buf, sizeof(buf));    CHECK(std::string(buf) == "1.4 / 10h");
    formatProgress(hours, 65.0, buf, sizeof(buf));    CHECK(std::string(buf) == "65 / 100h");
    const Entry& wheelieKm = *findById("fmx_wheelie_km");
    formatProgress(wheelieKm, 0.45, buf, sizeof(buf)); CHECK(std::string(buf) == "0.4 / 1.0km");
    // The text and the tier agree at the edge: 999.6 km is still short of Silver,
    // so it must not read "1,000 / 1,000km" beside a locked bar.
    formatProgress(dist, 999.6, buf, sizeof(buf));    CHECK(std::string(buf) == "999 / 1,000km");
    CHECK(tierFor(dist, 999.6) == 1);
    // Past Platinum the count keeps going against the last threshold: a
    // lifetime number never stops at the trophy.
    formatProgress(dist, 70000, buf, sizeof(buf));    CHECK(std::string(buf) == "70,000 / 25,000km");
    CHECK(progressFraction(dist, 70000) == doctest::Approx(1.0f));
    const Entry& races = *findById("races");
    formatProgress(races, 7, buf, sizeof(buf));       CHECK(std::string(buf) == "7 / 10");
    // 75s is past Bronze (60s) and climbing to Silver (600s): the HAVE half in
    // its compound short form, the WANT half in whole minutes.
    const Entry& shred = *findById("tyre_shredder");
    formatProgress(shred, 75, buf, sizeof(buf));      CHECK(std::string(buf) == "1min 15s / 10min");
    const Entry& air = *findById("fmx_airtime");
    formatProgress(air, 2.74, buf, sizeof(buf));      CHECK(std::string(buf) == "2.7s / 3.0s");
    formatDescription(air, 2, buf, sizeof(buf));      CHECK(std::string(buf) == "Stay airborne 3.0s and land it");
}

// ============================================================================
// docs/achievements.md is GENERATED from the table above and diffed against
// the committed copy, the way the spotter reference is: a row added, retitled,
// regrouped or re-tiered changes the generated text, this fails, and
// committing the regenerated file is the fix. The overview exists so the whole
// catalogue can be read at once -- names, icons, tiers, groups -- which the
// six-per-page tab cannot show, and a copy maintained by hand would drift the
// first time a threshold moved.
// ============================================================================
namespace {

std::string generateAchievementsDoc() {
    std::ostringstream out;
    // SHORT ON PURPOSE. This is the list players look things up in, so the
    // preamble says the four things they need and stops: what the tiers are,
    // what Hidden means, what "(not counted)" means, and how the numbering
    // works. The game-gate roll-call that used to be here went with
    // GAME_HAS_ACHIEVEMENTS -- achievements are MX Bikes only now, and that
    // game has every gate, so no row is ever missing.
    out << "# Achievements\n\n";
    out << "Every achievement in the plugin, with its tiers worded the way the toast words them "
           "(a one-shot has one). **MX Bikes only.**\n\n";
    out << "A **Hidden** row does not appear in Settings > Achievements until it is earned, and the "
           "pages marked *(does not count towards progress)* sit outside the Unlocked figure and the "
           "Completion rows. The leading number is a row's place in its GROUP, "
           "which the tab pages " << ENTRIES_PER_PAGE
        << " at a time; a row switched off reads **off** instead.\n\n";
    out << "<sub>Generated from `mxbmrp3/core/achievements.h` by `tests/unit/test_achievements.cpp` - "
           "do not edit. Run the unit gate and copy `/tmp/achievements.new.md` over this file.</sub>\n\n";
    int total = 0;
    for (int g = 0; g < static_cast<int>(Group::COUNT); ++g) {
        const Group group = static_cast<Group>(g);
        out << "## " << groupName(group)
            << (countsTowardCompletion(group) ? "" : " (does not count towards progress)")
            << "\n\n";
        // COLUMN LAYOUT IS PARSED, not just read: tools/analytics_report.py's
        // _CAT_ROW reads this table for the achievement chart's labels, icons
        // and tier wording, so a column added or moved here needs that regex
        // changed with it. Its selftest (the `analytics-selftest` gate) fails
        // when they disagree - that is the enforcement, not this comment.
        // (Dropping the trailing Needs column needed no regex change: the
        // pattern ends at Platinum's cell and its pipe, and ignored the rest.)
        out << "| # | Id | Icon | Achievement | Bronze | Silver | Gold | Platinum |\n";
        out << "|--:|---|---|---|---|---|---|---|\n";
        int position = 0;
        for (const Entry& e : kCatalogue) {
            if (e.group != group) continue;
            ++total;
            // Only a row the game actually lists takes a position; a disabled
            // one is skipped there, so numbering it would make every position
            // below it wrong about the page it is on.
            const bool off = isDisabledId(e.id);
            // "off" rather than a dash, in the leftmost cell: a switched-off row
            // has to be findable by scanning ONE column, not by reading to the
            // end of a wide line. It is also why the marker sits here and not
            // only in Needs, where it reads as a footnote to a game gate.
            if (off) { out << "| off | `"; } else { out << "| " << ++position << " | `"; }
            out << e.id << "` | ";
            // The marker art itself, from the SVG the shipped .tga is cut from
            // (the same embed README uses for the tab icons); its name is the
            // hover title. A row with no art yet shows nothing.
            if (e.icon) {
                out << "<img src=\"../assets/icons/" << e.icon << ".svg\" width=\"28\" height=\"28\" alt=\"\" title=\""
                    << e.icon << "\">";
            }
            out << " | " << e.title;
            char buf[64];
            for (int t = 1; t <= TIER_COUNT; ++t) {
                if (t > e.tierCount) { out << " | -"; continue; }
                formatDescription(e, t, buf, sizeof(buf));
                out << " | " << buf;
            }
            // NO "Needs" COLUMN. It was 79 dashes in 107 rows, and the 28 that
            // said anything cluster so hard that the preamble says it in two
            // sentences instead - every Freestyle, Air and Tricks row is FMX,
            // and the six crash-state rows are named. DISABLED went with it:
            // the leftmost column already reads "off" for those, deliberately
            // (see above), so it was only ever a second copy.
            out << " |\n";
        }
        out << "\n";
    }
    // Two counts, because they answer different questions: the catalogue's size
    // (what a developer maintains) and what a player can actually earn. They
    // differ by whatever is in kDisabledIds, which is exactly when quoting one
    // number for both would mislead.
    int tiers = 0, liveRows = 0, liveTiers = 0;
    for (const Entry& e : kCatalogue) {
        tiers += e.tierCount;
        if (isDisabledId(e.id)) continue;
        ++liveRows;
        liveTiers += e.tierCount;
    }
    out << total << " achievements, " << tiers << " tiers";
    if (liveRows != total) {
        out << " - " << (total - liveRows) << " switched off, leaving " << liveRows
            << " achievements and " << liveTiers << " tiers in game";
    }
    out << ".\n";
    return out.str();
}

}  // namespace

// THE RULE THAT USED TO LIVE HERE. "Racing is a career, Moments is a single
// occurrence" -- a Racing row had to have four tiers, a Moments row exactly
// one, with Lapped the Field named as the single exception. It existed because
// the two groups were otherwise told apart by taste, and had drifted: Photo
// Finish sat in Racing while Wire to Wire sat in Moments.
//
// Moments is gone. Its race-shaped rows (Photo Finish, Sandbagger, Running on
// Fumes) went to Racing and the two about how you ride (Metronome, Steady
// Hands) went to Consistency, so there is no second group for the rule to sort
// against and nothing for "or move it to Moments" to mean. Racing now holds
// tiered career rows and one-shot race outcomes side by side, which is what
// every other group already did -- Plugin and Tinkering are sorted by SUBJECT,
// not by shape, and Racing is now sorted the same way.
//
// Deleted rather than rewritten as "Racing may hold anything", which asserts
// nothing. If a rule is wanted here again it has to be a real one, and it will
// need a second group to sort into.

TEST_CASE("every marker icon a row names has its SVG source") {
    // The overview embeds assets/icons/<icon>.svg; the shipped .tga is cut from
    // the same file, so a row naming art that does not exist is caught here.
    for (const Entry& e : kCatalogue) {
        if (!e.icon) continue;
        const std::string path = std::string(MXB_DOCS_DIR) + "/../assets/icons/" + e.icon + ".svg";
        std::ifstream f(path, std::ios::binary);
        INFO(e.id << " names icon " << e.icon);
        CHECK(f.good());
    }
}

TEST_CASE("docs/achievements.md is current") {
    const std::string generated = generateAchievementsDoc();
    const std::string path = std::string(MXB_DOCS_DIR) + "/achievements.md";

    // Always write what this run produced - it is the input to the regen step
    // and costs nothing when the test passes.
    const std::string fresh = "/tmp/achievements.new.md";
    {
        std::ofstream out(fresh, std::ios::binary);
        if (out.good()) out << generated;
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(),
                    "missing " << path << " - seed it with: cp " << fresh << " " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    CHECK_MESSAGE(ss.str() == generated,
                  "docs/achievements.md is stale: the catalogue changed and the generated "
                  "overview no longer matches. Review the diff, then: cp " << fresh << " " << path);
}
