// ============================================================================
// tests/integration/harness/assertions.h
// Shared doctest assertions over the plugin's /api/state JSON. Include AFTER
// doctest.h (it uses the CHECK/REQUIRE macros). Keeps the standings-shape checks
// — the thing almost every integration test asserts — in one readable place.
// ============================================================================
#pragma once
#include <climits>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

// One expected standings row. String/best fields left empty and the numeric
// gapMs/penaltyMs left at UNCHECKED are simply not asserted, so each test spells
// out only what it cares about.
struct ExpectRow {
    static constexpr int UNCHECKED = INT_MIN;
    int pos;
    int num;
    std::string name;
    std::string gap;
    std::string best = "";
    int gapMs     = UNCHECKED;   // leader-relative gap in ms (race) — 0 for the leader
    int penaltyMs = UNCHECKED;   // accumulated penalty in ms
};

// Assert the standings array matches `want` in order, identity, gap, and any of
// best lap / gapMs / penaltyMs the row opts into. INFO() tags each row so a
// failure names the rider, not just an index.
inline void checkStandings(const nlohmann::json& d, const std::vector<ExpectRow>& want) {
    const nlohmann::json st = d.value("standings", nlohmann::json::array());
    REQUIRE(st.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        const nlohmann::json& s = st[i];
        INFO("standings[" << i << "] expected P" << want[i].pos
             << " #" << want[i].num << " " << want[i].name);
        CHECK(s.value("pos", -1) == want[i].pos);
        CHECK(s.value("num", -1) == want[i].num);
        CHECK(s.value("fullName", std::string()) == want[i].name);
        CHECK(s.value("gap", std::string()) == want[i].gap);
        if (!want[i].best.empty())
            CHECK(s.value("bestLap", std::string()) == want[i].best);
        if (want[i].gapMs != ExpectRow::UNCHECKED)
            CHECK(s.value("gapMs", -1) == want[i].gapMs);
        if (want[i].penaltyMs != ExpectRow::UNCHECKED)
            CHECK(s.value("penaltyMs", -1) == want[i].penaltyMs);
    }
}

// True if any event-log message contains `needle`.
inline bool hasEvent(const nlohmann::json& d, const std::string& needle) {
    for (const auto& e : d.value("events", nlohmann::json::array()))
        if (e.value("message", std::string()).find(needle) != std::string::npos)
            return true;
    return false;
}

// Look up a standings row by race number (null json if absent).
inline nlohmann::json riderByNum(const nlohmann::json& d, int num) {
    for (const auto& s : d.value("standings", nlohmann::json::array()))
        if (s.value("num", -1) == num) return s;
    return nlohmann::json();
}

// True if a standings row carries the given status chip (e.g. "fastest").
inline bool hasChip(const nlohmann::json& rider, const std::string& chip) {
    for (const auto& c : rider.value("chips", nlohmann::json::array()))
        if (c == chip) return true;
    return false;
}
