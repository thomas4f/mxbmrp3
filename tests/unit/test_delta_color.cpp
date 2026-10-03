// ============================================================================
// tests/unit/test_delta_color.cpp
// The one colour rule for a signed time delta (deltaColorSlot in
// core/color_config.h), shared by the Gap Bar, Lap Log gap row, Ideal Lap and
// the Timing comparisons. Each HUD used to spell it out on its own, and they
// disagreed on an exactly-even gap (Ideal Lap green, Timing and Lap Log muted)
// until 1.31's consistency pass set it to PRIMARY: a real value, not a
// placeholder.
// ============================================================================
#include "doctest.h"
#include "core/color_config.h"

TEST_CASE("delta colour: ahead positive, behind negative, even primary") {
    CHECK(deltaColorSlot(-1) == ColorSlot::POSITIVE);
    CHECK(deltaColorSlot(-90000) == ColorSlot::POSITIVE);
    CHECK(deltaColorSlot(1) == ColorSlot::NEGATIVE);
    CHECK(deltaColorSlot(90000) == ColorSlot::NEGATIVE);
    CHECK(deltaColorSlot(0) == ColorSlot::PRIMARY);
    // Never the placeholder's colour: an exactly-even gap is still a value.
    CHECK(deltaColorSlot(0) != ColorSlot::MUTED);
}
