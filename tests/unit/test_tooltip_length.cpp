// ============================================================================
// tests/unit/test_tooltip_length.cpp
// Guards the settings-tooltip LENGTH LIMIT documented in tooltip_manager.h:
// tooltips render as at most 2 word-wrapped lines in the settings panel, and
// anything that does not fit is cut off. This test compiles the real tooltip
// table and fails if any description would be truncated — it would have caught
// the appearance.display_target / general.auto_save / director.max_shot
// overflows. Keeps every tooltip readable in-game without a manual audit.
//
// IT NOW ASKS THE RENDERER'S OWN ALGORITHM. This used to assert a hardcoded
// ~120-character ceiling, because the wrap rules lived in a lambda inside
// SettingsHud::rebuildRenderData() and nothing below the Wine layer could reach
// them. That ceiling was an estimate in two directions at once: it passed
// tooltips that wrap badly (many short words losing characters to line breaks)
// and it would fail harmless ones. Now that the wrap is TextWrap::wrap, every
// tooltip goes through the SAME function the panel draws with, and the
// assertion is the real question — "does this get cut off?" — instead of a
// proxy for it. A panel-width or font change moves the box, and this test
// follows it.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/tooltip_manager.h"
#include "hud/settings/settings_metrics.h"
#include "hud/settings/text_wrap.h"

namespace {

// Not a magic number: the SAME constant expression the renderer passes to
// TextWrap::wrap, from the panel's own character metrics. Widen the panel and
// this guard widens with it on the next run.
constexpr int TOOLTIP_CHARS_PER_LINE = SettingsMetrics::tooltipCharsPerLine();

}  // namespace

TEST_CASE("tooltips: none is cut off by the 2-line settings box") {
    for (const auto& tip : TooltipManager::allTooltips()) {
        const std::string& id   = tip.first;
        const std::string& text = tip.second;

        const auto wrapped = TextWrap::wrap(text, TOOLTIP_CHARS_PER_LINE,
                                            TextWrap::TOOLTIP_LINES);

        INFO("tooltip '" << id << "' (" << text.size() << " chars) wraps to "
             << wrapped.lines.size() << " line(s): \"" << text << "\"");
        CHECK_FALSE(wrapped.truncated);
    }
}

TEST_CASE("tooltips: the guard actually bites") {
    // A canary, so the test above can never pass because the wrap stopped
    // reporting truncation. Something clearly too long for two lines must fail.
    std::string tooLong;
    while (tooLong.size() < static_cast<size_t>(TOOLTIP_CHARS_PER_LINE) * 4) {
        tooLong += "overflowing ";
    }
    CHECK(TextWrap::wrap(tooLong, TOOLTIP_CHARS_PER_LINE, TextWrap::TOOLTIP_LINES).truncated);
}
