// ============================================================================
// tests/unit/test_plate_geometry.cpp
// Unit tests for hud/plate_geometry.h — the standings race-number plate box and
// the nudge that centres the number inside it.
//
// THE BUG THIS PINS, reported from a screenshot: the number sat visibly high in
// its plate — 1px of grey above the digits and 5px below, on a 19px plate.
//
// The cause is not the font, which is the part worth keeping written down: a
// row's text is drawn with its glyph CELL top-aligned to the row origin, so the
// cell centre sits (lineHeight - fontSize)/2 above the row centre. Ordinary text
// hides this because descenders fill the space below, so a row highlight looks
// balanced. Digits have no descender and the generator centres the cap/digit band
// in the cell, so a race number occupies only that band and sits exactly that
// offset high. Measured across all six shipped fonts, the offset was identical to
// within 1.3 points — which is how the fonts were ruled out.
//
// So the offset is EXACTLY the row's slack over the font, halved, and that is
// what these cases pin. The plate box math is here too because StandingsHud's own
// PlateGeometry struct delegates to it: two copies of "0.8 * lineHeight" is how
// the box and the nudge drift apart.
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
    // change to kPlateHeightFrac must keep this true or the nudge below aims at
    // the wrong target.
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

TEST_CASE("number centering: the nudge is half the row's slack over the font") {
    // The measured case: a 19px plate in a ~23.75px row with a ~19.75px font left
    // the ink 2px high. Half the slack is exactly that 2px.
    CHECK(numberCenteringOffsetY(23.75f, 19.75f) == doctest::Approx(2.0f));
    CHECK(numberCenteringOffsetY(20.0f, 16.0f) == doctest::Approx(2.0f));
    CHECK(numberCenteringOffsetY(30.0f, 10.0f) == doctest::Approx(10.0f));
}

TEST_CASE("number centering: the nudge lands the digit band on the plate centre") {
    // The whole point, stated as the property rather than the formula. Drawing the
    // text cell at rowY puts its centre at rowY + fontSize/2; after the nudge that
    // centre must coincide with the plate's centre, which is the row's centre.
    for (float row : {12.0f, 20.0f, 23.75f, 64.0f}) {
        for (float font : {0.5f * row, 0.8f * row, 0.95f * row}) {
            const float cellCentreAfterNudge =
                numberCenteringOffsetY(row, font) + font * 0.5f;
            const float plateCentre = platePadY(row) + plateHeight(row) * 0.5f;
            CAPTURE(row);
            CAPTURE(font);
            CHECK(cellCentreAfterNudge == doctest::Approx(plateCentre));
        }
    }
}

TEST_CASE("number centering: no nudge when the font fills the row") {
    // Degenerate but reachable at small scales, and a negative nudge would push the
    // number UP out of the plate — worse than the bug being fixed.
    CHECK(numberCenteringOffsetY(20.0f, 20.0f) == doctest::Approx(0.0f));
    CHECK(numberCenteringOffsetY(20.0f, 24.0f) == doctest::Approx(0.0f));
    CHECK(numberCenteringOffsetY(0.0f, 0.0f) == doctest::Approx(0.0f));
}
