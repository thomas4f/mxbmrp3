// ============================================================================
// tests/integration/tests/box_terms_test.cpp
// EVERY AIR TERM REACHES AN UNTHEMED PANEL.
//
// The box model states eight terms and the settings menu documents all eight,
// but four of them — titleMargin, titlePadding, contentMargin, contentPadding —
// did nothing at all unless a theme was selected, which is not the default.
// PanelBox::layoutPanel collapsed the whole title and content BOXES on
// `hasTitle` / `hasCard`, switches that mean "there is ART to draw a band or a
// card with", when only the BORDER needs art; SettingsHud::cardPad*() gated its
// content padding the same way. A user who set them saw nothing happen and
// reasonably concluded the keys were fake.
//
// WHY NOTHING CAUGHT IT. Every other geometry case either installs a theme
// first (theme_geometry, theme_panel_padding, center_stack_theme) or asserts a
// term against itself — internally consistent either way. "The panel is
// self-consistent" is exactly what a dead term satisfies. So this file asks the
// one question none of them do, and asks it the crude way on purpose: set the
// term, look at the panel, and require that SOMETHING moved.
//
// It deliberately does NOT assert by how much. A number here would be a second
// copy of the arithmetic panel_box_test already pins against the shared
// fixture, and it would go stale on every retune — the failure this guards is a
// term wired to nothing, which "did not move at all" catches and a golden would
// bury among the retunes.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

namespace {

// A panel's rect plus every string it drew, as one comparable blob. Rect alone
// is not enough: a padding term can move the CONTENT inside a panel whose size
// is set by something else (the settings panel's width is fixed by its columns),
// and that is still the term acting.
std::string fingerprint(PluginHost& host, PluginHost::HudId which) {
    host.draw();
    const auto r = host.hudPanelRect(which);
    std::string out = std::to_string(r.w) + "x" + std::to_string(r.h);
    for (const auto& s : host.hudStringRows(which)) {
        out += "|" + std::to_string(static_cast<long long>(s.x * 1e6))
             + "," + std::to_string(static_cast<long long>(s.y * 1e6));
    }
    return out;
}

}  // namespace

TEST_CASE("box terms: every one of the eight moves an UNTHEMED panel") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\box_terms\\");
    REQUIRE_MESSAGE(host.hasBoxTerms(), "MXBMRP3_Test_SetBoxTerm not exported");
    REQUIRE(host.hasStringRows());

    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.session(6, 10);
    host.classify(6, 60000, { { .num = 12, .laps = 1 } });
    // NO THEME — the whole point. clearTheme() rather than trusting the default,
    // so a stray installTheme in an earlier case cannot make this pass.
    host.clearTheme();
    host.showSettings(true);

    struct Term {
        PluginHost::BoxTermId id;
        const char* key;
        PluginHost::HudId on;   // a panel that has the child this term belongs to
        const char* what;
    };
    // Each term is checked on a panel that actually HAS the box it belongs to:
    // Standings has a caption and a content section, the settings panel is the
    // only one with a footer button row.
    const Term terms[] = {
        { PluginHost::BOX_PANEL_PADDING,  "panelPadding",   PluginHost::HUD_STANDINGS, "panel border to content" },
        { PluginHost::BOX_TITLE_MARGIN,   "titleMargin",    PluginHost::HUD_STANDINGS, "air outside the caption block" },
        { PluginHost::BOX_TITLE_PADDING,  "titlePadding",   PluginHost::HUD_STANDINGS, "air around the caption" },
        { PluginHost::BOX_CONTENT_MARGIN, "contentMargin",  PluginHost::HUD_STANDINGS, "air outside the section" },
        { PluginHost::BOX_CONTENT_PADDING,"contentPadding", PluginHost::HUD_STANDINGS, "air inside the section" },
        { PluginHost::BOX_BUTTON_MARGIN,  "buttonMargin",   PluginHost::HUD_SETTINGS,  "air outside a footer button" },
        { PluginHost::BOX_BUTTON_PADDING, "buttonPadding",  PluginHost::HUD_SETTINGS,  "air inside a footer button" },
        { PluginHost::BOX_PANEL_GAP,      "panelGap",       PluginHost::HUD_STANDINGS, "air at each junction" },
    };

    // EACH SIDE ON ITS OWN, not one uniform value. A single "3" is CSS shorthand
    // for all four sides, so a term wired to the WRONG side of its own box still
    // moves the panel and still passes -- exactly the blindness that let a
    // caption's top margin be spent at its bottom seam and stay green
    // (title_band_test carries that story, and swept sides apart to end it).
    // `gap` is the one term with no sides; the loop skips its three extra passes.
    struct Side { const char* name; const char* value; };
    const Side sides[] = {
        { "top",    "3 0 0 0" },
        { "right",  "0 3 0 0" },
        { "bottom", "0 0 3 0" },
        { "left",   "0 0 0 3" },
    };

    for (const Term& t : terms) {
        const bool scalar = t.id == PluginHost::BOX_PANEL_GAP;
        for (size_t si = 0; si < sizeof(sides) / sizeof(sides[0]); ++si) {
            const Side& side = sides[si];
            if (scalar && si > 0) break;        // gap has no sides
            // (`side.value != sides[0].value` was the guard, comparing const char*
            //  POINTERS -- right only because the compiler pooled the literals.)
            const std::string label =
                std::string(t.key) + (scalar ? "" : std::string(" ") + side.name);
            CAPTURE(label);
            host.setBoxTerm(t.id, "0");
            const std::string zero = fingerprint(host, t.on);
            host.setBoxTerm(t.id, scalar ? "3" : side.value);
            const std::string three = fingerprint(host, t.on);
            host.setBoxTerm(t.id, "0");
            const std::string back = fingerprint(host, t.on);

            // A RIGHT-side term on the CAPTION's box is legitimately absorbed here,
            // and this is the shrink-to-fit rule rather than a dead term: the panel's
            // width is the widest child's ask (panel_box.h), the caption's ask is
            // insetL + text + insetR, and on every panel this case drives the CONTENT
            // column is wider -- so the caption's right inset is slack. That the
            // engine DOES spend it when the caption is the widest child is pinned
            // where it can be: panel_box_parity.json's `caption-sets-width` case,
            // which the live plugin has no panel in the shape of.
            //
            // Asserted as absorbed rather than skipped, so a change that starts
            // moving the panel from here fails and gets explained.
            const bool captionRight = (t.id == PluginHost::BOX_TITLE_MARGIN ||
                                       t.id == PluginHost::BOX_TITLE_PADDING) &&
                                      std::string(side.name) == "right";
            if (captionRight) {
                CHECK_MESSAGE(zero == three,
                              "[Advanced] " << label << " now moves a panel whose CONTENT "
                              "is the widest ask -- the caption's right inset used to be "
                              "absorbed by shrink-to-fit. Explain the change here.");
                continue;
            }
            CHECK_MESSAGE(zero != three,
                          "[Advanced] " << label << " (" << t.what
                          << ") changed nothing on an unthemed panel");
            // ...and it is the TERM doing it, not drift: putting it back restores
            // the panel exactly. A rebuild that wandered would pass the first check
            // on its own.
            CHECK_MESSAGE(zero == back,
                          "[Advanced] " << label << " did not restore at 0 — the panel "
                          "is not a pure function of the terms");
        }
    }
}

// The same question for the one panel that lays its own geometry out rather than
// going through PanelBox::layoutPanel. It is the panel a user cannot move, hide
// or resize, so a term that misses it misses the one place the miss is loudest —
// and it is where contentPadding was found gated on a theme a second time, after
// layoutPanel's copy of the same mistake had already been fixed.
//
// ALL EIGHT REACH IT NOW. Three did not when this case was written --
// panelPadding, titleMargin and titlePadding -- because the panel read the
// LEGACY panelPadding{X,Y}Cells through basePaddingY() and never asked for the
// caption block's terms at all. It reads the box terms for its own chrome now
// (SettingsHud::boxPanelPad*/boxTitle*), so every row below is `true` and the
// case is the same question the first one asks, on the panel that answers it
// with its own geometry rather than through PanelBox::layoutPanel.
TEST_CASE("box terms: the settings panel spends them too, unthemed") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\box_terms_settings\\");
    REQUIRE_MESSAGE(host.hasBoxTerms(), "MXBMRP3_Test_SetBoxTerm not exported");
    host.clearTheme();
    host.showSettings(true);

    struct Case { PluginHost::BoxTermId id; const char* key; bool reaches; };
    const Case cases[] = {
        { PluginHost::BOX_PANEL_PADDING,   "panelPadding",   true  },
        { PluginHost::BOX_TITLE_PADDING,   "titlePadding",   true  },
        { PluginHost::BOX_TITLE_MARGIN,    "titleMargin",    true  },
        { PluginHost::BOX_CONTENT_MARGIN,  "contentMargin",  true  },
        { PluginHost::BOX_CONTENT_PADDING, "contentPadding", true  },
        { PluginHost::BOX_BUTTON_MARGIN,   "buttonMargin",   true  },
        { PluginHost::BOX_BUTTON_PADDING,  "buttonPadding",  true  },
        { PluginHost::BOX_PANEL_GAP,       "panelGap",       true  },
    };

    for (const Case& c : cases) {
        CAPTURE(std::string(c.key));
        host.setBoxTerm(c.id, "0");
        const std::string zero = fingerprint(host, PluginHost::HUD_SETTINGS);
        host.setBoxTerm(c.id, "3");
        const std::string three = fingerprint(host, PluginHost::HUD_SETTINGS);
        host.setBoxTerm(c.id, "0");
        if (c.reaches) {
            CHECK_MESSAGE(zero != three, "[Advanced] " << c.key
                          << " changed nothing on the UNTHEMED settings panel");
        } else {
            CHECK_MESSAGE(zero == three, "[Advanced] " << c.key
                          << " NOW reaches the unthemed settings panel — good, but "
                          "this case says it does not. Move it to reaches=true and "
                          "delete its paragraph above.");
        }
    }
}

// A BUTTON INSIDE A SECTION IS STILL A BUTTON. The two cases above are satisfied
// by the FOOTER row alone — Save and Close are on the same panel, so any
// [button] term that moves them moves the panel's fingerprint and the term reads
// as wired. It was not, for the five action buttons drawn inside a tab's content
// (Copy, Reset, Check Now, Retry, Install): SettingsLayoutContext::buttonRow
// spent the border and the padding but never read marginT/marginB, and each
// caller wrote `currentY += lineHeightNormal * 0.5f` ahead of the button — a
// magic half-row no setting could reach, standing in for the junction gap.
//
// So this case names ONE such button and follows it, rather than fingerprinting
// the panel it sits on.
TEST_CASE("box terms: an in-section button spends them, not just the footer") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\box_terms_inline_button\\");
    REQUIRE_MESSAGE(host.hasBoxTerms(), "MXBMRP3_Test_SetBoxTerm not exported");
    REQUIRE(host.hasStringRows());
    host.clearTheme();
    host.showSettings(false);
    host.setActiveTab("General");
    host.showSettings(true);   // show() marks dirty; a bare tab set does not
    REQUIRE(host.activeTab() == std::string("General"));

    // The [Copy] button's label measured FROM THE ROW ABOVE IT, not from the
    // screen. Absolute y is not an answer here: the settings panel is centred,
    // so any term that changes its HEIGHT — buttonMargin does, through the
    // footer — slides every string on it, and an absolute check reads that slide
    // as the in-section button being wired. It was not, and the first draft of
    // this case passed against the very code it was written to fail.
    // The distance between the cycle row and the button under it is the thing
    // the button's own terms own.
    auto copyGap = [&]() {
        host.draw();
        double anchor = -1.0, copy = -1.0;
        for (const auto& s : host.hudStringRows(PluginHost::HUD_SETTINGS)) {
            if (s.text == "Copy") copy = s.y;
            else if (s.text.rfind("Copy current profile", 0) == 0) anchor = s.y;
        }
        return (anchor < 0.0 || copy < 0.0) ? -1.0 : copy - anchor;
    };

    struct Case { PluginHost::BoxTermId id; const char* key; };
    const Case cases[] = {
        { PluginHost::BOX_BUTTON_MARGIN,  "buttonMargin"  },
        { PluginHost::BOX_BUTTON_PADDING, "buttonPadding" },
        { PluginHost::BOX_PANEL_GAP,      "panelGap"      },
    };
    for (const Case& c : cases) {
        CAPTURE(std::string(c.key));
        host.setBoxTerm(c.id, "0");
        const double zero = copyGap();
        REQUIRE(zero > 0.0);          // both rows are on screen at all
        host.setBoxTerm(c.id, "3");
        const double three = copyGap();
        host.setBoxTerm(c.id, "0");
        const double back = copyGap();
        CHECK_MESSAGE(zero != three, "[Advanced] " << c.key
                      << " did not move the General tab's [Copy] button relative to the row above it");
        CHECK_MESSAGE(zero == back, "[Advanced] " << c.key
                      << " did not restore the [Copy] button at 0");
    }
}
