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

#include "core/achievements.h"
#include "core/exploration_signals.h"

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

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
            CHECK(e.descOne != nullptr);              // a one-shot is one sentence
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
        CHECK(e.hidden == (e.group == Group::Hidden));
        if (e.hidden) ++hidden;
    }
    // Hidden is for surprises, used sparingly (the platform guides' word): a
    // tenth of the catalogue at most, so a fresh install's tab is not a quarter
    // empty.
    // Twelve percent: nine of eighty-five after Backup Plan went hidden.
    CHECK(hidden * 100 <= COUNT * 12);
    for (int g = 0; g < static_cast<int>(Group::COUNT); ++g) {
        INFO(groupName(static_cast<Group>(g)));
        CHECK(perGroup[g] >= 1);
        CHECK(std::strlen(groupName(static_cast<Group>(g))) > 0);
    }
    // The Tricks page is a full page of eight; Somersault left it for Hidden
    // so a fifth named trick would not spill a lone row onto a second page.
    CHECK(perGroup[static_cast<int>(Group::Tricks)] == 8);
    CHECK(findById("fmx_frontflips")->group == Group::Hidden);
}

TEST_CASE("achievements: a one-shot has one tier, no count, and a plain tag") {
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
    formatProgress(phoenix, 1, buf, sizeof(buf));
    CHECK(std::string(buf).empty());          // the tag says it all
    formatDescription(phoenix, 1, buf, sizeof(buf));
    CHECK(std::string(buf) == "Come back after a crash to desktop");
    // A tiered row keeps its metals.
    const Entry& races = *findById("races");
    CHECK_FALSE(isOneShot(races));
    CHECK(std::string(tierLabel(races, 2)) == "Silver");
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
    const Entry& dist = *findById("distance");   // 100, 1000, 10000, 50000 km
    CHECK(progressFraction(dist, 0) == doctest::Approx(0.0f));
    CHECK(progressFraction(dist, 50) == doctest::Approx(0.5f));
    // Just earned Bronze: the bar restarts toward Silver.
    CHECK(progressFraction(dist, 100) == doctest::Approx(0.0f));
    // Silver-to-Gold at 4,000 of 10,000: a third of the 1,000..10,000 step.
    CHECK(progressFraction(dist, 4000) == doctest::Approx(3000.0f / 9000.0f));
    // Everything earned: full, and stays full.
    CHECK(progressFraction(dist, 50000) == doctest::Approx(1.0f));
    CHECK(progressFraction(dist, 90000) == doctest::Approx(1.0f));
    CHECK(targetThreshold(dist, 0) == 100.0);
    CHECK(targetThreshold(dist, 2) == 10000.0);
    CHECK(targetThreshold(dist, 4) == 50000.0);   // complete rows aim at the last one
}

TEST_CASE("achievements: value formatting per unit") {
    char buf[32];
    formatWithCommas(1234567, buf, sizeof(buf));  CHECK(std::string(buf) == "1,234,567");
    formatWithCommas(999, buf, sizeof(buf));      CHECK(std::string(buf) == "999");
    formatWithCommas(-5, buf, sizeof(buf));       CHECK(std::string(buf) == "0");
    formatValue(Unit::Count, 10000, buf, sizeof(buf));   CHECK(std::string(buf) == "10,000");
    formatValue(Unit::Km, 2.34, buf, sizeof(buf));       CHECK(std::string(buf) == "2.3 km");
    formatValue(Unit::Km, 2.3, buf, sizeof(buf));        CHECK(std::string(buf) == "2.3 km");   // not "2.2": the x10 artifact
    formatValue(Unit::Km, 2340.7, buf, sizeof(buf));     CHECK(std::string(buf) == "2,340 km"); // truncated, never rounded up
    formatValue(Unit::Hours, 3.5, buf, sizeof(buf));     CHECK(std::string(buf) == "3.5 h");
    formatValue(Unit::Hours, 500, buf, sizeof(buf));     CHECK(std::string(buf) == "500 h");
    formatValue(Unit::Seconds, 45, buf, sizeof(buf));    CHECK(std::string(buf) == "45s");
    formatValue(Unit::Seconds, 90, buf, sizeof(buf));    CHECK(std::string(buf) == "1m 30s");
    formatValue(Unit::Seconds, 3900, buf, sizeof(buf));  CHECK(std::string(buf) == "1h 05m");
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
    formatDescription(dist, 2, buf, sizeof(buf));   CHECK(std::string(buf) == "Ride 1,000 km in total");
    const Entry& pen = *findById("penalty_time");
    formatDescription(pen, 3, buf, sizeof(buf));    CHECK(std::string(buf) == "Serve 10m 00s of penalty time");
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
    formatProgress(dist, 2340.5, buf, sizeof(buf));   CHECK(std::string(buf) == "2,340 / 10,000 km");
    formatProgress(dist, 0, buf, sizeof(buf));        CHECK(std::string(buf) == "0.0 / 100 km");
    // What it has, at the unit's own resolution: the first hour in minutes,
    // the first ten in tenths, a kilometre in tenths -- so a row is seen to
    // move (an hour row used to read "0 / 1 h" for fifty-nine minutes).
    const Entry& rumble = *findById("good_vibrations");
    formatProgress(rumble, 0.4, buf, sizeof(buf));    CHECK(std::string(buf) == "24m / 1 h");
    formatProgress(rumble, 1.45, buf, sizeof(buf));   CHECK(std::string(buf) == "1.4 / 10 h");
    formatProgress(rumble, 65.0, buf, sizeof(buf));   CHECK(std::string(buf) == "65 / 100 h");
    const Entry& wheelieKm = *findById("fmx_wheelie_km");
    formatProgress(wheelieKm, 0.45, buf, sizeof(buf)); CHECK(std::string(buf) == "0.4 / 1.0 km");
    // The text and the tier agree at the edge: 999.6 km is still short of Silver,
    // so it must not read "1,000 / 1,000 km" beside a locked bar.
    formatProgress(dist, 999.6, buf, sizeof(buf));    CHECK(std::string(buf) == "999 / 1,000 km");
    CHECK(tierFor(dist, 999.6) == 1);
    // Past Platinum the count keeps going against the last threshold: a
    // lifetime number never stops at the trophy.
    formatProgress(dist, 70000, buf, sizeof(buf));    CHECK(std::string(buf) == "70,000 / 50,000 km");
    CHECK(progressFraction(dist, 70000) == doctest::Approx(1.0f));
    const Entry& races = *findById("races");
    formatProgress(races, 7, buf, sizeof(buf));       CHECK(std::string(buf) == "7 / 10");
    const Entry& pen = *findById("penalty_time");
    formatProgress(pen, 75, buf, sizeof(buf));        CHECK(std::string(buf) == "1m 15s / 10m 00s");
    const Entry& air = *findById("fmx_airtime");
    formatProgress(air, 2.74, buf, sizeof(buf));      CHECK(std::string(buf) == "2.7s / 3.0s");
    formatDescription(air, 2, buf, sizeof(buf));      CHECK(std::string(buf) == "Stay airborne for 3.0s in one jump");
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
    out << "# Achievements\n\n";
    out << "GENERATED from `mxbmrp3/core/achievements.h` by `tests/unit/test_achievements.cpp` - do not edit. "
           "When the table changes, run the unit gate and copy `/tmp/achievements.new.md` over this file.\n\n";
    out << "A development overview of the whole catalogue: every row with its marker icon, its tiers as the "
           "toast will word them (a one-shot has one), and the page (group) it is "
           "listed on in Settings > Achievements. `crash state` rows exist only on games that report "
           "one; `FMX` rows only on games with freestyle detection; `DISABLED` marks an id in "
           "`kDisabledIds`. Hidden rows are unlisted in the tab until earned. Every row is one line in "
           "`kCatalogue`: delete it to remove the achievement, list its id to disable it.\n\n";
    int total = 0;
    for (int g = 0; g < static_cast<int>(Group::COUNT); ++g) {
        const Group group = static_cast<Group>(g);
        out << "## " << groupName(group) << "\n\n";
        out << "| Id | Icon | Achievement | Bronze | Silver | Gold | Platinum | Needs |\n";
        out << "|---|---|---|---|---|---|---|---|\n";
        for (const Entry& e : kCatalogue) {
            if (e.group != group) continue;
            ++total;
            out << "| `" << e.id << "` | ";
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
            out << " | " << (e.needsCrashState ? "crash state" : e.needsFmx ? "FMX" : "-")
                << (isDisabledId(e.id) ? ", DISABLED" : "") << " |\n";
        }
        out << "\n";
    }
    int tiers = 0;
    for (const Entry& e : kCatalogue) tiers += e.tierCount;
    out << total << " achievements, " << tiers << " tiers.\n";
    return out.str();
}

}  // namespace

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
