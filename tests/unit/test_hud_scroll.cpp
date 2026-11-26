// ============================================================================
// tests/unit/test_hud_scroll.cpp
// A HUD list's scrollback (mxbmrp3/hud/hud_scroll.h), the pure half:
//   - a live list follows its live edge (the end for oldest-first, the start
//     for newest-first) and a list that fits does not scroll
//   - moved away, it stays on the rows it shows while new ones arrive, at
//     either edge, and when the store drops its oldest rows to make room;
//     what arrived is counted, and reaching the edge again clears the count
//   - reset() goes back to the edge (the home of a static list)
//   - steps and pages stop at both ends
//   - the player's moves glide on Motion's clock, one row more drawn between
//     rows; arrivals at the live edge, reset() and Motion off land at once
// The input and drawing are the HUDs' integration test (hud_scroll_test.cpp).
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/hud/hud_scroll.h"

TEST_CASE("hud scroll: a live list follows its edge, and one that fits does not move") {
    HudScroll end(HudScroll::Edge::END);
    CHECK(end.layout(4, 6, 4) == 0);
    CHECK_FALSE(end.scrollable());
    CHECK(end.layout(10, 6, 6) == 4);   // oldest-first: the newest six
    CHECK(end.following());
    CHECK(end.unseen() == 0);

    HudScroll start(HudScroll::Edge::START);
    CHECK(start.layout(10, 6, 10) == 0);   // newest-first: the top
    CHECK(start.following());
}

TEST_CASE("hud scroll: moved away, the rows stay put while new ones arrive") {
    SUBCASE("oldest-first: new rows land below the view") {
        HudScroll s(HudScroll::Edge::END);
        s.layout(20, 6, 0);
        CHECK(s.step(-3));
        CHECK(s.first() == 11);
        CHECK_FALSE(s.following());
        CHECK(s.layout(22, 6, 2) == 11);
        CHECK(s.unseen() == 2);
    }
    SUBCASE("oldest-first, the store full: the oldest rows drop, the view holds its rows") {
        HudScroll s(HudScroll::Edge::END);
        s.layout(100, 6, 0);
        s.step(-10);
        CHECK(s.first() == 84);
        CHECK(s.layout(100, 6, 3) == 81);   // three in, three dropped from the start
        CHECK(s.unseen() == 3);
    }
    SUBCASE("newest-first: new rows land above the view") {
        HudScroll s(HudScroll::Edge::START);
        s.layout(20, 6, 0);
        s.step(4);
        CHECK(s.layout(23, 6, 3) == 7);
        CHECK(s.unseen() == 3);
    }
    SUBCASE("back at the edge, following again and the count cleared") {
        HudScroll s(HudScroll::Edge::END);
        s.layout(20, 6, 0);
        s.step(-2);
        s.layout(21, 6, 1);
        CHECK(s.unseen() == 1);
        CHECK(s.step(3));
        CHECK(s.following());
        CHECK(s.unseen() == 0);
        CHECK(s.layout(22, 6, 1) == 16);   // following the next one in
    }
}

TEST_CASE("hud scroll: reset goes back to the live edge, or a static list's home") {
    HudScroll live(HudScroll::Edge::END);
    live.layout(30, 5, 0);
    live.page(-1);
    live.layout(31, 5, 1);
    CHECK(live.reset());
    CHECK(live.first() == 26);
    CHECK(live.unseen() == 0);
    CHECK_FALSE(live.reset());   // nothing to do the second time

    HudScroll records;
    records.setHome(12);
    CHECK(records.layout(50, 8, 0) == 12);
    CHECK(records.atHome());
    records.step(5);
    CHECK_FALSE(records.atHome());
    CHECK(records.reset());
    CHECK(records.first() == 12);
    records.setHome(60);   // past the end: clamped to the last window
    CHECK(records.layout(50, 8, 0) == 42);
}

TEST_CASE("hud scroll: steps and pages stop at both ends") {
    HudScroll s(HudScroll::Edge::START);
    s.layout(12, 5, 0);
    CHECK_FALSE(s.step(-1));
    CHECK(s.page(1));
    CHECK(s.first() == 4);   // a view less the row kept for context
    CHECK(s.page(1));
    CHECK(s.first() == 7);   // the last window
    CHECK_FALSE(s.step(1));
    float y = 0.0f, h = 0.0f;
    s.thumb(10.0f, 1.0f, y, h);
    CHECK(h == doctest::Approx(10.0f * 5.0f / 12.0f));
    CHECK(y == doctest::Approx(10.0f - h));
}

// The moves poll() hands the shared bar core (ScrollView::press/follow): an
// arrow is one row, the track a view less one, and a dragged thumb rests on
// the NEAREST row. Fails if a HUD list's bar steps further, or truncates.
TEST_CASE("hud scroll: the bar's moves are one row an arrow, and the nearest row for a drag") {
    HudScroll s(HudScroll::Edge::NONE);
    s.layout(20, 5, 0);
    HudScroll::Moves m{ s };
    CHECK(m.step(1));
    CHECK(s.first() == 1);
    CHECK(m.step(-1));
    CHECK(s.first() == 0);
    CHECK(m.page(1));
    CHECK(s.first() == 4);
    CHECK(m.dragTo(7.6f));
    CHECK(s.first() == 8);
    CHECK(m.dragTo(7.4f));
    CHECK(s.first() == 7);
    CHECK(m.dragTo(99.0f));
    CHECK(s.first() == 15);
}

TEST_CASE("hud scroll: another rider's list starts over at its edge") {
    HudScroll s(HudScroll::Edge::END);
    s.layout(40, 5, 0);
    s.step(-8);
    s.restart();
    CHECK(s.layout(3, 5, 3) == 0);
    CHECK(s.following());
}

TEST_CASE("hud scroll: a step glides, one row more drawn on the way, and lands") {
    HudScroll s(HudScroll::Edge::START);
    s.setClock(1000000, Motion::Level::NORMAL);
    s.layout(20, 6, 0);
    CHECK(s.step(3));
    CHECK(s.first() == 3);   // the row it rests on is set at once
    CHECK(s.gliding());
    CHECK(s.layout(20, 6, 0) == 0);   // drawn from where it was
    CHECK(s.drawRows() == 6);         // still on a whole row

    s.setClock(1000000 + 110000, Motion::Level::NORMAL);   // half way
    CHECK(s.tick());
    CHECK(s.at() > 0.0f);
    CHECK(s.at() < 3.0f);
    const int from = s.layout(20, 6, 0);
    CHECK(from == static_cast<int>(s.at()));
    CHECK(s.drawRows() == 7);   // the row the view's bottom edge crosses
    CHECK(s.extraRows() == 1);  // ...which the HUD takes back after the list

    s.setClock(1000000 + 400000, Motion::Level::NORMAL);
    s.tick();
    CHECK_FALSE(s.gliding());
    CHECK(s.at() == doctest::Approx(3.0f));
    CHECK(s.layout(20, 6, 0) == 3);
    CHECK(s.drawRows() == 6);
    CHECK(s.extraRows() == 0);
}

TEST_CASE("hud scroll: what the player did not ask for lands") {
    SUBCASE("Motion off: a step lands") {
        HudScroll s(HudScroll::Edge::START);
        s.setClock(1000000, Motion::Level::OFF);
        s.layout(20, 6, 0);
        s.step(4);
        CHECK_FALSE(s.gliding());
        CHECK(s.layout(20, 6, 0) == 4);
    }
    SUBCASE("reset lands at the edge") {
        HudScroll s(HudScroll::Edge::END);
        s.setClock(1000000, Motion::Level::NORMAL);
        s.layout(20, 6, 0);
        s.page(-1);
        CHECK(s.gliding());
        s.reset();
        CHECK_FALSE(s.gliding());
        CHECK(s.layout(20, 6, 0) == 14);
    }
    SUBCASE("a row arriving while following lands on the new edge") {
        HudScroll s(HudScroll::Edge::END);
        s.setClock(1000000, Motion::Level::NORMAL);
        s.layout(20, 6, 0);
        s.step(-2);
        s.setClock(1000000 + 400000, Motion::Level::NORMAL);
        s.tick();
        s.step(2);   // back at the edge, gliding there
        s.setClock(1000000 + 510000, Motion::Level::NORMAL);
        s.tick();
        CHECK(s.following());
        CHECK(s.gliding());
        CHECK(s.layout(20, 6, 0) < 14);   // nothing in: the glide goes on
        CHECK(s.gliding());
        CHECK(s.layout(21, 6, 1) == 15);   // a row in: it lands
        CHECK_FALSE(s.gliding());
    }
    SUBCASE("moved away, rows arriving carry the glide with the rows it draws") {
        HudScroll s(HudScroll::Edge::START);
        s.setClock(1000000, Motion::Level::NORMAL);
        s.layout(20, 6, 0);
        s.step(4);
        s.setClock(1000000 + 110000, Motion::Level::NORMAL);
        s.tick();
        const float at = s.at();
        s.layout(22, 6, 2);   // newest-first: two land above the view
        CHECK(s.first() == 6);
        CHECK(s.gliding());
        CHECK(s.at() == doctest::Approx(at + 2.0f));
    }
}
