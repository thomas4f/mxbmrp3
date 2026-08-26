// ============================================================================
// tests/integration/tests/title_band_test.cpp
// A CAPTION WITH NO BAND STILL RESERVES ITS ROW, and the body card starts under it.
//
// THE BUG, found by rendering the theme's [card] switches against each other rather
// than by reading: with `hud-title-band = 0` and `hud-content = 1` -- a theme that
// draws body cards but no title bands -- the caption and the panel's own reading
// collided, drawn on the same line. Reproduced on all three centre-stack panels.
//
// One cause for all three. The band emitter returns 0 both when there is NO caption and
// when there is one the theme draws no band for, and those are not the same panel: the
// second still advances by reservedTitleHeight(). Treating them alike put the body
// card's top at the panel's own clearance -- ON the caption -- and every one of these
// panels then re-anchors its content to that card:
//
//     GapBarHud   startY     = cardTop            (its "the bar IS the card" override)
//     NoticesHud  noticeQuadY = cardTop
//     TimingHud   cardTop    = hasThemedContentCard() ? sectionCardTop() : y
//
// so the reading was dragged back up onto its own label. It needs BOTH switches to
// show, which is why neither shipped theme's defaults expose it and why it survived
// until someone mixed them.
//
// ASSERTED AS A GAP BETWEEN TWO STRINGS, not as a card coordinate: the fault the user
// sees is two pieces of text on one line, and a card rect that looks right is worth
// nothing if the content still lands on the caption. MXBMRP3_Test_HudStringRows gives
// the drawn rows, so this asks the question the screenshot asked.
//
// RESTORED after the box-model port (removed in 8bf6610).
// Two adaptations, both stated: the band-border hook was renamed
// (setThemeBandBorder -> setThemeTitleBorder, the key now spelled [card] title
// border), and the band-vs-bandless caption delta is asserted as CONSTANT ACROSS
// FRAME SIZES rather than against the old model's derived -0.013911 -- the constancy
// was always the property (the magic number was one model's value of it), and the
// plan model owns that arithmetic now.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <utility>
#include <cmath>

namespace {

// A box term and ONE side of it to sweep, in CSS shorthand (top right bottom
// left). Shared by both caption cases; see the settings one for why a uniform
// value is not enough.
//
// `movesCaption` is the side's own expectation and is asserted BOTH ways: a TOP
// term pushes the caption down, a BOTTOM term must leave it exactly where it is
// while still moving what comes after. Asking only "did something move" cannot
// tell a term spent on the correct side from one spent on the other, which is
// the whole reason the sides are swept apart.
//
// SIX CELLS, not three. A box term is stated in x-cells and drawn square, so it
// converts to y at 5/6 of a cell each; Map's panel ceils to whole cells, so an
// odd count leaves a half-cell of ceil remainder in the measure and the
// assertion flaps on quantisation rather than on geometry. 6 x-cells is exactly
// 5 cell-heights and lands on the lattice.
struct Term {
    PluginHost::BoxTermId id; const char* name; const char* value; bool movesCaption;
};

constexpr float FRAME_INSET = 2.0f;
constexpr float CARD_INSET  = 1.0f;

}  // namespace

TEST_CASE("a bandless caption still clears the content below it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_band\\");
    REQUIRE_MESSAGE(host.hasThemeGeometry(),
                    "MXBMRP3_Test_InstallTheme not exported (test build?)");
    REQUIRE_MESSAGE(host.hasStringRows(),
                    "MXBMRP3_Test_HudStringRows not exported (test build?)");
    host.showAllHuds(true);

    // A scene the three panels have something to say in: the DEFAULT SETUP notice is
    // persistent (not timed), and a lap gives Timing a time to show.
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 1, .gap = 0 } });
    host.raceLap(1, 4, 0, 108231, /*best=*/1, /*split0=*/35900, /*split1=*/72400);

    struct Panel { PluginHost::HudId id; const char* name; const char* caption; };
    const Panel panels[] = {
        { PluginHost::HUD_GAPBAR,  "gap_bar_hud",  "Gap Bar" },
        { PluginHost::HUD_NOTICES, "notices_hud", "Notices" },
        { PluginHost::HUD_TIMING,  "timing_hud",  "Timing"  },
    };

    // All four combinations of the theme's two [card] switches for HUD panels, with the
    // caption ON throughout -- with it off there is no row to clear and nothing here is
    // interesting. Keyed [band][content][panel].
    double gap[2][2][3];

    for (int band = 1; band >= 0; --band) {
        for (int content = 1; content >= 0; --content) {
            const std::string themeName =
                "tb" + std::to_string(band) + std::to_string(content);
            host.installTheme(themeName.c_str(), FRAME_INSET, CARD_INSET,
                              /*titleBand=*/band, /*card=*/content);
            for (const Panel& p : panels) REQUIRE(host.setHudTitle(p.name, true));
            host.runInit(1);      // re-raise the notice, which times out
            host.draw();

            for (int i = 0; i < 3; ++i) {
                const Panel& p = panels[i];
                const std::vector<PluginHost::StringRow> rows = host.hudStringRows(p.id);
                // The caption, and the topmost string that is NOT the caption -- the
                // panel's first line of content, whatever it happens to say.
                double captionY = -1.0, firstContentY = 1e9;
                for (const auto& r : rows) {
                    if (r.text.empty()) continue;
                    if (r.text == p.caption) { captionY = r.y; continue; }
                    firstContentY = (std::min)(firstContentY, r.y);
                }
                INFO("band=" << band << " content=" << content << " "
                     << std::string(p.name) << ": caption y " << captionY
                     << ", first content y " << firstContentY);
                REQUIRE_MESSAGE(captionY >= 0.0, "caption not drawn -- is the title on?");
                REQUIRE_MESSAGE(firstContentY < 1e8, "panel drew no content to collide with");
                gap[band][content][i] = firstContentY - captionY;
                CHECK(gap[band][content][i] > 0.0);   // free, and catches a total inversion
            }
        }
    }

    // THE BODY CARD MUST NEVER PULL CONTENT UP TOWARD THE CAPTION.
    //
    // This is the assertion, and getting here took two wrong ones. "Content strictly
    // below the caption" passed against the bug -- the content was only a third of a row
    // down, overlapping the glyphs but numerically lower. An absolute floor in line
    // heights would work but hard-codes a metric this test has no business knowing.
    //
    // The card-OFF panel is the reference: with no card there is nothing to re-anchor
    // content to, so its caption-to-content distance is correct by construction. Turning
    // the card on may push content DOWN (it adds its own border to the padding) and must
    // never pull it up. One flag apart, so nothing else moves between the two readings.
    //
    // THE TOLERANCE IS MEASURED, not chosen by feel. Timing's banded case legitimately
    // gives up 0.00076 -- its sectionCardTop() starts a hair above where its bare content
    // would -- which is under a pixel at 1080p. The bug gave up 0.0118, half a row. 0.002
    // sits an order of magnitude clear of the first and six times inside the second.
    //
    // Measured at the shipped metrics, where a row is 0.0235: before the fix, band=0 went
    // 0.0257 -> 0.0139; after, 0.0257 -> 0.0335.
    // RECALIBRATED, and the reason is a real change rather than a loosening. Carded
    // content now anchors to the CARD'S INTERIOR (panelContentY) while uncarded content
    // anchors to paddingV, so the two legitimately differ: measured, Timing gives up
    // 0.00347 -- about a third of a cell -- against 3.9 cells of remaining clearance
    // between caption and content. The bug this guards gave up 0.0118, half a row, WITH
    // the glyphs overlapping. 0.004 sits clear of the first and three times inside the
    // second. It was 0.002 while both sides came from the same padding chain.
    constexpr double SLACK = 0.004;
    for (int band = 1; band >= 0; --band) {
        for (int i = 0; i < 3; ++i) {
            INFO("band=" << band << " " << std::string(panels[i].name)
                 << ": caption-to-content gap " << gap[band][0][i] << " without the card, "
                 << gap[band][1][i] << " with it");
            CHECK(gap[band][1][i] >= gap[band][0][i] - SLACK);
        }
    }

    // AND SWITCHING THE BAND OFF MAKES THE PANEL COMPACT WITHOUT MOVING THE CAPTION.
    //
    // The rule, in the reporter's words: "disabling an attribute should not shift the
    // title around, just become more compact." The body card already behaved that way,
    // which is what made the band's behaviour look wrong beside it.
    //
    // TWO faults were behind it and the first hid the second. The caption was placed
    // from the HUD's CONTENT origin, which pays contentPaddingY() -- the body card's
    // clearance -- so it dropped half a row whenever a card was present. And that origin
    // is a constant that knows nothing about the FRAME, so once the drop was gone the
    // caption still ignored the frame's thickness: measured at frame sizes 1, 2 and 4,
    // the caption sat at 0.0252 every time while a banded one moved 0.0157 / 0.0254 /
    // 0.0450. It looked correct at frame = 2 only because that is where the two cross --
    // which is the shipped value, and why the first fix looked complete.
    //
    // SWEPT OVER FRAME SIZE, because a single size cannot tell "tracks the frame" from
    // "happens to agree here". The band-to-bandless difference must be the SAME at every
    // frame size; that it is NEGATIVE is the compactness (a bare row is shorter than a
    // band, so its glyph centres higher) and it is exactly half that height difference.
    //
    // TOLERANCE IS ABSOLUTE, not relative: the quantity is a DIFFERENCE of two screen
    // coordinates and is legitimately near zero, so a proportional epsilon would tighten
    // to nothing as it shrank. 1e-4 of screen height is a fifth of a pixel at 1080p and
    // is two orders of magnitude inside the smallest deviation the bug produced (0.0002
    // at frame = 2, where the two curves cross; 0.0096 and 0.0198 either side of it).
    //
    // ASSERTED AS CONSTANCY, not against a derived constant. The removed version
    // pinned -0.013911 with its derivation spelled out from the old padding chain
    // (0.004133 + cardPadY); the plan model computes the same geometry its own way
    // and re-deriving the number here would just restate planPanel. What the bug
    // broke -- and what one frame size cannot see -- is that the delta is the SAME
    // at every frame size; before the caption fix it read +0.0096 / -0.0002 /
    // -0.0198 across this sweep. Negativity is the compactness (a bare row is
    // shorter than a band, so its glyph centres higher) and is kept as a sign check.
    constexpr double DELTA_SLACK = 1e-4;
    double firstDelta = 0.0;
    bool haveFirst = false;
    for (float frame : { 1.0f, 2.0f, 4.0f }) {
        double dy[2] = { 0.0, 0.0 };
        for (int band = 1; band >= 0; --band) {
            const std::string themeName =
                "cm" + std::to_string(static_cast<int>(frame)) + std::to_string(band);
            host.installTheme(themeName.c_str(), frame, CARD_INSET,
                              /*titleBand=*/band, /*card=*/1);
            REQUIRE(host.setHudTitle("timing_hud", true));
            host.draw();
            double captionY = -1.0;
            for (const auto& r : host.hudStringRows(PluginHost::HUD_TIMING)) {
                if (r.text == "Timing") { captionY = r.y; break; }
            }
            REQUIRE(captionY >= 0.0);
            dy[band] = captionY - host.hudScreenEdges(PluginHost::HUD_TIMING).t / 1e6;
        }
        const double delta = dy[0] - dy[1];
        INFO("frame=" << frame << ": caption sits " << dy[1] << " below the panel top with"
             << " a band, " << dy[0] << " without (delta " << delta << ")");
        // Compact, not lifted: without its band the caption's row is shorter, so it
        // sits HIGHER, never lower.
        CHECK(delta < 0.0);
        // ...and by the same amount at every frame size, which is what "tracks the
        // frame" means and is the half a single size cannot see.
        if (!haveFirst) { firstDelta = delta; haveFirst = true; }
        else CHECK(std::abs(delta - firstDelta) < DELTA_SLACK);
    }

    for (const Panel& p : panels) host.setHudTitle(p.name, false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// THE SETTINGS PANEL IS TALL ENOUGH FOR THE BAND IT DRAWS.
//
// THE BUG: the band's height had THREE spellings -- the band emitter drew it,
// titleRowHeight reserved a HUD's row for it, and SettingsHud::titleAdvance reserved
// the settings panel's -- and when the band became a border box only the first two were
// updated. The third went on reserving (padding + glyph + padding) for a band that now
// draws (border + padding + glyph + padding + border).
//
// WHERE THAT LANDS is what makes it hard to see, and is why the first version of this
// case passed against the bug. The panel does not MISPLACE anything: it lays its cards
// out from the DRAWN band's bottom edge (rebuildRenderData takes the caption row's return
// value), so every gap inside it stayed correct and every screenshot looked right. Only
// the panel's own HEIGHT comes from titleAdvance -- so the whole column of content was
// pushed 2 * border further down inside a background that had not grown, and the last
// section card ran out through the bottom edge. 0.0196 of overflow at [card] size 1,
// 0.0587 at size 3, against a bottom padding of 0.0234: crowded, then out.
//
// SO IT IS ASSERTED AT THE BOTTOM, as the clearance between the lowest card and the
// panel's own bottom edge. Swept over [card] size because a slice size has nothing to
// do with that clearance -- it must be the SAME number at every size. At any single
// size the wrong value looks plausible, and at the two sizes the shipped themes use it
// is merely tight rather than negative, which is exactly how it shipped.
TEST_CASE("the settings panel reserves the height of the band it draws") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_band_advance\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE_MESSAGE(host.hasFillCut(),
                    "MXBMRP3_Test_HudFillCut not exported (test build?)");
    REQUIRE(host.hasScreenEdges());
    host.showAllHuds(true);
    host.showSettings(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    // THE TALLEST TAB'S clearance, per size: the panel is floored at the
    // tallest tab's body (Spec::minBodyH -- Save and Close hold still while you
    // flick tabs), so on any SHORTER tab the air below its last card includes
    // the shortfall, which varies with content BY DESIGN. On the tallest tab
    // the floor binds nothing and the clearance is the bottom furniture
    // exactly -- and WHICH tab is tallest changes with the card size (a tab
    // with more sections grows faster per size), so the tallest is found by
    // taking the MINIMUM clearance over every tab rather than trusting one.
    // The single-tab sweep this replaces held only while General happened to
    // be the tallest at every swept size -- a coincidence of the shipped
    // metrics that broke the moment uiLineHeight moved.
    // EVERY selectable tab, not the sidebar list. About is hidden from the list
    // (TabDescriptor::hidden) but its content still counts toward the panel's
    // height, so a sweep over the LIST stops finding the tallest tab the moment
    // About is it -- which is exactly what happened: at [card] size 2 and 3 the
    // panel floor was set by a tab this loop never visited, and every tab it did
    // visit reported the shortfall as extra clearance (0.0904 against 0.0870).
    const std::vector<std::string> tabs = host.settingsAllTabNames();
    REQUIRE_MESSAGE(tabs.size() > 3u, "tab enumeration hook returned no tabs");
    double firstClear = -1.0;
    for (float card : { 1.0f, 2.0f, 3.0f, 4.0f }) {
        const std::string nm = "ta" + std::to_string(static_cast<int>(card));
        host.installTheme(nm.c_str(), FRAME_INSET, card, /*titleBand=*/1, /*card=*/1);

        double minClear = 1e9;
        for (const std::string& tab : tabs) {
            host.setActiveTab(tab.c_str());
            host.draw();

            const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_SETTINGS);
            REQUIRE_MESSAGE(cut.themed, "settings panel drew no themed centre");
            REQUIRE_MESSAGE(cut.covers.size() >= 2u,
                            "expected the band and at least one section card");
            // The band is the first cover finalizeThemedFill records (see its cover
            // list); everything after it is a section card. Asserted rather than
            // assumed, so a reshuffled cover list fails here instead of quietly
            // measuring the band.
            REQUIRE_MESSAGE(cut.covers.front().t < cut.covers.back().t,
                            "cover[0] is not the topmost rect -- not the band?");
            double lowestCard = -1e9;
            for (size_t i = 1; i < cut.covers.size(); ++i)
                lowestCard = (std::max)(lowestCard, cut.covers[i].b);

            const double panelBottom =
                host.hudScreenEdges(PluginHost::HUD_SETTINGS).b / 1e6;
            const double clear = panelBottom - lowestCard;
            INFO("[card] size " << card << ", tab " << tab
                 << ": lowest card ends at " << lowestCard << ", panel bottom "
                 << panelBottom << " (clearance " << clear << ")");
            // POSITIVE is the fault a user eventually sees -- content out through
            // the bottom edge -- and holds on every tab, floor or no floor.
            CHECK(clear > 0.0);
            minClear = (std::min)(minClear, clear);
        }

        INFO("[card] size " << card << ": tallest-tab clearance " << minClear);
        // CONSTANT is the real assertion: on the tallest tab the clearance IS the
        // panel's bottom furniture, and a slice size is not part of it. Measured,
        // before the border fix: 0.0860, 0.0665, 0.0469, 0.0274 -- one cell of
        // border gone per size, on its way to negative. 1e-4 of screen height is
        // a fifth of a pixel at 1080p.
        if (firstClear < 0.0) firstClear = minClear;
        else CHECK(std::abs(minClear - firstClear) < 1e-4);
    }
    host.setActiveTab("General");

    host.showSettings(false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// `[card] band-size` SIZES THE BAND WITHOUT TOUCHING THE BODY CARD.
//
// The two were one number: the band and the body card are drawn from the same nine
// card sprites, so `[card] size` scaled both corners and a theme could not have a
// heavy header over a light body. band-size parts the SCALES, not the art.
//
// FOUR THINGS HAVE TO MOVE TOGETHER for that to be a feature rather than a way to
// break a header, and this drives all four:
//
//   1. THE BAND'S HEIGHT follows band-size (it is a border box), so raising the key
//      makes the band taller by exactly twice the change.
//   2. THE BODY CARD DOES NOT MOVE. This is the whole point of the key, and it is the
//      thing a shared helper would silently break.
//   3. THE PANEL GROWS WITH THE BAND. titleRowHeight and titleAdvance reserve the box
//      titleBandBoxHeight owns, so a taller band makes a taller panel rather than
//      running into the content -- the single-owner fix in the previous commit is what
//      this depends on.
//   4. EACH COLUMN CLEARS ITS OWN BOX. Clause 4 read "the content column clears the
//      LARGER of the two borders" when one shared column served the caption and the
//      rows -- the max() model. The plan dismantled that on purpose: panel_box.h
//      gives the caption its own column (captionX = inner + insetL(title)) and the
//      rows theirs (rowsX = inner + insetL(content)), so a caption cannot sit on its
//      band's edge BY CONSTRUCTION and a thick band no longer pushes the rows around.
//      What is asserted now is that separation: the rows' offset from the panel edge
//      tracks the CARD alone (band swept, rows still), and the caption's clearance
//      from its band's edge tracks the BAND alone (monotone in band-size). A thin
//      band beside a thick card may legitimately place the caption LEFT of the rows
//      -- under the max() model that was the bug; under per-box columns it is each
//      box charging its own terms.
//
// AND THE DEFAULT IS "FOLLOW `size`", asserted first: a theme that never names the key
// must render byte-identically to before it existed, and must keep following `size`
// when a skinner edits that alone (which a copied default rather than the -1 sentinel
// would silently stop doing).
TEST_CASE("[card] band-size scales the band alone") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\band_size\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE_MESSAGE(host.hasTitleBorder(),
                    "MXBMRP3_Test_SetThemeTitleBorder not exported (test build?)");
    REQUIRE(host.hasFillCut());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 1, .gap = 0 } });
    REQUIRE(host.setHudTitle("timing_hud", true));

    // The band and the body card, read off the covers finalizeThemedFill cut against:
    // cover[0] is the band, cover[1] the whole-body card (see the cover list). Also the
    // panel's own height, so the reservation can be checked with them.
    //
    // TWO COLUMNS, read separately, because the plan gives each its own: capIn is the
    // caption's clearance from ITS band's left edge (the quantity that must track the
    // band), rowsIn the rows' clearance from the PANEL's left edge (the quantity that
    // must track the card and nothing else). The old single contentL -- min over all
    // strings -- conflated them, and Timing is screen-centred besides, so growth on
    // both sides moved the panel edge under the measurement.
    struct Shape { double bandH, cardTop, cardH, panelH, capIn, rowsIn; };
    auto measure = [&](float cardSize, float bandBorder) {
        const std::string nm = "bs" + std::to_string(static_cast<int>(cardSize))
                             + std::to_string(static_cast<int>(bandBorder + 2.0f));
        host.installTheme(nm.c_str(), FRAME_INSET, cardSize, /*titleBand=*/1, /*card=*/1);
        REQUIRE(host.setThemeTitleBorder(bandBorder));
        host.draw();
        const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_TIMING);
        REQUIRE_MESSAGE(cut.themed, "Timing drew no themed centre");
        REQUIRE_MESSAGE(cut.covers.size() >= 2u, "expected a band and a body card");
        const PluginHost::QuadRect band = cut.covers[0], card = cut.covers[1];
        REQUIRE_MESSAGE(band.t < card.t, "cover order is not band-then-card");
        const PluginHost::ScreenEdges e = host.hudScreenEdges(PluginHost::HUD_TIMING);
        double capL = -1.0, rowsL = 1e9;
        for (const auto& r : host.hudStringRows(PluginHost::HUD_TIMING)) {
            if (r.text.empty()) continue;
            if (r.text == "Timing") { capL = r.x; continue; }
            rowsL = (std::min)(rowsL, r.x);
        }
        REQUIRE_MESSAGE(capL >= 0.0, "caption not drawn");
        REQUIRE_MESSAGE(rowsL < 1e8, "no content rows drawn");
        return Shape{ band.b - band.t, card.t, card.b - card.t,
                      (e.b - e.t) / 1e6, capL - band.l, rowsL - e.l / 1e6 };
    };

    // THE DEFAULT. Unset (-1) must be indistinguishable from naming the card's own
    // size, at two different card sizes so "follows `size`" is told apart from
    // "happens to equal 1".
    for (float cardSize : { 1.0f, 3.0f }) {
        const Shape unset  = measure(cardSize, -1.0f);
        const Shape spelled = measure(cardSize, cardSize);
        INFO("[card] size " << cardSize << ": band " << unset.bandH
             << " unset against " << spelled.bandH << " spelled");
        CHECK(std::abs(unset.bandH  - spelled.bandH)  < 1e-6);
        CHECK(std::abs(unset.panelH - spelled.panelH) < 1e-6);
        CHECK(std::abs(unset.cardTop - spelled.cardTop) < 1e-6);
    }

    // THE SPLIT. Card held at 1, band swept 1 -> 4.
    const Shape base = measure(1.0f, 1.0f);
    double prevBandH = base.bandH, prevPanelH = base.panelH, prevCapIn = base.capIn;
    for (float bandBorder : { 2.0f, 3.0f, 4.0f }) {
        const Shape s = measure(1.0f, bandBorder);
        INFO("band-size " << bandBorder << ": band " << s.bandH << " (was " << prevBandH
             << "), card " << s.cardH << " at " << s.cardTop
             << ", panel " << s.panelH << " (was " << prevPanelH << ")");

        // 1. The band grows. One cell of border per size, twice over -- but asserted as
        //    "strictly taller than the step before" rather than against a derived
        //    number, so this cannot pass by agreeing with the formula it is checking.
        CHECK(s.bandH > prevBandH);
        // 2. The body card's HEIGHT is untouched: it is the panel's interior below the
        //    band, so it moves DOWN with a taller band and keeps its own thickness. Its
        //    top tracks the band's bottom exactly (contentCardTop is flush).
        CHECK(std::abs(s.cardH - base.cardH) < 1e-4);
        // 3. The panel grows with the band, by the same amount, so the content is not
        //    squeezed. Both are ceiled to a cell, hence the cell-sized tolerance rather
        //    than an exact equality.
        CHECK(s.panelH > prevPanelH);
        CHECK((s.panelH - base.panelH) >= (s.bandH - base.bandH) - 1e-4);
        // 4a. The caption clears its own thicker band: its inset from the band's left
        //     edge grows with every band-size step. The old model asserted this via a
        //     shared column; the plan makes it the title box's own insetL.
        CHECK(s.capIn > prevCapIn);
        // 4b. ...and the ROWS do not move: their offset from the panel's edge is the
        //     content box's alone, so a band sweep leaves it exactly where it was.
        //     Tolerance is quantisation only (strings and edges are both x1e6).
        CHECK(std::abs(s.rowsIn - base.rowsIn) < 5e-6);

        prevBandH = s.bandH; prevPanelH = s.panelH; prevCapIn = s.capIn;
    }

    // AND THE OTHER WAY: a band THINNER than the card leaves the rows exactly where
    // the card puts them -- the card owns the rows' column, whatever the band does.
    // The caption's clearance shrinks with its band (per-box columns), which under
    // the old shared-column model would have been the bug and is now the design.
    {
        const Shape thickCard = measure(3.0f, 3.0f);
        const Shape thinBand  = measure(3.0f, 1.0f);
        INFO("card 3 / band 1: rows inset " << thinBand.rowsIn
             << " against " << thickCard.rowsIn << " with both at 3; caption inset "
             << thinBand.capIn << " against " << thickCard.capIn);
        CHECK(std::abs(thinBand.rowsIn - thickCard.rowsIn) < 5e-6);
        CHECK(thinBand.capIn < thickCard.capIn);   // the caption follows ITS band
        CHECK(thinBand.bandH < thickCard.bandH);   // the band did shrink
    }

    host.setHudTitle("timing_hud", false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// AND THE SETTINGS PANEL'S CAPTION OBEYS THE SAME RULE.
//
// It is the one panel that builds its caption itself -- centred
// and carries no identity icon, so SettingsHud builds it itself -- which is exactly why
// it needs its own case. The fix above moved every HUD's bandless caption onto the
// frame's inner boundary and left this one at `startY + panelPadV`, a content origin
// that knows nothing about the frame. Nothing failed: the sweep in
// theme_panel_padding_test.cpp measures padding rather than captions, and the case above
// drives HUDs. A theme setting `settings-title-band = 0` (or shipping no card slices at
// all) is all it takes to see it.
//
// Both callers now ask BaseHud::emitCaptionRow. This asserts the property that makes
// them one implementation rather than two that agree today: the settings caption sits
// the SAME distance below its panel top as a HUD's does, at every frame size.
TEST_CASE("the settings panel's bandless caption tracks the frame like a HUD's") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_band_settings\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasStringRows());
    host.showAllHuds(true);
    host.showSettings(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    REQUIRE(host.setHudTitle("timing_hud", true));

    // Swept over frame size for the same reason the case above is: at [frame] size 2 a
    // constant content origin and the frame's boundary coincide, so one size cannot
    // tell "tracks the frame" from "happens to agree here" -- which is precisely how
    // this panel was missed.
    double prevSettings = -1.0, prevHud = -1.0;
    for (float frame : { 1.0f, 2.0f, 4.0f }) {
        const std::string nm = "sb" + std::to_string(static_cast<int>(frame));
        host.installTheme(nm.c_str(), frame, CARD_INSET, /*titleBand=*/0, /*card=*/1);
        host.draw();

        auto captionDrop = [&](PluginHost::HudId id, const char* text) {
            double y = -1.0;
            for (const auto& r : host.hudStringRows(id)) {
                if (r.text == text) { y = r.y; break; }
            }
            REQUIRE_MESSAGE(y >= 0.0, "caption not drawn: " << text);
            return y - host.hudScreenEdges(id).t / 1e6;
        };
        const double settings = captionDrop(PluginHost::HUD_SETTINGS, "MXBMRP3 SETTINGS");
        const double hud      = captionDrop(PluginHost::HUD_TIMING,   "Timing");

        INFO("frame=" << frame << ": settings caption sits " << settings
             << " below its panel top, a HUD's " << hud);
        // The two captions are DIFFERENT SIZES (the settings title is fontSizeLarge),
        // and addString centres each in its own row -- so the glyph rows differ by half
        // the difference in row height and cannot be compared exactly. What must match
        // is that both move with the frame: the drop's DERIVATIVE, not its value.
        if (prevSettings >= 0.0) {
            INFO("settings moved " << (settings - prevSettings)
                 << " between frame sizes, the HUD " << (hud - prevHud));
            CHECK(std::abs((settings - prevSettings) - (hud - prevHud)) < 1e-4);
        }
        prevSettings = settings;
        prevHud = hud;
    }
    host.setHudTitle("timing_hud", false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// `[title] margin` MOVES A THEMED BAND, and does not merely grow the panel.
//
// The settings panel is the case, and the only panel a headless probe can reach
// that still draws its band through emitTitleBand rather than through the plan
// (every HUD with a testHudById id composes through planPanel now). Two halves
// of one term had drifted apart on that path: titleAdvance() RESERVED the margin,
// so the panel grew, while NineSlice::titleBandTop() clamped the band's top FLUSH
// to the frame's inner boundary and discarded it — so raising the key bought a
// taller panel with the band in exactly the same place and the extra left as dead
// air. The clamp's flush clearance is now the frame PLUS the title box's own top
// margin, which is what a margin means.
//
// Asserted as MONOTONE rather than by amount: how far is panel_box_test's
// question against the shared fixture, not a second copy of the arithmetic here.
TEST_CASE("a themed band honours [title] margin, on both ends") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_band_margin\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasFillCut());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasBoxTerms());
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.installTheme("tm", FRAME_INSET, CARD_INSET, /*titleBand=*/1, /*card=*/1);
    host.showSettings(true);

    auto probe = [&]() {
        host.draw();
        const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_SETTINGS);
        REQUIRE_MESSAGE(cut.themed, "settings panel drew no themed centre");
        REQUIRE_MESSAGE(!cut.covers.empty(), "no band cover recorded");
        const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
        // Band top measured FROM THE PANEL TOP: the panel is re-centred as it
        // grows, so an absolute band top would move for the wrong reason — which
        // is how the first draft of this case passed against the broken code.
        return std::pair<double, double>{ cut.covers.front().t - e.t / 1e6,
                                          (e.b - e.t) / 1e6 };
    };

    host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
    const auto zero = probe();
    host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "2");
    const auto two = probe();
    host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
    const auto back = probe();

    INFO("band top from panel top: " << zero.first << " -> " << two.first
         << "; panel height " << zero.second << " -> " << two.second);
    // A THRESHOLD, not `>`. With the clamp discarding the margin the band's top
    // still moved -- by 1e-6, float wobble from a panel that changed height around
    // it -- and a bare `>` passed on exactly the code this case exists to fail.
    // 1e-4 of screen height is a fifth of a pixel at 1080p: far under the real
    // signal (one whole margin, ~2e-2 here) and far over the noise.
    constexpr double kMoved = 1e-4;
    CHECK_MESSAGE(two.first - zero.first > kMoved,
                  "[title] margin did not push the band down from the panel's top edge");
    CHECK_MESSAGE(two.second - zero.second > kMoved,
                  "[title] margin did not grow the panel that has to hold the air");
    CHECK(std::abs(back.first - zero.first) < 1e-6);
    CHECK(std::abs(back.second - zero.second) < 1e-6);

    host.showSettings(false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// EVERY BRANCH OF THE CAPTION BLOCK SPENDS WHAT IT RESERVES.
//
// The caption block has a two-by-two split — themed or not, band or not — and
// every fix to it so far has landed on one branch and left the other. The
// symptom is always the same shape and always invisible in a screenshot of the
// default theme: titleAdvance() reserves a term, one branch does not spend it,
// and the difference turns up as dead air at the PANEL'S BOTTOM rather than as
// air around the caption. So this case walks all four and asks the same question
// of each: does raising the term move the CONTENT, or only stretch the panel?
//
// Measured as the distance from the panel's own top to its first content row, so
// a panel that merely grew cannot pass. The band's height is not the subject —
// title_band's other cases own that — the question here is only whether the
// content below it moved with the reservation.
TEST_CASE("the settings caption block spends its terms on every branch") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_block_branches\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasBoxTerms());
    REQUIRE(host.hasStringRows());
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.showSettings(true);

    // Top of the first CONTENT row, measured from the panel's top edge. The tab
    // list is the leftmost column and starts at the same y, so the topmost string
    // below the caption answers for the whole block.
    auto contentTop = [&]() {
        host.draw();
        const double panelTop = host.hudScreenEdges(PluginHost::HUD_SETTINGS).t / 1e6;
        double best = 1e9, caption = 1e9;
        for (const auto& r : host.hudStringRows(PluginHost::HUD_SETTINGS)) {
            if (r.text.empty()) continue;
            if (r.text == "MXBMRP3 SETTINGS") { caption = r.y; continue; }
            best = (std::min)(best, r.y);
        }
        REQUIRE(caption < 1e8);
        REQUIRE(best < 1e8);
        // BOTH, and both are needed. The content row alone passes on a branch that
        // drops the caption's TOP terms and keeps its bottom ones: the rows still
        // move, by the half that was spent, while the caption itself sits where it
        // always did. Asking only the second question is how the themed-bandless
        // branch survived the pass that fixed the other three.
        // ...and the air left UNDER the last row, which is the third question and
        // the only one that sees an over-reservation: a term reserved and not spent
        // still moves the caption and the rows (the half that WAS spent moves them),
        // and shows up only as the panel outgrowing its own content. That is how the
        // themed band's bottom margin hid behind two passing checks.
        struct Probe { double caption, content, tail; };
        // The last row of CONTENT, named rather than found by maximum y. Two things
        // at the bottom of this panel are anchored to the panel's own bottom edge --
        // the footer buttons and the tab tooltip -- so "the lowest string" reports a
        // constant whatever the caption block does, and an over-reservation hides
        // behind it. "Widgets" is the last entry in the tab column, which flows from
        // the same currentY the caption block advances.
        double last = -1e9;
        for (const auto& r : host.hudStringRows(PluginHost::HUD_SETTINGS)) {
            if (r.text == "Widgets") last = (std::max)(last, r.y);
        }
        REQUIRE_MESSAGE(last > -1e8, "tab column's last entry not drawn");
        const double panelBot = host.hudScreenEdges(PluginHost::HUD_SETTINGS).b / 1e6;
        return Probe{ caption - panelTop, best - panelTop, panelBot - last };
    };

    struct Branch { const char* what; bool themed; int band; };
    const Branch branches[] = {
        { "unthemed",             false, 0 },
        { "themed, band on",      true,  1 },
        { "themed, band OFF",     true,  0 },   // the branch fixes kept missing
    };
    // ONE SIDE AT A TIME, in CSS shorthand order (top right bottom left). A
    // uniform "3" sets all four, and reserve-vs-spend then compares SUMS: spend a
    // term's TOP at its BOTTOM seam and the totals still agree, so the assertion
    // passes while an asymmetric `0 0 3 0` renders wrong. Demonstrated -- swapping
    // titleMarginY(true) for (false) at the band seam is green under a uniform
    // sweep and red under this one.
    const Term terms[] = {
        { PluginHost::BOX_TITLE_MARGIN,  "titleMargin top",     "6 0 0 0", true  },
        { PluginHost::BOX_TITLE_MARGIN,  "titleMargin bottom",  "0 0 6 0", false },
        { PluginHost::BOX_TITLE_PADDING, "titlePadding top",    "6 0 0 0", true  },
        { PluginHost::BOX_TITLE_PADDING, "titlePadding bottom", "0 0 6 0", false },
    };

    for (const Branch& b : branches) {
        // A DISTINCT NAME PER BRANCH: installTheme registers by name, so reusing one
        // silently kept the first band flag and made the two themed branches the
        // same test run twice — they reported identical numbers, which is what gave
        // it away.
        const std::string themeName = std::string("tb") + std::to_string(b.band);
        if (b.themed) host.installTheme(themeName.c_str(), FRAME_INSET, CARD_INSET,
                                        b.band, /*card=*/1);
        else          host.clearTheme();
        for (const Term& t : terms) {
            CAPTURE(std::string(b.what) + " / " + t.name);
            host.setBoxTerm(t.id, "0");
            const auto zero = contentTop();
            host.setBoxTerm(t.id, t.value);
            const auto three = contentTop();
            host.setBoxTerm(t.id, "0");
            const auto back = contentTop();
            // 1e-4 of screen height is a fifth of a pixel at 1080p: under any real
            // signal and over the float wobble a re-centred panel produces, which
            // is what let a bare `>` pass against the broken code once already.
            if (t.movesCaption) {
                CHECK_MESSAGE(three.caption - zero.caption > 1e-4,
                              "[Advanced] " << std::string(t.name)
                              << " grew the panel without moving the CAPTION on the '"
                              << std::string(b.what) << "' branch");
            } else {
                CHECK_MESSAGE(std::abs(three.caption - zero.caption) < 1e-4,
                              "[Advanced] " << std::string(t.name)
                              << " moved the CAPTION on the '" << std::string(b.what)
                              << "' branch -- a BOTTOM term is spent below it");
            }
            CHECK_MESSAGE(three.content - zero.content > 1e-4,
                          "[Advanced] " << std::string(t.name)
                          << " grew the panel without moving the CONTENT on the '"
                          << std::string(b.what) << "' branch");
            CHECK_MESSAGE(std::abs(three.tail - zero.tail) < 1e-3,
                          "[Advanced] " << std::string(t.name)
                          << " is RESERVED and not spent on the '" << std::string(b.what)
                          << "' branch: the panel's bottom clearance grew with it");
            CHECK(std::abs(back.caption - zero.caption) < 1e-6);
            CHECK(std::abs(back.content - zero.content) < 1e-6);
        }
    }

    host.showSettings(false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// RESERVE EQUALS SPEND, asked ONCE for every skin instead of per branch.
//
// This replaces a "did the caption move / did the panel grow" pair, and the
// replacement is the point. The caption seam has a two-by-two-by-two split
// (themed or not, band or not, settings chain or HUD chain), every round of
// fixes has landed on one branch of it, and every round the test added ALONGSIDE
// the fix could only see that branch -- so the next instance shipped green three
// times running. "Moved" is not the invariant; "moved by what the panel reserved
// for it" is.
//
// THE MEASURE IS THE BODY CARD'S HEIGHT, and it is exact rather than a proxy.
// titleRowHeight reserves the whole caption block ABOVE the card, so a panel
// grows by the reservation while the card's top edge descends by the spend; its
// bottom is the frame clearance either way. So:
//
//     reserve == spend   <=>   the card's height does not change
//                              ... up to ONE grid cell of declared quantization.
//
// Under-spend (a term reserved and not paid) grows the card; over-spend (a term
// paid twice) shrinks it. Both are caught by one number, in one direction-free
// assertion, and neither needs the test to know the arithmetic -- which is what
// kept the per-branch versions blind.
//
// THE QUANTIZER IS PART OF THE CONTRACT, not noise. A caption term is an ART
// cell (cellW * aspect, font-derived); the caption block's advance and the
// panel's height are each ceiled to whole GRID cells (cellH, line-height-
// derived; panel_box.h states why both ceils exist). So a term that is not a
// whole number of grid cells moves the caption by EXACTLY the term while the
// band bottom and the panel bottom move in whole-cell steps -- reserve and
// spend then legitimately differ by less than one cell, and the difference is
// itself a whole number of cells (0 or 1). The first version asserted plain
// equality, which held only because the shipped ratio makes 6 art cells
// exactly 5 grid cells -- flip uiLineHeight and the equality broke with the
// engine behaving exactly as designed. What is asserted now is the design:
// the spend is term-exact, the growth is whole cells, and they differ by
// less than one cell. At a ratio where the term IS whole cells this is
// precisely the old equality, so nothing was weakened at the shipped default.
//
// SessionHud is the panel, and this note is the correction to why. It was
// chosen as "the LEGACY chain's representative", which stopped being true when
// SessionHud was ported to planStandardPanel -- the premise was already stale
// before the legacy chain was retired outright (radar and pitboard were its
// last callers, and both only ever wanted the body card). What the case
// actually drives is the plainest table panel with a caption, which is still
// worth a probe of its own: it is the shape every other caption case is a
// variation on.
TEST_CASE("a caption term moves content by exactly what it reserved") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_legacy\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasBoxTerms());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasFillCut());
    REQUIRE(host.hasLayoutCells());
    // The two lattices the quantizer note above names, from the live metrics.
    const auto lat = host.layoutCells();
    const double kTermCells = 6.0;                    // every swept term is "6"
    const double termV = kTermCells * lat.artV;       // its exact vertical spend
    auto wholeCells = [&](double v) {
        return std::abs(v / lat.cellH - std::round(v / lat.cellH)) < 1e-3;
    };
    // When the term itself lands on the grid the ceils have nothing to round and
    // every quantizer allowance below collapses to the old plain equality -- so
    // at the shipped ratio (6 art cells == 5 grid cells) nothing got looser.
    const bool termOnGrid = wholeCells(termV);
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    // cardH is the themed measure; panelH carries the UNTHEMED skin, which has no
    // card to measure and would otherwise have no coverage at all -- it was dropped
    // when this case moved to the card measure, and that branch is exactly where
    // the previous pass's first finding lived.
    struct Probe { double caption, cardH, cardTop, panelH; };
    bool bandDrawn = false, themed = false;   // set per skin, read by the probe
    auto probe = [&]() {
        host.draw();
        const auto e = host.hudScreenEdges(PluginHost::HUD_SESSION);
        double caption = 1e9;
        for (const auto& r : host.hudStringRows(PluginHost::HUD_SESSION)) {
            if (r.text == "Session") caption = (std::min)(caption, r.y);
        }
        REQUIRE_MESSAGE(caption < 1e8, "session caption not drawn -- is the title on?");
        // The BODY card is the last cover finalizeThemedFill records; the band,
        // when there is one, comes first. Asserted, so a reshuffled cover list
        // fails here instead of quietly measuring the band.
        const double panelH = (e.b - e.t) / 1e6;
        if (!themed) return Probe{ caption - e.t / 1e6, 0.0, 0.0, panelH };
        const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_SESSION);
        REQUIRE_MESSAGE(cut.themed, "session hud drew no themed centre");
        REQUIRE(!cut.covers.empty());
        // STRICTLY more than one cover when a band is drawn, and strictly top-down.
        // `front().t <= back().t` was the check, and it is VACUOUS on a one-element
        // list -- so "band drawn, card missing" would have silently measured the
        // band's height instead of the card's and reported it as a pass.
        REQUIRE_MESSAGE(cut.covers.size() >= (bandDrawn ? 2u : 1u),
                        "expected a card" << (bandDrawn ? " under a band" : "")
                        << ", got " << cut.covers.size() << " cover(s)");
        if (cut.covers.size() > 1) {
            REQUIRE_MESSAGE(cut.covers.front().t < cut.covers.back().t,
                            "cover list is not top-down -- which one is the card?");
        }
        const auto& card = cut.covers.back();
        return Probe{ caption - e.t / 1e6, card.b - card.t, card.t - e.t / 1e6, panelH };
    };

    struct Skin { const char* what; bool themed; int band; };
    const Skin skins[] = {
        { "themed, band on",  true,  1 },
        { "themed, band OFF", true,  0 },
        { "unthemed",         false, 0 },   // no card: measured on the panel instead
    };
    // One side at a time, for the reason spelled out at the settings case above:
    // a uniform sweep compares SUMS, and spending a term's top at its bottom seam
    // survives that.
    const Term terms[] = {
        { PluginHost::BOX_TITLE_MARGIN,  "titleMargin top",     "6 0 0 0", true  },
        { PluginHost::BOX_TITLE_MARGIN,  "titleMargin bottom",  "0 0 6 0", false },
        { PluginHost::BOX_TITLE_PADDING, "titlePadding top",    "6 0 0 0", true  },
        { PluginHost::BOX_TITLE_PADDING, "titlePadding bottom", "0 0 6 0", false },
    };

    for (const Skin& sk : skins) {
        // A distinct theme per band flag: installTheme registers by NAME, and
        // reusing one silently keeps the first flag -- which made two branches of
        // the settings case the same test run twice before it was caught.
        bandDrawn = sk.themed && sk.band != 0;
        themed = sk.themed;
        if (sk.themed) host.installTheme((std::string("mp") + std::to_string(sk.band)).c_str(),
                                         FRAME_INSET, CARD_INSET, sk.band, /*card=*/1);
        else           host.clearTheme();
        // Re-asserted per skin: a theme change re-reads the HUD's settings, and the
        // panel then draws an EMPTY caption string rather than none, which reads as
        // "drawn" to a careless probe.
        REQUIRE(host.setHudTitle("session_widget", true));
        for (const Term& t : terms) {
            CAPTURE(std::string(sk.what) + " / " + t.name);
            host.setBoxTerm(t.id, "0");
            const Probe zero = probe();
            host.setBoxTerm(t.id, t.value);
            const Probe three = probe();
            host.setBoxTerm(t.id, "0");
            const Probe back = probe();

            // THE ASSERTION, in the two forms one measure cannot cover. Themed, the
            // card's height is invariant iff reserve == spend. UNTHEMED there is no
            // card, so the same statement is read off the panel: a TOP term grows
            // the panel by exactly what it moves the caption, and a BOTTOM term
            // grows it while leaving the caption where it is.
            if (!sk.themed) {
                const double grew = three.panelH - zero.panelH;
                const double moved = three.caption - zero.caption;
                CHECK_MESSAGE(grew > 1e-4, "[Advanced] " << std::string(t.name)
                              << " (unthemed) did not grow the panel at all");
                if (t.movesCaption) {
                    // The SPEND is term-exact, always: the caption moved by the
                    // term itself, no quantizer between them (planTitleY centres
                    // in the glyph box, not into titleSlack).
                    CHECK_MESSAGE(std::abs(moved - termV) < 1e-4,
                                  "[Advanced] " << std::string(t.name)
                                  << " (unthemed) moved the caption by " << moved
                                  << " for a term of " << termV
                                  << " -- the spend is not term-exact");
                    // The RESERVE is the spend pushed through the panel-height
                    // ceil: whole cells, within one cell of the spend -- and
                    // equal to it outright when the term lands on the grid.
                    CHECK_MESSAGE(wholeCells(grew),
                                  "[Advanced] " << std::string(t.name)
                                  << " (unthemed) grew the panel by " << grew
                                  << " -- not a whole number of grid cells");
                    CHECK_MESSAGE(std::abs(grew - moved) <
                                      (termOnGrid ? 1e-4 : lat.cellH - 1e-6),
                                  "[Advanced] " << std::string(t.name)
                                  << " (unthemed) grew the panel by " << grew
                                  << " and moved the caption by " << moved
                                  << " -- reserve and spend disagree beyond the "
                                  "declared quantizer");
                } else {
                    // WHAT THIS CANNOT SEE, said out loud: "the panel grew and the
                    // caption stayed put" is also true of a bottom term RESERVED and
                    // never spent, and this probe measures no content row to catch
                    // the difference with. The spend side of an unthemed bottom term
                    // is covered on the settings panel instead -- same skin, and it
                    // has rows plus a tail to measure. Do not read this pair as a
                    // proof of spend.
                    CHECK_MESSAGE(std::abs(moved) < 1e-4,
                                  "[Advanced] " << std::string(t.name)
                                  << " (unthemed) moved the CAPTION -- a BOTTOM term "
                                  "is spent below it");
                }
                CHECK(std::abs(back.caption - zero.caption) < 1e-6);
                CHECK(std::abs(back.panelH - zero.panelH) < 1e-6);
                continue;
            }
            // Reserve == spend, up to the quantizer: the band bottom and the
            // panel bottom each ceil independently, so a term that is not whole
            // grid cells may move the card's two edges by ceils that differ by
            // one cell -- the card then grows or shrinks by exactly that cell,
            // absorbed as slackY (panel_box.h names the absorber). Whole-cell
            // term => the two ceils agree and the card must not move at all
            // (1e-4 of screen height is a fifth of a pixel at 1080p).
            const double dCard = three.cardH - zero.cardH;
            CHECK_MESSAGE(wholeCells(dCard),
                          "[Advanced] " << std::string(t.name)
                          << " (" << std::string(sk.what) << ") changed the body card "
                          "by " << dCard << " -- not a whole number of grid cells");
            CHECK_MESSAGE(std::abs(dCard) <
                              (termOnGrid ? 1e-4 : lat.cellH + 1e-6),
                          "[Advanced] " << std::string(t.name)
                          << " (" << std::string(sk.what) << ") moved the body card by "
                          << dCard << " MORE than the panel "
                          "reserved for it -- reserve and spend disagree beyond the "
                          "declared quantizer");
            // ...and the term is not simply dead, which would satisfy the above.
            if (t.movesCaption) {
                CHECK_MESSAGE(three.caption - zero.caption > 1e-4,
                              "[Advanced] " << std::string(t.name)
                              << " (" << std::string(sk.what) << ") moved nothing at all");
            } else {
                CHECK_MESSAGE(std::abs(three.caption - zero.caption) < 1e-4,
                              "[Advanced] " << std::string(t.name) << " ("
                              << std::string(sk.what) << ") moved the CAPTION -- a BOTTOM "
                              "term is spent below it, not above");
                // ...and it still has to be spent SOMEWHERE. This used to re-assert
                // the cardH check above it, byte for byte, under a comment claiming
                // the opposite -- so a bottom term that was reserved and never spent
                // satisfied every line here. What proves the spend is the card's TOP
                // moving down by it: cardH constant says reserve == spend, cardTop
                // moving says the spend happened at all.
                CHECK_MESSAGE(three.cardTop - zero.cardTop > 1e-4,
                              "[Advanced] " << std::string(t.name) << " ("
                              << std::string(sk.what) << ") did not move the card down -- "
                              "reserved and never spent");
            }
            CHECK(std::abs(back.caption - zero.caption) < 1e-6);
            CHECK(std::abs(back.cardH - zero.cardH) < 1e-6);
        }
    }

    host.setHudTitle("session_widget", false);
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// THE TWO CHAINS AGREE, measured where they actually DIFFER.
//
// The first version of this case compared the CAPTION's offset from the panel
// top, and it could not fail: in every configuration it drove, that offset is
// produced by shared BaseHud code with no input from the caller -- band-on,
// titleBandTop saturates on flush (paddingV >= frameMarginY is guaranteed);
// band-off, emitCaptionRow's themed arm is absolute. So the two sides were equal
// by construction and the assertion was decoration.
//
// What genuinely differs is the ADVANCE BELOW the caption: titleRowHeight on the
// legacy chain against SettingsHud::titleAdvance on the settings one, two
// separately maintained sums that every caption bug on this branch was reported
// as a disagreement between. The body card's top is where each one lands, so
// that is what is compared -- as a DELTA, since the two panels legitimately
// start at different offsets.
//
// AND THE BAND'S OWN TOP IS PINNED TO THE FRAME, which nothing did. Setting the
// settings panel's caption row to a bare `startY` (dropping frameBorderY) drew
// its band over the whole frame border and the entire suite stayed green -- the
// exact regression the code's comment describes, unfalsifiable by the code's own
// tests. The band's top must be PROPORTIONAL to the theme's frame inset, which a
// zero cannot satisfy at either inset.
TEST_CASE("the legacy and settings chains place a caption to the same rules") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\title_crosschain\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasBoxTerms());
    REQUIRE(host.hasFillCut());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    {
        std::vector<TrackSegmentRow> circle(64);
        const float trackLen = 1600.0f;
        for (auto& seg : circle) {
            seg.type = 1;
            seg.length = trackLen / 64.0f;
            seg.radius = trackLen / (2.0f * 3.14159265f);
            seg.angle = 0.0f;
        }
        host.trackCenterline(circle, { 800.0f, 400.0f, 1200.0f, 0.0f });
    }
    host.showSettings(true);

    // The band's top and the topmost CARD's top, both from the panel's own top
    // edge. finalizeThemedFill records the band first, then the cards.
    // `banded` says whether cover[0] is a BAND (and so must be skipped when looking
    // for the topmost card). Passed in rather than inferred from the count: a panel
    // with a band and no card would otherwise report the band as its card, which is
    // the vacuous-comparison trap this file has already hit once.
    struct Geom { double bandTop, cardTop; };
    auto geom = [&](PluginHost::HudId id, bool banded) {
        const auto e = host.hudScreenEdges(id);
        const PluginHost::FillCut cut = host.hudFillCut(id);
        REQUIRE_MESSAGE(cut.themed, "panel drew no themed centre");
        REQUIRE_MESSAGE(cut.covers.size() >= (banded ? 2u : 1u),
                        "expected " << (banded ? "a band and " : "") << "at least one card, got "
                        << cut.covers.size());
        double card = 1e9;
        for (size_t i = banded ? 1u : 0u; i < cut.covers.size(); ++i)
            card = (std::min)(card, cut.covers[i].t);
        return Geom{ banded ? cut.covers.front().t - e.t / 1e6 : 0.0, card - e.t / 1e6 };
    };

    // --- the band's top follows the FRAME, on both chains ---------------------
    // Every chain's clearance is AFFINE in the frame now -- frame border plus
    // [panel] padding, the legacy chain through panelSurfaceInsetY and a plan
    // panel through its spec -- and an affine term is not proportional. The
    // frame's own contribution is still exactly what the ratio was testing, so
    // the padding term is removed from the measurement instead: with the theme
    // stating zero [panel] padding (and the [title] terms zeroed below), the
    // clearance is the frame's alone again and doubling the inset must double
    // it. That keeps the absolute anchor a delta-only comparison cannot
    // provide: two chains can move in step and both be wrong by the same
    // constant.
    {
        // ZEROED EXPLICITLY: the proportionality below holds for the frame clearance
        // ALONE, and a nonzero shipped [title] margin would ride on top of it and
        // fail the ratio with "does not track the frame" -- a true failure with a
        // misleading name. The default is 0 today; that is not this case's business.
        host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
        host.setBoxTerm(PluginHost::BOX_TITLE_PADDING, "0");
        host.installTheme("cf2", /*inset=*/2.0f, CARD_INSET, /*titleBand=*/1, /*card=*/1);
        REQUIRE(host.setThemePanelPadding(0.0f, 0.0f));
        REQUIRE(host.setHudTitle("session_widget", true));
        host.draw();
        const Geom leg2 = geom(PluginHost::HUD_SESSION, true);
        const Geom set2 = geom(PluginHost::HUD_SETTINGS, true);
        host.installTheme("cf4", /*inset=*/4.0f, CARD_INSET, /*titleBand=*/1, /*card=*/1);
        REQUIRE(host.setThemePanelPadding(0.0f, 0.0f));
        REQUIRE(host.setHudTitle("session_widget", true));
        host.draw();
        const Geom leg4 = geom(PluginHost::HUD_SESSION, true);
        const Geom set4 = geom(PluginHost::HUD_SETTINGS, true);

        CHECK_MESSAGE(set2.bandTop > 1e-4,
                      "the settings band sits ON the panel's top edge -- it is supposed "
                      "to clear the frame's border");
        CHECK_MESSAGE(leg2.bandTop > 1e-4, "the legacy band sits on the panel's top edge");
        CHECK_MESSAGE(std::abs(leg4.bandTop - 2.0 * leg2.bandTop) < 1e-4,
                      "with [panel] padding zeroed the legacy band's top does not track "
                      "the frame: " << leg2.bandTop << " at inset 2, " << leg4.bandTop
                      << " at inset 4");
        // Two cells of frame move BOTH bands by the same distance -- the legacy
        // chain's whole clearance, and the frame half of the settings panel's.
        CHECK_MESSAGE(std::abs((set4.bandTop - set2.bandTop)
                             - (leg4.bandTop - leg2.bandTop)) < 1e-4,
                      "the settings band's top does not track the frame: it moved "
                      << (set4.bandTop - set2.bandTop) << " where the legacy band moved "
                      << (leg4.bandTop - leg2.bandTop));
        // ...and whatever constant separates the two chains' clearances (zero,
        // with the padding they both spend zeroed) stays constant across frame
        // sizes.
        CHECK_MESSAGE(std::abs((set4.bandTop - leg4.bandTop)
                             - (set2.bandTop - leg2.bandTop)) < 1e-4,
                      "the chains' clearances diverge with the frame: legacy "
                      << leg2.bandTop << "/" << leg4.bandTop << ", settings "
                      << set2.bandTop << "/" << set4.bandTop);
    }

    // --- the advance BELOW the caption responds identically --------------------
    // titleRowHeight vs titleAdvance: two sums, maintained apart, and the surface
    // every caption bug on this branch was reported as a disagreement between.
    for (int band : { 1, 0 }) {
        host.installTheme((std::string("cc") + std::to_string(band)).c_str(),
                          FRAME_INSET, CARD_INSET, band, /*card=*/1);
        REQUIRE(host.setHudTitle("session_widget", true));
        for (const Term& t : { Term{ PluginHost::BOX_TITLE_MARGIN,  "titleMargin top",     "6 0 0 0", true  },
                               Term{ PluginHost::BOX_TITLE_MARGIN,  "titleMargin bottom",  "0 0 6 0", false },
                               Term{ PluginHost::BOX_TITLE_PADDING, "titlePadding top",    "6 0 0 0", true  },
                               Term{ PluginHost::BOX_TITLE_PADDING, "titlePadding bottom", "0 0 6 0", false } }) {
            CAPTURE(std::string(t.name) + (band ? " / band on" : " / band OFF"));
            host.setBoxTerm(t.id, "0");
            host.draw();
            const double hud0 = geom(PluginHost::HUD_SESSION, band != 0).cardTop;
            const double set0 = geom(PluginHost::HUD_SETTINGS, band != 0).cardTop;
            host.setBoxTerm(t.id, t.value);
            host.draw();
            const double hudD = geom(PluginHost::HUD_SESSION, band != 0).cardTop - hud0;
            const double setD = geom(PluginHost::HUD_SETTINGS, band != 0).cardTop - set0;
            host.setBoxTerm(t.id, "0");

            CHECK_MESSAGE(hudD > 1e-4, "[Advanced] " << std::string(t.name)
                          << " did not move the legacy chain's card at all");
            CHECK_MESSAGE(std::abs(hudD - setD) < 1e-4,
                          "[Advanced] " << std::string(t.name) << " moved the LEGACY "
                          "chain's card by " << hudD << " and the SETTINGS chain's by "
                          << setD << " -- titleRowHeight and titleAdvance disagree");
        }
    }

    host.showSettings(false);
    host.setHudTitle("session_widget", false);
    host.clearTheme();
}
