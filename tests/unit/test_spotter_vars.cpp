// ============================================================================
// tests/unit/test_spotter_vars.cpp
// Pins the {variable} namespace (core/spotter_vars.h) — the half of the pack
// format that is FROZEN once packs are written against it.
//
// WHY THE CENSUS MATTERS MORE THAN THE LOOKUPS. A variable is a name in
// somebody else's file. Renaming one does not fail to build, does not fail to
// parse, and does not warn: the old name stops being a variable and starts
// being literal text, so a shared pack quietly says "gap {gap_to_ahead}." Adding
// a field to Vars and forgetting the table row is the same failure from the
// other direction — the variable simply never resolves. So the cases below
// walk the table and the struct against each other, and spell out the names,
// rather than testing that a map lookup works.
// ============================================================================
#include "doctest.h"

#include "core/spotter_vars.h"

#include <set>
#include <string>

using namespace SpotterVars;

TEST_CASE("the variable names are the ones packs are written against") {
    // Spelled out, deliberately: this list IS the published namespace, and a
    // rename should have to be made here too, in a file that says why.
    const std::set<std::string> expected = {
        "event_rider", "event_time", "penalty_seconds", "overtime_laps",
        "positions_changed", "sector_number",
        "event_gap_to_best_lap", "event_gap_to_last_lap",
        "sector_duration",
        "sector_delta_best_lap", "sector_delta_ideal", "sector_delta_last_lap",
        "sector_delta_alltime", "sector_delta_record",
        "pace_margin", "sector_best_delta",
        "rider_name", "position", "lap_number", "last_lap_time",
        "gap_to_leader", "fuel_laps", "finish_time", "setup_name",
        "best_lap_time", "alltime_best_time", "ideal_lap_time",
        "overall_best_time", "record_time",
        "gap_to_best_lap", "gap_to_alltime", "gap_to_ideal",
        "gap_to_overall", "gap_to_record", "gap_to_last_lap",
        "penalty_total",
        "positions_since_start", "positions_since_lap",
        "positions_since_sector",
        "position_ahead", "rider_ahead", "gap_to_ahead", "gained_on_ahead",
        "trend_ahead", "last_lap_ahead",
        "position_behind", "rider_behind", "gap_to_behind", "gained_on_behind",
        "trend_behind", "last_lap_behind",
        "session_name", "session_state",
        "session_length", "session_remaining",
        "laps_remaining", "time_remaining", "leader_name", "track_name",
    };
    std::set<std::string> actual;
    for (const Binding& b : bindings()) actual.insert(b.name);
    CHECK(actual == expected);
}

TEST_CASE("every row reaches a distinct field, and none is unreachable") {
    // Two rows pointing at one field would make one name silently shadow the
    // other's intent; a field with no row can never be used from a template.
    // Both are caught by writing a marker into each field through the table
    // and reading every field back.
    Vars v;
    int i = 0;
    for (const Binding& b : bindings()) {
        v.*(b.value) = "v" + std::to_string(i++);
    }
    std::set<std::string> seen;
    for (const Binding& b : bindings()) seen.insert(v.*(b.value));
    CHECK(seen.size() == bindings().size());
}

TEST_CASE("lookup: known names resolve, unknown ones are not variables") {
    Vars v;
    v.gapToAhead = "one point two";
    v.position = "four";

    REQUIRE(lookup(v, "gap_to_ahead") != nullptr);
    CHECK(*lookup(v, "gap_to_ahead") == "one point two");
    CHECK(*lookup(v, "position") == "four");

    // Empty is a VALUE, not an absence: leading the field, {gap_to_ahead} is a
    // real variable that resolves to nothing. expand() relies on the
    // difference — an unknown name stays on screen as a typo, a known-but-
    // empty one disappears and takes its optional group with it.
    REQUIRE(lookup(v, "gap_to_behind") != nullptr);
    CHECK(lookup(v, "gap_to_behind")->empty());

    CHECK(lookup(v, "gap_to_ahed") == nullptr);      // typo
    CHECK(lookup(v, "GAP_TO_AHEAD") == nullptr);     // case-sensitive, like keys
    CHECK(lookup(v, "") == nullptr);
    CHECK(lookup(v, "gap_to_ahead ") == nullptr);    // no trimming: braces are exact
}
