// ============================================================================
// tests/unit/test_deck_meter.cpp
// Unit tests for hud/deck_meter.h -- which block of a deck's meter is
// lit, hot, in the zone or toned, the one rule the bar, block and arc meters
// all draw by (deck_hud_draw.cpp).
//
// THE CENTRED METER IS THE TRAP: the Slide deck's runs -1..1 from the middle,
// and its zone and hot point are measured OUTWARD from there on both sides. A
// rule measured from the near end instead would still light the right number
// of blocks and pass every count check, so each case below names a side.
// ============================================================================
#include "doctest.h"

#include "hud/deck_meter.h"
#include "hud/deck_layout.h"
#include "core/fmx_types.h"
#include "core/layout_metrics.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

using namespace DeckMeter;

TEST_CASE("a plain meter lights the blocks below its value") {
    Data d;
    d.value = 0.5f;
    CHECK(blockState(d, 0.0f, 0.25f) == LIT);
    CHECK(blockState(d, 0.25f, 0.5f) == LIT);
    CHECK(blockState(d, 0.5f, 0.75f) == DIM);
    CHECK(blockState(d, 0.75f, 1.0f) == DIM);
}

TEST_CASE("past the hot point a block is hot, lit or not") {
    Data d;
    d.value = 0.9f;
    d.hotFrom = 0.8f;
    CHECK(blockState(d, 0.7f, 0.8f) == LIT);
    CHECK(blockState(d, 0.8f, 0.9f) == HOT_LIT);
    CHECK(blockState(d, 0.9f, 1.0f) == HOT_DIM);
}

TEST_CASE("the zone wins over the hot point inside it") {
    Data d;
    d.value = 0.6f;
    d.zoneFrom = 0.4f;
    d.zoneTo = 0.7f;
    d.hotFrom = 0.5f;   // inside the zone: the zone still reads as the zone
    CHECK(blockState(d, 0.4f, 0.5f) == ZONE_LIT);
    CHECK(blockState(d, 0.6f, 0.7f) == ZONE_DIM);
    CHECK(blockState(d, 0.8f, 0.9f) == HOT_DIM);
}

TEST_CASE("a centred meter lights from the middle toward the value's side only") {
    Data d;
    d.centered = true;
    d.value = 0.5f;   // half way out to the right
    CHECK(blockState(d, 0.5f, 0.6f) == LIT);   // just right of the middle
    CHECK(blockState(d, 0.7f, 0.75f) == LIT);  // at |c| 0.45
    CHECK(blockState(d, 0.8f, 0.9f) == DIM);   // past the value
    CHECK(blockState(d, 0.4f, 0.5f) == DIM);   // the left side stays dark
    d.value = -0.5f;
    CHECK(blockState(d, 0.4f, 0.5f) == LIT);
    CHECK(blockState(d, 0.5f, 0.6f) == DIM);
    d.value = 0.0f;
    CHECK(blockState(d, 0.45f, 0.55f) == DIM);
}

TEST_CASE("a mirrored meter lights both sides out to the value, in the colour it is lit as") {
    // The Combo deck's chain: the time left shrinks to the middle
    Data d;
    d.centered = d.mirrored = true;
    d.value = 0.5f;
    CHECK(blockState(d, 0.5f, 0.6f) == LIT);    // just right of the middle
    CHECK(blockState(d, 0.4f, 0.5f) == LIT);    // and just left of it
    CHECK(blockState(d, 0.8f, 0.9f) == DIM);    // past |value| on the right
    CHECK(blockState(d, 0.1f, 0.2f) == DIM);    // and on the left
    d.litAs = DANGER_DIM;                       // running out, the blink's off beat
    CHECK(blockState(d, 0.4f, 0.5f) == DANGER_DIM);
    CHECK(blockState(d, 0.8f, 0.9f) == DIM);    // an unlit block stays plain
    d.value = 0.0f;
    CHECK(blockState(d, 0.45f, 0.55f) == DIM);
}

TEST_CASE("a centred meter's zone and hot point mirror on both sides") {
    Data d;
    d.centered = true;
    d.zoneFrom = 0.2f;
    d.zoneTo = 0.6f;
    d.hotFrom = 0.8f;
    d.value = -1.0f;   // full left
    // |c| 0.3, on either side: the zone
    CHECK(blockState(d, 0.3f, 0.4f) == ZONE_LIT);
    CHECK(blockState(d, 0.6f, 0.7f) == ZONE_DIM);
    // |c| 0.9: hot on both sides
    CHECK(blockState(d, 0.0f, 0.1f) == HOT_LIT);
    CHECK(blockState(d, 0.9f, 1.0f) == HOT_DIM);
}

TEST_CASE("a band meter lights only the band holding the value, never the plain run") {
    // The Wheelie arc: 0..1, the zone 0.4..0.7, hot past it
    Data d;
    d.band = true;
    d.zoneFrom = 0.4f;
    d.zoneTo = 0.7f;
    d.hotFrom = 0.7f;
    d.value = 0.2f;   // in the plain run: nothing lit
    CHECK(blockState(d, 0.0f, 0.4f) == DIM);
    CHECK(blockState(d, 0.4f, 0.7f) == ZONE_DIM);
    CHECK(blockState(d, 0.7f, 1.0f) == HOT_DIM);
    CHECK(needleState(d) == LIT);
    d.value = 0.5f;   // in the zone: the zone alone
    CHECK(blockState(d, 0.0f, 0.4f) == DIM);
    CHECK(blockState(d, 0.4f, 0.7f) == ZONE_LIT);
    CHECK(blockState(d, 0.7f, 1.0f) == HOT_DIM);
    CHECK(needleState(d) == ZONE_LIT);
    d.value = 0.9f;   // past it: hot alone, the zone below goes dark
    CHECK(blockState(d, 0.4f, 0.7f) == ZONE_DIM);
    CHECK(blockState(d, 0.7f, 1.0f) == HOT_LIT);
    CHECK(needleState(d) == HOT_LIT);
    d.idle = true;    // nothing going on: all dim, the needle too
    CHECK(blockState(d, 0.7f, 1.0f) == HOT_DIM);
    CHECK(needleState(d) == DIM);
}

TEST_CASE("a centred band meter lights the band on the value's side only") {
    // The Slide pendulum: grip in the middle, hot and danger either side
    Data d;
    d.band = true;
    d.centered = true;
    d.zoneFrom = 0.0f;
    d.zoneTo = 0.1f;
    d.hotFrom = 0.33f;
    d.dangerFrom = 0.6f;
    d.value = -0.5f;   // hot, on the left
    CHECK(blockState(d, 0.2f, 0.3f) == HOT_LIT);     // |c| 0.5, left
    CHECK(blockState(d, 0.7f, 0.8f) == HOT_DIM);     // the same band on the right stays dark
    CHECK(blockState(d, 0.45f, 0.55f) == ZONE_DIM);
    CHECK(needleState(d) == HOT_LIT);
    d.value = 0.8f;    // danger, on the right
    CHECK(blockState(d, 0.9f, 1.0f) == DANGER_LIT);
    CHECK(blockState(d, 0.0f, 0.1f) == DANGER_DIM);
    CHECK(blockState(d, 0.7f, 0.8f) == HOT_DIM);
    CHECK(needleState(d) == DANGER_LIT);
    d.value = 0.02f;   // grip: the middle band spans both sides and lights whole
    CHECK(blockState(d, 0.45f, 0.5f) == ZONE_LIT);
    CHECK(blockState(d, 0.5f, 0.55f) == ZONE_LIT);
    CHECK(needleState(d) == ZONE_LIT);
}

TEST_CASE("a band meter's parts are its bands, mirrored when centred") {
    Data d;
    d.zoneFrom = 0.4f;
    d.zoneTo = 0.7f;
    d.hotFrom = 0.7f;   // the hot band starts where the zone ends: one cut
    bandParts(d);
    REQUIRE(d.parts == 3);
    CHECK(d.partEnd[0] == doctest::Approx(0.4f));
    CHECK(d.partEnd[1] == doctest::Approx(0.7f));
    CHECK(d.partEnd[2] == doctest::Approx(1.0f));
    Data c;
    c.centered = true;
    c.zoneFrom = 0.0f;   // from the middle: no cut there
    c.zoneTo = 0.1f;
    c.hotFrom = 0.5f;
    c.dangerFrom = 0.8f;
    bandParts(c);
    REQUIRE(c.parts == 7);
    const float want[7] = { 0.1f, 0.25f, 0.45f, 0.55f, 0.75f, 0.9f, 1.0f };
    for (int i = 0; i < 7; ++i) CHECK(c.partEnd[i] == doctest::Approx(want[i]));
}

TEST_CASE("a sided meter lights its left side bad and its right side good") {
    // The Gap deck: behind the reference to the left (red), ahead to the right
    Data d;
    d.centered = true;
    d.sided = true;
    d.value = 0.5f;
    CHECK(blockState(d, 0.5f, 0.6f) == GOOD);
    CHECK(blockState(d, 0.4f, 0.5f) == DIM);
    d.value = -0.5f;
    CHECK(blockState(d, 0.4f, 0.5f) == BAD);
    CHECK(blockState(d, 0.5f, 0.6f) == DIM);
    CHECK(blockState(d, 0.0f, 0.1f) == DIM);   // past the value: dark, not bad
}

TEST_CASE("a gap sits left when behind, right when ahead, full at the range") {
    CHECK(gapPosition(500, 1000) == doctest::Approx(-0.5f));    // 0.5 s behind: left
    CHECK(gapPosition(-250, 1000) == doctest::Approx(0.25f));   // ahead: right
    CHECK(gapPosition(3000, 1000) == doctest::Approx(-1.0f));   // past the range: held at the end
    CHECK(gapPosition(-3000, 1000) == doctest::Approx(1.0f));
    CHECK(gapPosition(400, 0) == 0.0f);                          // no range: the middle
}

TEST_CASE("a toned part is good or bad whatever the fill says") {
    Data d;
    d.value = 0.1f;
    CHECK(blockState(d, 0.5f, 0.6f, TONE_GOOD) == GOOD);
    CHECK(blockState(d, 0.0f, 0.05f, TONE_BAD) == BAD);
}

TEST_CASE("parts: where a position falls, and its tone") {
    Data d;
    evenParts(d, 4);
    REQUIRE(d.parts == 4);
    CHECK(d.partEnd[3] == doctest::Approx(1.0f));
    CHECK(partAt(d, 0.1f) == 0);
    CHECK(partAt(d, 0.3f) == 1);
    CHECK(partAt(d, 1.0f) == 3);
    d.partTone[1] = TONE_BAD;
    CHECK(toneAt(d, 0.3f) == TONE_BAD);
    CHECK(toneAt(d, 0.6f) == 0);
    // One lap, or more laps than blocks could show: one unbroken run
    evenParts(d, 1);
    CHECK(d.parts == 0);
    evenParts(d, MAX_PARTS + 1);
    CHECK(d.parts == 0);
    CHECK(toneAt(d, 0.5f) == 0);
}

TEST_CASE("race progress counts laps done and the way round this one") {
    CHECK(raceProgress(0, 0.0f, 10) == doctest::Approx(0.0f));
    CHECK(raceProgress(3, 0.5f, 10) == doctest::Approx(0.35f));
    CHECK(raceProgress(10, 0.2f, 10) == doctest::Approx(1.0f));   // past the flag: full
    CHECK(raceProgress(-1, 0.5f, 10) == doctest::Approx(0.05f));  // no lap yet
    CHECK(raceProgress(2, 0.5f, 0) == doctest::Approx(0.0f));     // not a lap race
}

// ---- Combo chips (DeckLayout::PILL_WORD) -------------------------------------
// The tricks were once one joined 32-byte field, filled oldest first: a long
// chain cut the NEWEST trick mid-word ("...|SUPERM"), the one the rider just
// did. Each chip now holds its own word: every trick's name must fit whole.

TEST_CASE("every trick's name fits a chip whole") {
    for (int i = 1; i < static_cast<int>(Fmx::TrickType::COUNT); ++i) {
        const char* name = Fmx::getTrickName(static_cast<Fmx::TrickType>(i));
        CAPTURE(name);
        CHECK(std::strlen(name) < static_cast<size_t>(DeckLayout::PILL_WORD));
    }
}

// The per-deck lists a layout author reads (Cabinet Dark's decks.ini: "; fields" and
// "; meters" over each [<deck>] section) are DeckLayout::kDeckFields and
// kDeckMeters word for word, both ways: a field the table gains and the list
// does not is one no author knows to use, and one the list names and the table
// lacks draws nothing and logs a warning.
TEST_CASE("Cabinet lists each deck's fields and meters exactly as the table has them") {
    std::ifstream in(std::string(THEMES_DIR) + "/cabinet-dark/decks.ini");
    REQUIRE(in.good());
    std::string line, list[2];   // 0 fields, 1 meters, as read since the last section
    int which = -1;
    bool seen[DeckLayout::DECK_COUNT] = {};
    // As sets: the list reads in the deck's own order, the table in the enum's
    const auto words = [](const std::string& s) {
        std::set<std::string> out;
        std::string w;
        std::istringstream ss(s);
        while (ss >> w) {
            if (!w.empty() && w.back() == ',') w.pop_back();
            if (w != "none") out.insert(w);
        }
        return out;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("; fields  ", 0) == 0) { which = 0; list[0] = line.substr(10) + " "; continue; }
        if (line.rfind("; meters  ", 0) == 0) { which = 1; list[1] = line.substr(10) + " "; continue; }
        // A wrapped fields line continues; the notes under meters are prose
        if (which == 0 && line.rfind(";         ", 0) == 0) { list[0] += line.substr(10) + " "; continue; }
        if (line.size() > 2 && line[0] == '[' && line.back() == ']' && line.find('-') == std::string::npos) {
            const std::string name = line.substr(1, line.size() - 2);
            for (int d = 0; d < DeckLayout::DECK_COUNT; ++d) {
                if (name != DeckLayout::kDeckNames[d]) continue;
                INFO("deck " << name);
                std::set<std::string> fields, meters;
                for (int f = 0; f < DeckLayout::FIELD_COUNT; ++f)
                    if (DeckLayout::deckFills(d, f)) fields.insert(DeckLayout::kFieldNames[f]);
                for (int m = 0; m < DeckLayout::METER_COUNT; ++m)
                    if (DeckLayout::deckHasMeter(d, m)) meters.insert(DeckLayout::kMeterNames[m]);
                CHECK(words(list[0]) == fields);
                CHECK(words(list[1]) == meters);
                seen[d] = true;
            }
            list[0].clear();
            list[1].clear();
        }
        if (line.empty() || line[0] != ';') which = -1;
    }
    for (int d = 0; d < DeckLayout::DECK_COUNT; ++d) CHECK_MESSAGE(seen[d], DeckLayout::kDeckNames[d]);
}

// deck.ini's units are the grid the drawer scales by (DeckHud::lx/ly divide
// by LayoutMetrics' default cell): a pack's whole cell must land on that grid,
// or a deck sized in whole cells stops snapping with the classic HUDs.
TEST_CASE("deck.ini's column, row and text size are LayoutMetrics' defaults at 1080p") {
    LayoutMetrics m;
    m.derive();
    CHECK(DeckLayout::COLUMN_PX == doctest::Approx(m.cellW * 1920.0f).epsilon(1e-5));
    CHECK(DeckLayout::ROW_PX == doctest::Approx(m.cellH * 1080.0f).epsilon(1e-5));
    CHECK(DeckLayout::TEXT_PX == doctest::Approx(m.fontSizeNormal * 1080.0f).epsilon(1e-5));
}

// A meter's gap is columns on a strip and degrees on an arc, and a skin may
// change the kind after the gap was read: finalize() settles it, every time.
TEST_CASE("a meter's gap is columns on a strip, degrees on an arc, decided at finalize") {
    DeckLayout::DeckGeometry g;
    DeckLayout::Deck& d = g.decks[0];
    d.meterCount = 2;
    d.meters[0].gapAsWritten = 2.0f;   // a bar: two columns
    d.meters[1].kind = DeckLayout::METER_ARC;
    d.meters[1].gapAsWritten = 2.0f;   // an arc: two degrees
    DeckLayout::finalize(g);
    CHECK(d.meters[0].gap == doctest::Approx(2.0f * DeckLayout::COLUMN_PX));
    CHECK(d.meters[1].gap == doctest::Approx(2.0f));
    d.meters[0].kind = DeckLayout::METER_ARC;   // a skin's kind = arc
    DeckLayout::finalize(g);
    CHECK(d.meters[0].gap == doctest::Approx(2.0f));
    DeckLayout::Deck& unset = g.decks[1];
    unset.meterCount = 1;
    DeckLayout::finalize(g);
    CHECK(unset.meters[0].gap == doctest::Approx(3.0f));   // unstated: the built-in
}

// EVERY LENGTH KEY'S UNIT, spelled out here a second time on purpose: the
// reader scales each key by DeckLayout::kIniUnits, so a key dropped from it,
// or given the wrong axis, reads as px again and nothing else fails (a
// needle, a well's pad and a stack's lean only show in a few deck states).
TEST_CASE("every deck.ini length key scales by its own unit, and nothing else does") {
    using DeckLayout::iniScale;
    const float C = DeckLayout::COLUMN_PX, R = DeckLayout::ROW_PX, T = DeckLayout::TEXT_PX;
    struct Want { const char* element; const char* prop; float units[4]; };
    const Want wants[] = {
        { "", "size", { C, R, C, R } }, { "", "row-pitch", { R, R, R, R } }, { "", "row-break", { R, R, R, R } },
        { "box", "rect", { C, R, C, R } }, { "box", "corners", { C, R, C, R } }, { "box", "skew", { C, C, C, C } },
        { "box", "pad", { C, C, C, C } }, { "box", "border", { R, R, R, R } }, { "box", "glow", { R, R, R, R } },
        { "text", "x", { C, C, C, C } }, { "text", "y", { R, R, R, R } }, { "text", "h", { R, R, R, R } },
        { "text", "size", { T, T, T, T } }, { "text", "fit", { C, C, C, C } },
        { "text", "pill-pad", { C, C, C, C } }, { "text", "pill-gap", { R, R, R, R } },
        { "text", "pill-h", { R, R, R, R } }, { "text", "pill-border", { R, R, R, R } },
        { "text", "stack-lean", { C / R, C / R, C / R, C / R } }, { "", "row-lean", { C / R, C / R, C / R, C / R } },
        { "meter", "rect", { C, R, C, R } }, { "meter", "arc", { C, R, R, R } }, { "meter", "skew", { C, C, C, C } },
        { "meter", "mid", { C, C, C, C } }, { "meter", "mark", { C, C, C, C } }, { "meter", "over", { R, R, R, R } },
        { "meter", "needle", { R, R, R, R } }, { "meter", "needle-in", { R, R, R, R } },
        { "meter", "line", { R, R, R, R } }, { "meter", "well-pad", { R, R, R, R } }, { "meter", "knob", { C, C, C, C } },
    };
    for (const Want& w : wants) {
        for (int i = 0; i < 4; ++i) {
            INFO(w.element << "." << w.prop << " number " << i);
            CHECK(iniScale(w.element, w.prop, i) == doctest::Approx(w.units[i]));
        }
    }
    CHECK(std::size(wants) == std::size(DeckLayout::kIniUnits));   // no length the list above misses
    // Not lengths: as written (a meter's gap waits for its kind; from is offset, not scaled)
    for (const char* prop : { "opacity", "count", "gap", "from", "sweep", "dim-opacity", "tail", "stack", "rows" }) {
        INFO(prop);
        CHECK(iniScale("meter", prop, 0) == 1.0f);
        CHECK(iniScale("text", prop, 0) == 1.0f);
        CHECK(iniScale("", prop, 0) == 1.0f);
    }
    CHECK(iniScale("text", "rect", 0) == 1.0f);   // a key is a length on its own element only
}

// THE BARS SWITCH'S SET, spelled out: each deck's bar along its foot goes with
// Settings > Decks > Bars, and every readout meter stays (Speed's revs, the
// gauges, the Inputs columns, the volume). A meter dropped from isBar, or added
// to it, changes what the switch hides on one deck only, which the integration
// test samples on two.
TEST_CASE("the Bars switch drops exactly each deck's foot bar") {
    using namespace DeckLayout;
    const Meter bars[] = { PROGRESS, SECTORS, CHAIN_TIME, GAP_BAR, RIVALS, TANK, TRACK_POS };
    for (int m = 0; m < METER_COUNT; ++m) {
        INFO(kMeterNames[m]);
        const bool want = std::find(std::begin(bars), std::end(bars), static_cast<Meter>(m)) != std::end(bars);
        CHECK(isBar(static_cast<Meter>(m)) == want);
    }
}
