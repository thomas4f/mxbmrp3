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
// 1.32 is the Delta Trace release: its tab bands the Visible switch, the one
// thing on it to turn on (no tag: the name fills the sidebar, tabCanTag below).
// The Map's Lap delta, Tilt, Adaptive range and Mode (Follow now fades the map at its
// edge), the Gap Bar's Range (now Auto by default), the Timing panel's Gap, the Stream Chat's Display
// order, the Appearance tab's Motion, Messages, UI scale and Companion bg, the
// General tab's Reference and Freeze defaults and the Delta Trace hotkey are
// the release's other new rows. The 1.31 markers - the Stream Chat Status switches,
// the Gap Bar's Splits and Reference, the Lap Log's Gap reference and Gap
// freeze, the Pitboard's Freeze, the Standings' Class column - are gone: each
// had its release, and a tag still lit a release later says nothing.
//
// Prestige is the table's one Gate::Unlocked row: it is not new in a release,
// it is new the moment a player trades their ladder for it, and the row it
// bands does not exist on the Widgets tab until then.
const Marker kMarkers[] = {
    { SettingsHud::TAB_DELTA_TRACE,  "common.visible",           "1.32" },
    { SettingsHud::TAB_MAP,          "map.lap_delta",            "1.32" },
    { SettingsHud::TAB_MAP,          "map.tilt",                 "1.32" },
    { SettingsHud::TAB_MAP,          "map.range_adaptive",       "1.32" },
    { SettingsHud::TAB_MAP,          "map.mode",                 "1.32" },
    { SettingsHud::TAB_GAP_BAR,      "gap_bar.range",            "1.32" },
    { SettingsHud::TAB_TIMING,       "timing.gap",               "1.32" },
    { SettingsHud::TAB_STREAM_CHAT,  "stream_chat.order",        "1.32" },
    { SettingsHud::TAB_APPEARANCE,   "appearance.motion",        "1.32" },
    { SettingsHud::TAB_APPEARANCE,   "appearance.messages",      "1.32" },
    { SettingsHud::TAB_APPEARANCE,   "appearance.ui_scale",      "1.32" },
    { SettingsHud::TAB_APPEARANCE,   "appearance.companion_background", "1.32" },
    { SettingsHud::TAB_GENERAL,      "general.reference",        "1.32" },
    { SettingsHud::TAB_GENERAL,      "general.freeze",           "1.32" },
    { SettingsHud::TAB_HOTKEYS,      "hotkeys.delta_trace",      "1.32" },
    { SettingsHud::TAB_WIDGETS,      "widgets.rpm",              "1.32" },
    { SettingsHud::TAB_WIDGETS,      "widgets.prestige",         nullptr, Gate::Unlocked },
};

// See setUnlocked().
bool g_unlocked = false;

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
    // SCOPED TO THE RELEASE LINE, like the markers themselves. It was "T<tab>"
    // and kept for good, so opening a tab once killed its "New" tag in every
    // future version too -- and since a marker is only live in the release that
    // added it, a new one on a tab anybody had ever opened could never tag it.
    // Old unscoped keys stay in the file and are simply never looked up again,
    // which costs one thing worth stating: a player upgrading from 1.30.1 sees
    // the Achievements tab's dot ONCE more, because their dismissal was stored
    // under the old spelling. Not migrated - the old key says nothing about
    // WHICH line it dismissed, so folding it into the current one would hide a
    // genuinely new marker from anyone who last opened that tab a line ago. A
    // dot shown twice is the cheaper mistake.
    //
    // A tab carrying a Gate::Unlocked marker gets a SECOND key once that gate
    // opens: the unlock is news whenever it happens, and a player who opened
    // the Widgets tab earlier in the release line would otherwise prestige to
    // a row band with no tag pointing at the tab it is on.
    std::string key = "T" + std::to_string(tabId) + ":" + currentLine();
    if (g_unlocked) {
        for (const Marker& m : kMarkers) {
            if (m.tabId == tabId && m.gate == Gate::Unlocked) return key + ":unlocked";
        }
    }
    return key;
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

void setUnlocked(bool on) { g_unlocked = on; }

bool isLive(const Marker& m) {
    if (m.gate == Gate::Unlocked) {
        // No version to compare: the unlock is the event. Dismissal is still
        // for good -- a second prestige does not make the row new again.
        if (!g_unlocked) return false;
    } else if (std::strcmp(m.sinceVersion, currentLine()) != 0) {
        // BELONGS TO THIS RELEASE. The comparison is on MAJOR.MINOR, not the
        // full version, so a patch release (1.29.1 -> 1.29.2) does not re-arm
        // markers the player already dismissed, and does not need the table
        // touched either. (The sidebar DOT is keyed separately - see tabKey,
        // which names the one upgrade that shows it again.)
        return false;
    }
    const auto& d = dismissed();
    return d.find(rowKey(m.tabId, m.rowTooltipId)) == d.end();
}

// A tab whose name fills the sidebar's label cells gets no "New" tag: the
// Achievements, Stream Chat and Delta Trace rows in s_tabRegistry (settings_hud_render.cpp)
// have the arithmetic. Their markers still band their rows, and the sidebar
// bands their ROW instead (tabHighlightsRow).
bool tabCanTag(int tabId) {
    return tabId != SettingsHud::TAB_ACHIEVEMENTS && tabId != SettingsHud::TAB_STREAM_CHAT
        && tabId != SettingsHud::TAB_DELTA_TRACE;
}

// Undismissed news on this tab, whichever way the sidebar shows it.
static bool tabHasNews(int tabId) {
    if (dismissed().count(tabKey(tabId))) return false;
    for (int i = 0; i < MARKER_COUNT; ++i) {
        if (kMarkers[i].tabId == tabId && isLive(kMarkers[i])) return true;
    }
    return false;
}

bool tabHasLive(int tabId) {
    return tabCanTag(tabId) && tabHasNews(tabId);
}

bool tabHighlightsRow(int tabId) {
    return !tabCanTag(tabId) && tabHasNews(tabId);
}

int firstTabWithNews() {
    for (int i = 0; i < MARKER_COUNT; ++i) {
        if (tabHasNews(kMarkers[i].tabId)) return kMarkers[i].tabId;
    }
    return -1;
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
