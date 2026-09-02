// ============================================================================
// tests/integration/tests/card_anchor_sweep_test.cpp
// EVERY HUD's content anchors to the box that owns it -- the card for body
// content, the title band for the caption -- never to the outer panel.
//
// WHY A SWEEP. The per-panel fixes (asym_border_test, the Fuel/Bars/Crash
// batch) each pinned one panel after a skinner's asymmetric [content] terms
// exposed it; the idiom that broke them (`startX + backgroundWidth / 2`,
// or a LEFT inset mirrored onto the right edge) is available to every HUD, so
// the guard has to be a property test over the whole registry, not a list of
// the panels caught so far.
//
// THE PROBE. Grow a RIGHT-ONLY margin. For a panel sized from its content the
// panel widens while the card (and title band) stay exactly where they were --
// so every drawn string and quad must keep its offset from the card rect
// (MXBMRP3_Test_HudCardRect), and anything tracking the panel's centre or its
// right edge moves and fails. For a panel whose width is pinned (a minPanelW:
// the center-stack trio, Settings) the same margin SHRINKS the card instead;
// there each string must keep its offset to at least one of the card's left
// edge, centre, or right edge -- the three legitimate horizontal anchors --
// and its y exactly. The two classes are told apart by whether the card's
// width changed, so no per-HUD list can go stale.
//
// Offsets are measured card-relative on BOTH draws, so the HUD's own screen
// offset (which strings carry and the pre-offset card rect does not) is a
// constant that cancels.
//
// Excluded: VERSION places a local copy of its plan, so the memoized rect the
// hook reads is never stamped with its origin (it has its own placement
// tests); MAP's ribbon quads are world geometry driven by track data this
// harness does not feed. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr double EPS = 1e-4;

struct Snapshot {
    PluginHost::QuadRect card;
    std::vector<PluginHost::StringRow> rows;
    std::vector<PluginHost::QuadRect> quads;
};

bool take(PluginHost& host, PluginHost::HudId id, Snapshot& out) {
    if (!host.hudCardRect(id, &out.card)) return false;
    out.rows = host.hudStringRows(id);
    out.quads = host.hudQuadRects(id);
    return true;
}

// One string keeps one of the three legitimate horizontal anchors.
bool keepsAnAnchor(const PluginHost::StringRow& was, const PluginHost::QuadRect& c0,
                   const PluginHost::StringRow& is, const PluginHost::QuadRect& c1) {
    const double m0 = (c0.l + c0.r) / 2.0, m1 = (c1.l + c1.r) / 2.0;
    return std::fabs((is.x - c1.l) - (was.x - c0.l)) < EPS
        || std::fabs((is.x - m1) - (was.x - m0)) < EPS
        || std::fabs((is.x - c1.r) - (was.x - c0.r)) < EPS;
}

// The panel's right edge: the frame's own extent, which its right-side chrome
// (edge/corner slices) legitimately tracks when a margin widens the panel.
double panelRight(const Snapshot& s) {
    double r = -1e9;
    for (const auto& q : s.quads) r = std::max(r, q.r);
    return r;
}

}  // namespace

TEST_CASE("right-only [content]/[title] margins: content keeps its own box") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\card_anchor\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasStringRows());

    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(6, 5, 0);
    host.addEntry(4, "Thomas");
    host.addEntry(7, "Rival");
    host.runInit(6);

    host.installTheme("anchor", /*inset=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*card=*/1);
    REQUIRE_MESSAGE(host.setThemeContentMargin(0.0f, 0.0f, 0.0f, 0.0f),
                    "no synthetic theme to inject margins into");
    REQUIRE(host.setThemeTitleMargin(0.0f, 0.0f, 0.0f, 0.0f));
    host.draw();

    struct Target { PluginHost::HudId id; const char* name; };
    const Target targets[] = {
        {PluginHost::HUD_STANDINGS, "standings"},
        {PluginHost::HUD_GFORCE, "gforce"},
        {PluginHost::HUD_TIMING, "timing"},
        {PluginHost::HUD_PERFORMANCE, "performance"},
        {PluginHost::HUD_SESSION_CHARTS, "session_charts"},
        {PluginHost::HUD_SETTINGS, "settings"},
        {PluginHost::HUD_GAPBAR, "gapbar"},
        {PluginHost::HUD_NOTICES, "notices"},
        {PluginHost::HUD_RECORDS, "records"},
        {PluginHost::HUD_LAP, "lap"},
        {PluginHost::HUD_POSITION, "position"},
        {PluginHost::HUD_SESSION, "session"},
        {PluginHost::HUD_RADAR, "radar"},
        {PluginHost::HUD_SPEED, "speed"},
        {PluginHost::HUD_GEAR, "gear"},
        {PluginHost::HUD_CRASH, "crash"},
        // VERSION anchors its row on the card box like the rest and was missing
        // from this list -- the "a new HUD joins the target list" convention
        // CLAUDE.md names, caught the hard way: it was also the panel whose
        // centre-stack width and anchor had drifted (see center_stack_theme_test).
        {PluginHost::HUD_VERSION, "version"},
        {PluginHost::HUD_BARS, "bars"},
        {PluginHost::HUD_GL_CONFIRM, "glconfirm"},
        {PluginHost::HUD_COMPASS, "compass"},
        {PluginHost::HUD_LEAN, "lean"},
    };

    // The Direct GL prompt builds nothing unless it is armed, and `present`
    // silently skips a HUD with no plan - so without this its row here would pass
    // by never being looked at, which is exactly the shape of green this suite
    // has been fooled by before.
    host.glConfirmArm(true);
    host.draw();

    std::vector<Snapshot> before(std::size(targets));
    std::vector<bool> present(std::size(targets), false);
    for (size_t i = 0; i < std::size(targets); ++i)
        present[i] = take(host, targets[i].id, before[i]);

    REQUIRE(host.setThemeContentMargin(0.0f, 8.0f, 0.0f, 0.0f));
    host.draw();

    int swept = 0;
    for (size_t i = 0; i < std::size(targets); ++i) {
        if (!present[i]) continue;
        Snapshot now;
        REQUIRE_MESSAGE(take(host, targets[i].id, now),
                        targets[i].name << " lost its plan under a margin");
        const Snapshot& was = before[i];
        INFO("hud " << std::string(targets[i].name));
        REQUIRE_MESSAGE(now.rows.size() == was.rows.size(),
                        targets[i].name << " changed its string count under a margin");
        ++swept;

        const bool cardResized =
            std::fabs((now.card.r - now.card.l) - (was.card.r - was.card.l)) > EPS;
        for (size_t s = 0; s < now.rows.size(); ++s) {
            INFO("string '" << now.rows[s].text << "'");
            CHECK(now.rows[s].text == was.rows[s].text);
            CHECK(std::fabs((now.rows[s].y - now.card.t) - (was.rows[s].y - was.card.t))
                  < EPS);
            if (!cardResized) {
                CHECK(std::fabs((now.rows[s].x - now.card.l) - (was.rows[s].x - was.card.l))
                      < EPS);
            } else {
                CHECK_MESSAGE(keepsAnAnchor(was.rows[s], was.card, now.rows[s], now.card),
                              "anchored to neither card edge nor card centre");
            }
        }
        // Quads only in the strict class: a resized card legitimately resizes
        // fills and row bands with it. A quad keeps its offset from the card --
        // or, for the frame's own right-side chrome (edge/corner slices), from
        // the panel's right edge, the one box that legitimately moved. Content
        // tracking the panel's CENTRE moves by half the widening and matches
        // neither anchor.
        if (!cardResized && now.quads.size() == was.quads.size()) {
            const double r0 = panelRight(was), r1 = panelRight(now);
            for (size_t q = 0; q < now.quads.size(); ++q) {
                INFO("quad " << q << " of " << now.quads.size());
                // The quad list is a fixed pool with zero-sized placeholders,
                // and widening the panel makes the frame emit EXTRA tiling
                // slices into slots that were empty before -- comparing a real
                // quad against a placeholder measures nothing.
                const auto zero = [](const PluginHost::QuadRect& r) {
                    return r.r - r.l < EPS || r.b - r.t < EPS;
                };
                if (zero(was.quads[q]) || zero(now.quads[q])) continue;
                const bool cardAnchored =
                    std::fabs((now.quads[q].l - now.card.l) - (was.quads[q].l - was.card.l))
                    < EPS;
                const bool frameAnchored =
                    std::fabs((now.quads[q].l - r1) - (was.quads[q].l - r0)) < EPS;
                const bool anchored = cardAnchored || frameAnchored;
                CHECK_MESSAGE(anchored,
                              "anchored to neither the card nor the frame's right edge");
                CHECK(std::fabs((now.quads[q].t - now.card.t) - (was.quads[q].t - was.card.t))
                      < EPS);
            }
        }
    }
    // The sweep is only a sweep if it actually visited the registry.
    CHECK_MESSAGE(swept >= 10, "only " << swept << " HUDs produced a plan to sweep");
    // Named explicitly, because it is the one target here that has to be armed to
    // exist at all: a silent skip would take it out of the sweep without taking
    // its row out of the list.
    for (size_t i = 0; i < std::size(targets); ++i)
        if (targets[i].id == PluginHost::HUD_GL_CONFIRM)
            CHECK_MESSAGE(present[i], "the Direct GL prompt produced no plan - it was "
                                      "listed as swept but never actually looked at");
    host.glConfirmArm(false);

    // Second perturbation, on its own so the two margins cannot cancel through
    // the band chrome: a right-only [title] margin moves the TITLE BAND's own
    // right edge and nothing else, so no drawn string may move at all --
    // captions are left-anchored in the band, and body content owes the title
    // terms nothing. (Quads are not asserted here: the band's right-side
    // slices legitimately follow the band edge.)
    REQUIRE(host.setThemeContentMargin(0.0f, 0.0f, 0.0f, 0.0f));
    host.draw();
    std::vector<Snapshot> mid(std::size(targets));
    for (size_t i = 0; i < std::size(targets); ++i)
        if (present[i]) present[i] = take(host, targets[i].id, mid[i]);

    REQUIRE(host.setThemeTitleMargin(0.0f, 8.0f, 0.0f, 0.0f));
    host.draw();
    for (size_t i = 0; i < std::size(targets); ++i) {
        if (!present[i]) continue;
        Snapshot now;
        REQUIRE(take(host, targets[i].id, now));
        INFO("hud " << std::string(targets[i].name) << " under a [title] margin");
        REQUIRE(now.rows.size() == mid[i].rows.size());
        // A panel whose CAPTION drives its width widens under a title margin,
        // and its card widens with it -- there centred content legitimately
        // follows the card's centre, so it gets the anchor test, same as the
        // pinned-width class above.
        const bool widened =
            std::fabs((now.card.r - now.card.l) - (mid[i].card.r - mid[i].card.l)) > EPS;
        for (size_t r = 0; r < now.rows.size(); ++r) {
            INFO("string '" << now.rows[r].text << "'");
            CHECK(std::fabs((now.rows[r].y - now.card.t) - (mid[i].rows[r].y - mid[i].card.t))
                  < EPS);
            if (!widened) {
                CHECK(std::fabs((now.rows[r].x - now.card.l) - (mid[i].rows[r].x - mid[i].card.l))
                      < EPS);
            } else {
                CHECK_MESSAGE(keepsAnAnchor(mid[i].rows[r], mid[i].card, now.rows[r], now.card),
                              "anchored to neither card edge nor card centre");
            }
        }
    }

    host.shutdown();
}

// ============================================================================
// The unthemed convention: every plan-based panel keeps the SAME side air --
// the panel padding -- between its edges and its card, and keeps it on both
// sides. The deliberate exception is contentFillsPanel (Gap Bar, Notices):
// their slab IS the reading, so it runs full-bleed -- zero side air, asserted
// as exactly zero rather than skipped, so a half-applied padding cannot hide
// in the exemption.
// ============================================================================
TEST_CASE("unthemed: every panel keeps the same side air around its card") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\card_air\\");
    REQUIRE(host.hasQuadRects());

    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(6, 5, 0);
    host.addEntry(4, "Thomas");
    host.runInit(6);
    host.draw();

    struct Target { PluginHost::HudId id; const char* name; bool fullBleed; };
    const Target targets[] = {
        {PluginHost::HUD_STANDINGS, "standings", false},
        {PluginHost::HUD_GFORCE, "gforce", false},
        {PluginHost::HUD_TIMING, "timing", false},
        {PluginHost::HUD_PERFORMANCE, "performance", false},
        {PluginHost::HUD_GAPBAR, "gapbar", true},
        {PluginHost::HUD_NOTICES, "notices", true},
        {PluginHost::HUD_RECORDS, "records", false},
        {PluginHost::HUD_SESSION, "session", false},
        {PluginHost::HUD_SPEED, "speed", false},
        {PluginHost::HUD_GEAR, "gear", false},
        {PluginHost::HUD_CRASH, "crash", false},
        {PluginHost::HUD_GL_CONFIRM, "glconfirm", false},
    };

    // Standings is the reference: the panel the report compared against. Card
    // and panel rects both come from the plan hook, so they share one space --
    // drawn quads carry the HUD offset and cannot be compared against either.
    PluginHost::QuadRect refCard, refPanel;
    REQUIRE(host.hudCardRect(PluginHost::HUD_STANDINGS, &refCard, &refPanel));
    const double refInset = refCard.l - refPanel.l;
    CHECK(refInset > 1e-4);   // there IS side air to compare against

    for (const auto& t : targets) {
        PluginHost::QuadRect card, panel;
        if (!host.hudCardRect(t.id, &card, &panel)) continue;
        const double insetL = card.l - panel.l;
        const double insetR = panel.r - card.r;
        INFO("hud " << std::string(t.name) << "  insetL " << insetL
             << "  insetR " << insetR << "  ref " << refInset);
        CHECK(std::fabs(insetL - insetR) < 1e-4);
        CHECK(std::fabs(insetL - (t.fullBleed ? 0.0 : refInset)) < 1e-4);
    }

    host.shutdown();
}
