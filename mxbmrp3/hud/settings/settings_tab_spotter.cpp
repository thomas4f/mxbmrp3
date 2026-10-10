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
#include <memory>
#include <string>
#include <vector>

#if defined(MXBMRP3_TEST_BUILD)
// How many times the lists below were read (MXBMRP3_Test_SpotterListReads).
int g_spotterListReads = 0;
#endif

namespace {
// The installed pack folders, a directory scan, are read once per menu opening
// (SettingsHud::m_openSerial), not on each rebuild (the panel rebuilds on every
// hover change, twice: the measure pass, then the draw); a pack installed while
// the menu is open shows the next time it opens. The SAPI voices are the
// session list SpotterManager read at plugin load (getTtsVoices).
//
// NOT while the panel measures its tallest tab: every opening lays out every
// tab for its height, Spotter included, and the lists change no row's height,
// so the measure gets empty ones and the scan waits until the tab is drawn.
struct SpotterLists {
    bool read = false;
    unsigned serial = 0;
    std::shared_ptr<const std::vector<std::string>> packs, voices;
};
const SpotterLists& spotterLists(unsigned openSerial, bool measuring) {
    static SpotterLists s_lists;
    if (measuring) {
        static const SpotterLists s_empty = [] {
            SpotterLists e;
            e.packs = std::make_shared<const std::vector<std::string>>();
            e.voices = e.packs;
            return e;
        }();
        return s_empty;
    }
    if (!s_lists.read || s_lists.serial != openSerial) {
        const SpotterManager& spotter = SpotterManager::getInstance();
        s_lists.packs = std::make_shared<const std::vector<std::string>>(spotter.listAvailablePacks());
        s_lists.voices = SpotterManager::getInstance().getTtsVoices();   // once per session
        s_lists.serial = openSerial;
        s_lists.read = true;
#if defined(MXBMRP3_TEST_BUILD)
        ++g_spotterListReads;
#endif
    }
    return s_lists;
}
}  // namespace

bool SettingsHud::handleClickTabSpotter(const ClickRegion& region) {
    SpotterManager& spotter = SpotterManager::getInstance();

    // SPOTTER_ENABLED_TOGGLE is deliberately NOT here: the tab list carries
    // that same toggle as its row checkbox, and a tab-scoped handler only
    // runs while its own tab is open — so it lives in dispatchRegion's
    // common switch (settings_hud_input.cpp), like the director's.
    switch (region.type) {
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
    // with a themed card -- so 58 at the shipped 61). Each line is written to that
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
    ctx.beginColumns(2, 3);   // side by side (beginColumns)
    ctx.addToggleControl("Subtitles", spotter.isSubtitlesEnabled(),
        SettingsHud::ClickRegion::SPOTTER_SUBTITLES_TOGGLE, nullptr,
        nullptr, 0, true, "spotter.subtitles");
    ctx.addOpacityControl(&widget, spotter.isSubtitlesEnabled());
    ctx.addScaleControl(&widget, spotter.isSubtitlesEnabled());

    ctx.endColumns();

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
        ctx.addSteppedControl("Volume", buf, sc, nullptr, on, false,
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
        ctx.addSteppedControl("Speed", buf, sc, nullptr, on, false,
                              "spotter.speed");
    }

    // Voice pack: a folder under mxbmrp3_data/spotters. `default` is the
    // shipped one and is also the BASE every other pack layers over, so a
    // recorded pack covering half the cues still speaks the other half. Cycle
    // even while audio is off — a pack sets the subtitle text too.
    // Shown by its title, cycled and stored by its folder name -- see
    // SpotterManager::getPackDisplayName.
    const std::string& packName = spotter.getPackName();
    //
    // The list is the installed pack folders. There is no "None" entry: the
    // shipped `default` pack is the wording, so "None" would mean silence, which
    // is what the Spoken audio switch is for. The CURRENT name stays in the list
    // even when its folder is missing (at its sorted place), so picking away and
    // back never rewrites a temporarily-absent pack out of the INI.
    //
    // Both lists play the sample line after a pick: a pack is a folder name until
    // you hear it, and two voices differ in a way no label can carry.
    const SpotterLists& lists = spotterLists(ctx.parent->m_openSerial, ctx.parent->m_measuringTallest);
    auto packs = std::make_shared<std::vector<std::string>>(*lists.packs);
    if (!packName.empty() && std::find(packs->begin(), packs->end(), packName) == packs->end()) {
        packs->push_back(packName);
        std::sort(packs->begin(), packs->end());
    }
    SettingsHud::CycleControl pack;
    pack.count = static_cast<int>(packs->size());
    pack.get = [packs]() {
        const auto it = std::find(packs->begin(), packs->end(), SpotterManager::getInstance().getPackName());
        return it == packs->end() ? 0 : static_cast<int>(it - packs->begin());
    };
    pack.set = [packs](int i) { SpotterManager::getInstance().setPackName((*packs)[static_cast<size_t>(i)]); };
    pack.nameOf = [packs](int i) { return (*packs)[static_cast<size_t>(i)]; };
    pack.postStep = []() { SpotterManager::getInstance().previewVoice(); };
    pack.repeat = false;   // a step loads the pack and plays a preview
    ctx.addCycleControl("Voice pack", spotter.getPackDisplayName().c_str(), pack,
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
    // [System default] + the installed voices -- SpotterTtsVoice::cycle's ring,
    // whose step the arrows keep.
    const std::shared_ptr<const std::vector<std::string>> voices = lists.voices;
    SettingsHud::CycleControl voice;
    voice.count = static_cast<int>(voices->size()) + 1;
    voice.get = [voices]() {
        const std::string& cur = SpotterManager::getInstance().getTtsVoice();
        for (size_t i = 0; i < voices->size(); ++i) {
            if ((*voices)[i] == cur) return static_cast<int>(i) + 1;
        }
        return 0;
    };
    voice.set = [voices](int i) {
        SpotterManager::getInstance().setTtsVoice(i <= 0 ? std::string() : (*voices)[static_cast<size_t>(i) - 1]);
    };
    voice.step = [voices](bool forward) {
        SpotterManager& s = SpotterManager::getInstance();
        s.setTtsVoice(SpotterTtsVoice::cycle(*voices, s.getTtsVoice(), forward));
    };
    voice.nameOf = [voices](int i) {
        return i <= 0 ? std::string("System default") : SpotterTtsVoice::displayName((*voices)[static_cast<size_t>(i) - 1]);
    };
    voice.postStep = []() { SpotterManager::getInstance().previewVoice(/*ttsOnly=*/true); };
    voice.repeat = false;  // a step plays a preview
    ctx.addCycleControl("TTS voice", ttsShown.c_str(), voice,
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
    ctx.beginColumns(2, 5);
    for (const auto& row : kRows) {
        const uint32_t bit = 1u << static_cast<unsigned>(row.cat);
        ctx.addToggleControl(row.label, (mask & bit) != 0,
            SettingsHud::ClickRegion::CHECKBOX, &widget,
            spotter.categoryMaskPtr(), bit, true, row.tip);
    }

    ctx.endColumns();

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
    ctx.beginColumns(2, 4);

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
        ctx.addSteppedControl("Behind distance", buf, sc, nullptr,
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
        ctx.addSteppedControl("Alongside back", buf, sc, nullptr,
                              proxLive, false, "spotter.alongside_on_m");
    }

    snprintf(buf, sizeof(buf), "%.0fm",
             spotter.hazardConfig().alongsideAheadMeters);
    {
        auto sc = SettingsHud::SteppedControl::fixedFloat(
            spotter.alongsideAheadMetersPtr(), 1.0f, 0.0f, 20.0f, nullptr);
        ctx.addSteppedControl("Alongside front", buf, sc, nullptr,
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
        ctx.addSteppedControl("Width", buf, sc, nullptr, proxLive, false,
                              "spotter.lateral_m");
    }
    ctx.endColumns();

    return nullptr;  // No specific HUD for this tab
}
