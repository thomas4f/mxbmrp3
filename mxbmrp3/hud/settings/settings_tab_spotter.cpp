// ============================================================================
// hud/settings/settings_tab_spotter.cpp
// Tab renderer for the spotter (audio race callouts + subtitle widget).
//
// The spotter is a global manager like the Director, so this tab drives
// SpotterManager::getInstance() directly and returns nullptr (no backing
// HUD). The subtitle widget's appearance rows lead, matching the Director
// tab's shape (its status widget leads there too).
//
// Category checkboxes ride the generic CHECKBOX region on the manager's
// mask; volume and speed are data-driven stepped controls writing the
// manager's members, with a postStep that republishes to the audio worker's
// atomic copies (see SpotterManager::publishAudioSettings). Speed is a
// decimal MULTIPLIER because it drives both backends — the wav paths
// time-stretch by it (core/spotter_stretch.h), SAPI takes it mapped onto its
// coarse -10..10 rate. The TTS voice picker cycles the OS's installed SAPI
// voices in place of a trip to Windows' speech settings.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../spotter_widget.h"
#include "../../core/hud_manager.h"
#include "../../core/spotter_manager.h"
#include "../../core/spotter_stretch.h"
#include "../../core/spotter_tts_voice.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
// Step the pack choice through the installed pack folders. There is no "None"
// entry: the shipped `default` pack is the wording, so "None" would mean
// silence, which is what the Spoken audio switch is for.
//
// The CURRENT name is honored even when its folder is missing: it still
// occupies its sorted slot, so cycling away and back never rewrites a
// temporarily-absent pack out of the INI.
void cyclePack(SpotterManager& spotter, bool forward) {
    std::vector<std::string> options = spotter.listAvailablePacks();
    const std::string& current = spotter.getPackName();
    if (!current.empty() &&
        std::find(options.begin(), options.end(), current) == options.end()) {
        options.push_back(current);
        std::sort(options.begin(), options.end());
    }
    if (options.empty()) return;   // nothing installed: nothing to cycle
    const auto it = std::find(options.begin(), options.end(), current);
    size_t idx = (it == options.end()) ? 0 : (it - options.begin());
    idx = forward ? (idx + 1) % options.size()
                  : (idx + options.size() - 1) % options.size();
    spotter.setPackName(options[idx]);
}
}  // namespace

bool SettingsHud::handleClickTabSpotter(const ClickRegion& region) {
    SpotterManager& spotter = SpotterManager::getInstance();

    // SPOTTER_ENABLED_TOGGLE is deliberately NOT here: the tab list carries
    // that same toggle as its row checkbox, and a tab-scoped handler only
    // runs while its own tab is open — so it lives in dispatchRegion's
    // common switch (settings_hud_input.cpp), like the director's.
    switch (region.type) {
        // Both cyclers play the sample line after switching: a pack is a folder
        // name until you hear it, and the difference between two voices (or two
        // Windows TTS voices) is not something a label can carry.
        case ClickRegion::SPOTTER_PACK_PREV:
        case ClickRegion::SPOTTER_PACK_NEXT:
            cyclePack(spotter, region.type == ClickRegion::SPOTTER_PACK_NEXT);
            spotter.previewVoice();
            setDataDirty();
            markSettingsDirty();
            return true;

        case ClickRegion::SPOTTER_TTSVOICE_PREV:
        case ClickRegion::SPOTTER_TTSVOICE_NEXT:
            spotter.setTtsVoice(SpotterTtsVoice::cycle(
                spotter.listTtsVoices(), spotter.getTtsVoice(),
                region.type == ClickRegion::SPOTTER_TTSVOICE_NEXT));
            spotter.previewVoice(/*ttsOnly=*/true);
            setDataDirty();
            markSettingsDirty();
            return true;

        case ClickRegion::SPOTTER_SUBTITLES_TOGGLE:
            spotter.setSubtitlesEnabled(!spotter.isSubtitlesEnabled());
            HudManager::getInstance().getSpotterWidget().setDataDirty();
            setDataDirty();
            markSettingsDirty();
            return true;

        default:
            return false;
    }
}

BaseHud* SettingsHud::renderTabSpotter(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("spotter");

    SpotterManager& spotter = SpotterManager::getInstance();
    SpotterWidget& widget = HudManager::getInstance().getSpotterWidget();
    const bool on = spotter.isEnabled();

    char buf[16];

    // --- What "Beta" means here, in the reader's terms. -------------------
    //
    // FIRST, before any control, because it is what the rest of the tab should be
    // read through. The tab's sidebar entry carries the same badge (see
    // TabDescriptor::badge).
    //
    // Written for a player, not a developer: no versions, no file formats, no
    // roadmap. It says the three things that stop a reasonable person filing the
    // same disappointment three different ways -- the wording is not settled,
    // their opinion is how it settles, and the robot voice is not the only option.
    // Anything more belongs in the docs, which the heading points at rather than
    // reproducing.
    //
    // addTextRow, NOT addNote. A note is a muted 0.9x aside that belongs to the
    // control above it, and it opens with half a row of air on top of the heading's
    // full one -- on screen that reads as a gap where nothing goes, and as body text
    // shrunk for no reason the reader can see. This paragraph is not an aside about a
    // control; it is what the tab says before it asks anything, so it is ordinary
    // text at ordinary size, one row under its heading like every other row.
    //
    // Lines do not wrap: each is its own row, broken by hand inside the content
    // column's budget (settingsContentAreaChars - settingsLabelColumn, one narrower
    // with a themed card -- so 50 at the shipped 53). Each line is written to that
    // length, not trimmed to one.
    //
    // THREE LINES, and the ceiling is real rather than taste: every tab shares one
    // panel height, set by the tallest, and the panel does not scroll.
    // settings_render_test measures the tab against one screen.
    //
    // WHAT IT SAYS. It leads with what this release IS -- a demonstration of what the
    // spotter can do -- because that is what sets the right expectation for a first
    // hearing. Someone who knows they are hearing a sketch judges it as a sketch. The
    // tuning is then stated as intent ("its behavior will change based on your
    // feedback") rather than as a request for opinions: a player reading a tab called
    // Beta already expects change, and a request reads as work being handed to them
    // before they have heard the thing.
    //
    // US spelling ("behavior") because the user-facing strings and the README use it
    // throughout; the code comments are the only place British spelling survives.
    //
    // ONE SENTENCE PER ROW, not lines packed to the margin: a sentence broken
    // mid-clause makes the eye carry a fragment across the break on every line, which
    // reads worse than a ragged right. So each row is a whole sentence short enough
    // to fit, and the wording is cut to make that possible rather than the break
    // moved.
    //
    // WARNING-COLOURED, the same slot the sidebar's "Beta" tag draws in. Secondary is
    // the colour of every other line of prose in the panel, so in it three rows of
    // "this is provisional" would read exactly like three rows of ordinary help text,
    // with nothing connecting them to the tag. One slot for the whole idea, so the
    // tag and the paragraph it expands read as the same statement.
    //
    // The heading carries NO hint: a hint lands at SECTION_HINT_COLUMN (16), a column
    // chosen for hints that annotate a wide table ("(click to track/untrack)"), so
    // beside a 4-letter word it reads as a caption floating in the middle of nowhere.
    ctx.addSectionHeading("Beta");
    ctx.addTextRow("This is a demonstration, not a finished spotter.",
                   ColorConfig::getInstance().getWarning());
    ctx.addTextRow("Its behavior will change based on your feedback.",
                   ColorConfig::getInstance().getWarning());
    ctx.addTextRow("Recorded voice packs can replace Windows speech.",
                   ColorConfig::getInstance().getWarning());

    // --- Subtitles: the on-screen text widget (also the testing surface). ---
    ctx.addSectionHeading("Subtitles");
    ctx.addToggleControl("Subtitles", spotter.isSubtitlesEnabled(),
        SettingsHud::ClickRegion::SPOTTER_SUBTITLES_TOGGLE, nullptr,
        nullptr, 0, true, "spotter.subtitles");
    snprintf(buf, sizeof(buf), "%d%%",
             static_cast<int>(widget.getBackgroundOpacity() * 100.0f + 0.5f));
    ctx.addCycleControl("Opacity", buf, 10,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP,
        &widget, spotter.isSubtitlesEnabled(), false, "common.opacity");
    snprintf(buf, sizeof(buf), "%d%%",
             static_cast<int>(widget.getScale() * 100.0f + 0.5f));
    ctx.addCycleControl("Scale", buf, 10,
        SettingsHud::ClickRegion::SCALE_DOWN,
        SettingsHud::ClickRegion::SCALE_UP,
        &widget, spotter.isSubtitlesEnabled(), false, "common.scale");

    // --- Voice: the audio side. ---
    ctx.addSectionHeading("Voice");
    ctx.addToggleControl("Spoken audio", on,
        SettingsHud::ClickRegion::SPOTTER_ENABLED_TOGGLE, nullptr,
        nullptr, 0, true, "spotter.enabled");

    snprintf(buf, sizeof(buf), "%d%%", spotter.getVolume());
    {
        auto sc = SettingsHud::SteppedControl::clampInt(
            spotter.volumePtr(), 5, 0, 100, nullptr);
        sc.postStep = []() { SpotterManager::getInstance().publishAudioSettings(); };
        ctx.addSteppedControl("Volume", buf, 10, sc, nullptr, on, false,
                              "spotter.volume");
    }

    // Speed as a MULTIPLIER, in 0.05 steps: it drives both backends (the wav
    // paths time-stretch, SAPI takes the mapped integer rate), so a decimal
    // here is not cosmetic — 1.15x is a real setting that SAPI's coarse
    // -10..10 scale could not express.
    snprintf(buf, sizeof(buf), "%.2fx", spotter.getSpeed());
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.speedPtr(), 0.05f, SpotterStretch::kMinSpeed,
            SpotterStretch::kMaxSpeed, nullptr);
        sc.postStep = []() { SpotterManager::getInstance().publishAudioSettings(); };
        ctx.addSteppedControl("Speed", buf, 10, sc, nullptr, on, false,
                              "spotter.speed");
    }

    // Voice pack: a folder under mxbmrp3_data/spotters. `default` is the
    // shipped one and is also the BASE every other pack layers over, so a
    // recorded pack covering half the cues still speaks the other half. Cycle
    // even while audio is off — a pack sets the subtitle text too.
    // Shown by its title, cycled and stored by its folder name -- see
    // SpotterManager::getPackDisplayName.
    const std::string& packName = spotter.getPackName();
    ctx.addCycleControl("Voice pack", spotter.getPackDisplayName().c_str(), 10,
        SettingsHud::ClickRegion::SPOTTER_PACK_PREV,
        SettingsHud::ClickRegion::SPOTTER_PACK_NEXT,
        nullptr, true, packName == "default", "spotter.pack");

    // TTS voice: which Windows voice speaks the text-to-speech cues. Enabled
    // whenever spoken audio is — NOT only on the None (TTS) pack: a pack
    // that doesn't cover a cue falls back down the ladder to TTS, so the
    // voice is audible with a pack selected too. The list is empty where
    // SAPI isn't (every Wine prefix), which "System default" reads correctly.
    const std::string& ttsVoice = spotter.getTtsVoice();
    // Shortened for the row, not for the setting — see displayName().
    const std::string ttsShown = ttsVoice.empty()
        ? std::string("System default")
        : SpotterTtsVoice::displayName(ttsVoice);
    ctx.addCycleControl("TTS voice", ttsShown.c_str(), 10,
        SettingsHud::ClickRegion::SPOTTER_TTSVOICE_PREV,
        SettingsHud::ClickRegion::SPOTTER_TTSVOICE_NEXT,
        nullptr, on, ttsVoice.empty(), "spotter.tts_voice");

    // --- Callouts: which cue categories are announced (audio AND subtitle —
    // the gate sits before composition, so a muted category is truly silent,
    // whatever the pack says; nobody has to edit an ini to quieten a group).
    //
    // The ORDER here is the order of the headings in the shipped pack, so the
    // file reads as the same five groups these switches name. Reorder one and
    // reorder the other. The mapping itself is checked:
    // test_spotter_pack_census.cpp asserts every cue sits under the heading
    // for the category that actually mutes it. ---
    ctx.addSectionHeading("Callouts");
    const uint32_t mask = spotter.getCategoryMask();
    struct { const char* label; SpotterPhrase::Category cat; const char* tip; } kRows[] = {
        { "General",   SpotterPhrase::Category::General,   "spotter.cat_general" },
        { "Timing",    SpotterPhrase::Category::Timing,    "spotter.cat_timing" },
        { "Opponents", SpotterPhrase::Category::Opponents, "spotter.cat_opponents" },
        { "Proximity", SpotterPhrase::Category::Proximity, "spotter.cat_proximity" },
        { "Hazards",   SpotterPhrase::Category::Hazard,    "spotter.cat_hazard" },
    };
    for (const auto& row : kRows) {
        const uint32_t bit = 1u << static_cast<unsigned>(row.cat);
        ctx.addToggleControl(row.label, (mask & bit) != 0,
            SettingsHud::ClickRegion::CHECKBOX, &widget,
            spotter.categoryMaskPtr(), bit, true, row.tip);
    }

    // --- Proximity: how close a rider has to be before the PROXIMITY
    // category's cues fire at all — the distances behind the switch of the
    // same name directly above. Its own section because these are the only
    // rows that tune WHEN a cue happens rather than what is said or how it
    // sounds.
    //
    // Live whenever either output is on: the gate runs before composition, so
    // it decides the subtitles as much as the audio.
    //
    // The four rows are a BOX around you. Three give its length — how far
    // back a rider is called at all, and how much overlap counts as alongside
    // in each direction — and the fourth its width. They do overlap by
    // design: a rider inside both the behind distance and the alongside
    // window gets the alongside call, because the detector ranks a rival
    // beside you above one merely behind you.
    //
    // Their release thresholds ([Spotter] behind_clear_m / alongside_clear_m)
    // and the repeat cooldowns stay INI-only: those exist to stop chatter,
    // and a rider who wants a quieter spotter is served by moving a trigger,
    // not a release.
    ctx.addSectionHeading("Proximity");
    const bool proxLive = on || spotter.isSubtitlesEnabled();

    snprintf(buf, sizeof(buf), "%.0fm", spotter.hazardConfig().behindOnMeters);
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.behindOnMetersPtr(), 1.0f, 2.0f, 60.0f, nullptr);
        // Re-enter the setter: it owns the "release must stay above trigger"
        // contract, and a raw write past behind_clear_m would invert the
        // hysteresis band and turn behind/clear into a chatter machine.
        sc.postStep = []() {
            SpotterManager& s = SpotterManager::getInstance();
            s.setBehindOnMeters(s.hazardConfig().behindOnMeters);
        };
        ctx.addSteppedControl("Behind distance", buf, 10, sc, nullptr,
                              proxLive, false, "spotter.behind_on_m");
    }

    // The alongside window is two rows because it is asymmetric: it reaches
    // well BACK into the blind spot and only a little FORWARD, since a rider
    // up the road is one you are already looking at. Front at 0 calls nobody
    // you could see by turning your head.
    snprintf(buf, sizeof(buf), "%.0fm",
             spotter.hazardConfig().alongsideOnMeters);
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.alongsideOnMetersPtr(), 1.0f, 1.0f, 20.0f, nullptr);
        sc.postStep = []() {
            SpotterManager& s = SpotterManager::getInstance();
            s.setAlongsideOnMeters(s.hazardConfig().alongsideOnMeters);
        };
        ctx.addSteppedControl("Alongside back", buf, 10, sc, nullptr,
                              proxLive, false, "spotter.alongside_on_m");
    }

    snprintf(buf, sizeof(buf), "%.0fm",
             spotter.hazardConfig().alongsideAheadMeters);
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.alongsideAheadMetersPtr(), 1.0f, 0.0f, 20.0f, nullptr);
        ctx.addSteppedControl("Alongside front", buf, 10, sc, nullptr,
                              proxLive, false, "spotter.alongside_ahead_m");
    }

    // How far ACROSS the track a rider can be and still be called. The Radar
    // HUD's "Alert distance" is the same idea: the radar filters on
    // straight-line distance, so a rider on the far side of a wide straight
    // is nowhere near, and without this width the proximity calls, measured
    // along the racing line only, would announce them anyway.
    // Same units, same kind of control — this one is the WIDTH of the gate,
    // where the two above are its length.
    snprintf(buf, sizeof(buf), "%.0fm", spotter.hazardConfig().lateralMeters);
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.lateralMetersPtr(), 1.0f, 3.0f, 60.0f, nullptr);
        ctx.addSteppedControl("Width", buf, 10, sc, nullptr, proxLive, false,
                              "spotter.lateral_m");
    }

    return nullptr;  // No specific HUD for this tab
}
