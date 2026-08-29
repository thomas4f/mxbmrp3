// ============================================================================
// hud/settings/whats_new.cpp
// The marker table and the dismissed-set. See whats_new.h for the design.
// ============================================================================
#include "whats_new.h"

#include "../settings_hud.h"
#include "../../core/plugin_constants.h"

#include <algorithm>
#include <set>

namespace WhatsNew {

namespace {

// THE TABLE. Reviewed every feature release: prune what is no longer new, add
// what is, and keep it short enough that a marked row still means something.
// check_whats_new.sh fails the build if a version here falls behind resource.h,
// so the review is not optional.
//
// 1.29, the first release since 1.28.0, shipped about twenty menu-reachable
// changes. Six are marked, and the ones left out were left out on purpose:
//   - the SPOTTER tab already carries a Beta tag, and two badges on one row say
//     less than one;
//   - the per-HUD Theme row is the same feature as Appearance > Theme, and
//     marking it would light up every HUD tab to say so twice;
//   - Standings' last-lap column and the radar/gap-bar label anchor are real, and
//     findable by someone already in that tab.
const Marker kMarkers[] = {
    // Panel themes: the most visible thing in the release, and invisible until
    // you find the one picker that turns it on. The row itself is conditional --
    // the Appearance tab hides it when no themes are installed -- so a player who
    // deleted mxbmrp3_data\themes\ gets the tab tag with nothing behind it. Left
    // as is: no themes means the feature is genuinely not there to point at, and
    // suppressing the tag would mean teaching the marker table about layout.
    { SettingsHud::TAB_APPEARANCE, "appearance.theme", "1.29" },
    // A whole new widget, in a long list of widgets.
    { SettingsHud::TAB_WIDGETS,    "widgets.crashes",  "1.29" },
    // The gamepad's Texture row picks a PACK now, not a texture number.
    { SettingsHud::TAB_WIDGETS,    "widgets.gamepad",  "1.29" },
    // ...and so does the pit board's. NOTE the id: a pack HUD's Texture row is
    // built by addPackControl and registers "pitboard.pack", NOT the
    // "common.texture" that every non-pack HUD's Texture row uses. Both rows are
    // labelled "Texture" on screen, so the wrong one here is invisible -- it draws
    // no band and reports nothing, while the tab keeps its tag. It was wrong here
    // first; whats_new_test's resolve case is what found it.
    { SettingsHud::TAB_PITBOARD,   "pitboard.pack",    "1.29" },
    // ...and now the two gauges', for the same reason and with the same shape of
    // change: their Texture row picks a gauges PACK (the dial face plus what it
    // READS) rather than a texture number. Two markers for one feature, unlike
    // the single badge the table prefers, because they are two independent rows
    // -- somebody who runs only the tacho would never see a badge left on the
    // speedo. The ids are the row-wide tooltips addWidgetRow registers, which is
    // what the gamepad row above uses too; a pack HUD in the WIDGETS table does
    // not go through addPackControl and so has no "<type>.pack" id of its own.
    { SettingsHud::TAB_WIDGETS,    "widgets.speedo",   "1.29" },
    { SettingsHud::TAB_WIDGETS,    "widgets.tacho",    "1.29" },
    // The Timing panel's second section. All seven rows share one tooltip id, so
    // one marker bands the whole block -- which is what it is: they arrived
    // together and are one feature, not seven.
    { SettingsHud::TAB_TIMING,     "timing.readouts",  "1.29" },
    // The spotter's own hotkey. Marked even though the Spotter tab is not: a
    // hotkey lives on the Hotkeys tab, which is not where anyone reading about
    // the spotter would think to look.
    { SettingsHud::TAB_HOTKEYS,    "hotkeys.spotter_cue", "1.29" },
};

// The dismissed keys. A std::set of strings rather than flags on the table:
// the stored file may name markers this build has never heard of (written by a
// newer one), and those have to survive a load/save round trip -- see
// serialize().
std::set<std::string>& dismissed() {
    static std::set<std::string> s;
    return s;
}

std::string rowKey(int tabId, const char* rowTooltipId) {
    return std::to_string(tabId) + ":" + (rowTooltipId ? rowTooltipId : "");
}
std::string tabKey(int tabId) {
    return "T" + std::to_string(tabId);
}

}  // namespace

const Marker* const MARKERS = kMarkers;
const int MARKER_COUNT = static_cast<int>(sizeof(kMarkers) / sizeof(kMarkers[0]));

// "1.29" out of "1.29.1.766". Computed once: PLUGIN_VERSION is fixed for the
// life of the process.
const char* currentLine() {
    static const std::string line = [] {
        const std::string v = PluginConstants::PLUGIN_VERSION;
        const size_t first = v.find('.');
        if (first == std::string::npos) return v;
        const size_t second = v.find('.', first + 1);
        return second == std::string::npos ? v : v.substr(0, second);
    }();
    return line.c_str();
}

bool isLive(const Marker& m) {
    // BELONGS TO THIS RELEASE. The comparison is on MAJOR.MINOR, not the full
    // version, so a patch release (1.29.1 -> 1.29.2) does not re-arm markers the
    // player already dismissed, and does not need the table touched either.
    if (std::strcmp(m.sinceVersion, currentLine()) != 0) return false;
    const auto& d = dismissed();
    return d.find(rowKey(m.tabId, m.rowTooltipId)) == d.end();
}

bool tabHasLive(int tabId) {
    if (dismissed().count(tabKey(tabId))) return false;
    for (int i = 0; i < MARKER_COUNT; ++i) {
        if (kMarkers[i].tabId == tabId && isLive(kMarkers[i])) return true;
    }
    return false;
}

const Marker* liveForRow(int tabId, const char* rowTooltipId) {
    if (!rowTooltipId || !*rowTooltipId) return nullptr;
    for (int i = 0; i < MARKER_COUNT; ++i) {
        const Marker& m = kMarkers[i];
        if (m.tabId != tabId) continue;
        if (std::strcmp(m.rowTooltipId, rowTooltipId) != 0) continue;
        return isLive(m) ? &m : nullptr;
    }
    return nullptr;
}

bool dismissTab(int tabId) {
    // The TAG only. The rows keep their bands: opening the tab says "I know
    // there is something new here", not "I have found it".
    //
    // The insert's own `inserted` flag is the return value rather than a separate
    // "was it live" test: re-opening a tab must not report a change, or every tab
    // click would mark the settings file dirty for nothing.
    return dismissed().insert(tabKey(tabId)).second;
}

bool dismissRow(int tabId, const char* rowTooltipId) {
    if (!rowTooltipId || !*rowTooltipId) return false;
    if (!liveForRow(tabId, rowTooltipId)) return false;   // nothing marked here
    return dismissed().insert(rowKey(tabId, rowTooltipId)).second;
}

std::string serialize() {
    std::string out;
    for (const std::string& k : dismissed()) {
        if (!out.empty()) out += ',';
        out += k;
    }
    return out;
}

void deserialize(const std::string& csv) {
    dismissed().clear();
    size_t start = 0;
    while (start <= csv.size()) {
        const size_t comma = csv.find(',', start);
        const size_t end = (comma == std::string::npos) ? csv.size() : comma;
        std::string key = csv.substr(start, end - start);
        // Trim, because a hand-edited INI is a supported workflow.
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        if (!key.empty()) dismissed().insert(key);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

}  // namespace WhatsNew
