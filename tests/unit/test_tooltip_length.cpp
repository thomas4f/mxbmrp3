// ============================================================================
// tests/unit/test_tooltip_length.cpp
// Guards the settings-tooltip LENGTH LIMIT documented in tooltip_manager.h:
// tooltips render as at most 2 word-wrapped lines and anything beyond ~120
// characters is HARD-TRUNCATED with "..." in the settings panel (see
// renderWrappedText in settings_hud.cpp, MAX_LINES=2). This test compiles the
// real tooltip table and fails if any description would be cut off — it would
// have caught the appearance.display_target / general.auto_save / director.max_shot
// overflows. Keeps every tooltip readable in-game without a manual audit.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/tooltip_manager.h"

TEST_CASE("tooltips: no description exceeds the 2-line settings box (truncation limit)") {
    // The renderer hard-truncates past ~120 chars; assert against that ceiling so
    // no tooltip is ever displayed cut off. (The comment in tooltip_manager.h
    // recommends ~100 for wrap margin — this is the hard fail line.)
    constexpr size_t kMaxChars = 120;

    for (const auto& tip : TooltipManager::allTooltips()) {
        const std::string& id   = tip.first;
        const std::string& text = tip.second;
        INFO("tooltip '" << id << "' is " << text.size() << " chars: \"" << text << "\"");
        CHECK(text.size() <= kMaxChars);
    }
}
