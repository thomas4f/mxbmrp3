// ============================================================================
// tests/unit/test_plate_geometry.cpp
// Unit tests for hud/plate_geometry.h — the standings race-number plate box.
//
// ONE PROPERTY, and everything about where the number lands rests on it: the plate
// is CENTRED in its row. Glyphs are row-centred by BaseHud::rowCenterOffset(), so a
// centred plate is what makes the number land centred on it with no plate-local
// correction — and a plate-local correction is exactly what used to live here and
// then double-counted once the glyph centring became global. Change
// kPlateHeightFrac freely; break the centring and the number goes off the box.
//
// Where the number ACTUALLY lands is not knowable from this header — that needs the
// real render, and is pinned by the centring case in
// tests/integration/tests/standings_layout_test.cpp.
// ============================================================================
#include "doctest.h"

#include "hud/plate_geometry.h"

#include <initializer_list>

using namespace PlateLayout;

TEST_CASE("plate box: inset equally top and bottom inside the row") {
    const float row = 20.0f;
    CHECK(plateHeight(row) == doctest::Approx(16.0f));      // 0.8 * row
    CHECK(platePadY(row) == doctest::Approx(2.0f));         // (20 - 16) / 2

    // The defining property, rather than the two constants above: the plate is
    // CENTRED in the row, so its centre and the row's centre coincide. A future
    // change to kPlateHeightFrac must keep this true or the row-centred number no
    // longer lands on the box.
    const float plateCentre = platePadY(row) + plateHeight(row) * 0.5f;
    CHECK(plateCentre == doctest::Approx(row * 0.5f));
}

TEST_CASE("plate box: centred at any row height") {
    for (float row : {8.0f, 13.5f, 20.0f, 47.25f, 120.0f}) {
        const float centre = platePadY(row) + plateHeight(row) * 0.5f;
        CAPTURE(row);
        CHECK(centre == doctest::Approx(row * 0.5f));
    }
}

