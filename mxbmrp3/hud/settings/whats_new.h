// ============================================================================
// hud/settings/whats_new.h
// "New" markers on settings tabs and rows, for the release a player just
// upgraded into.
//
// THE PROBLEM THIS SOLVES: a feature that lands in a settings row nobody opens
// is a feature nobody has. The changelog reaches the handful of players who
// read release notes; everyone else meets the plugin through this menu, and a
// new row looks exactly like a row that was always there.
//
// So a marked tab carries a "New" tag in the sidebar, and the marked row draws
// a band in the POSITIVE colour (an invitation, not a caveat -- the Beta tag keeps
// the warning slot). The tag clears when the tab is OPENED and the
// band clears when the row is HOVERED -- both meaning "you have seen where this
// lives", which is all the marker was ever claiming.
//
// CURATED, NOT COMPLETE, and that is the whole design. Marking everything new
// in a release marks a third of the menu, and a third of the menu lit up is
// wallpaper -- it buries the one row that would have been worth finding. The
// table below is a handful of headline items chosen per release, and the
// judgement of which ones is the point. 1.29 shipped ~20 menu-reachable
// changes; six are marked.
//
// SELF-EXPIRING, because a stale "New" tag is worse than no tag at all: it
// teaches players that the tag means nothing. A marker is live only while its
// `sinceVersion` matches the running plugin's MAJOR.MINOR, so the whole table
// goes dark on the next feature release whether or not anyone remembered to
// prune it. check_whats_new.sh (CI) then fails the build on a marker older than
// resource.h, so "remembered to prune it" is not something anyone has to do.
// ============================================================================
#pragma once

#include <cstring>
#include <string>
#include <vector>

namespace WhatsNew {

// One marked row. `rowTooltipId` is the row-wide tooltip id the row already
// registers -- rows carry one for their hover text, so there is no second
// identity to invent and nothing to keep in step.
//
// KEYED ON THE TAB TOO, because several of those ids are SHARED: the pit board's
// pack row registers "common.texture", the same id every HUD's Texture row uses,
// and "common.theme" belongs to the per-HUD theme override on all twenty-odd HUD
// tabs. Keying on the id alone would light up every one of them.
struct Marker {
    int tabId;                  // SettingsHud::Tab value
    const char* rowTooltipId;   // the row's own tooltip id
    const char* sinceVersion;   // "MAJOR.MINOR" -- see liveness above
};

// THE TABLE lives in whats_new.cpp, not here: it names tabs by their Tab enum
// value, and settings_hud.h is what defines that enum. Magic numbers with a
// comment would be a second copy of the enum, wrong the first time a tab moves.
extern const Marker* const MARKERS;
extern const int MARKER_COUNT;

// The running plugin's "MAJOR.MINOR", which is what a marker's version is
// compared against. Defined in the .cpp so this header does not drag in
// resource.h -- see the note at PluginConstants::PLUGIN_VERSION for why that
// include is kept to one translation unit.
const char* currentLine();

// Has this marker not yet been dismissed, and does it belong to this release?
bool isLive(const Marker& m);

// Any live marker on this tab -- drives the sidebar's "New" tag.
bool tabHasLive(int tabId);

// The live marker for a row, or nullptr. `rowTooltipId` may be empty.
const Marker* liveForRow(int tabId, const char* rowTooltipId);

// Dismiss. Opening a tab dismisses its TAG (every marker on it stops counting
// toward tabHasLive); hovering a row dismisses that row's BAND.
//
// BOTH RETURN whether the dismissed-set actually changed, and the caller owes
// SettingsHud::markSettingsDirty() when it did. This is the whole of "both
// persist": nothing else in the panel is touched when a player merely opens a tab
// or moves the pointer across a row, so with no mark the file is never rewritten
// and every dismissal is lost at exit -- the markers come back on the next launch,
// forever. Returning the bool rather than marking from in here keeps this
// translation unit free of SettingsManager, which is also what lets the marker
// table be readable from a test with no settings loaded.
bool dismissTab(int tabId);
bool dismissRow(int tabId, const char* rowTooltipId);

// Persistence, through SettingsManager's global section. The stored form is the
// dismissed markers' "<tab>:<rowTooltipId>" keys, comma-separated, plus a "T"
// prefix for a tab-level dismissal. Unknown keys are kept on load and written
// back out: a file written by a NEWER build names markers this one has never
// heard of, and dropping them would re-show that build's tags on the next
// downgrade-and-upgrade round trip.
std::string serialize();
void deserialize(const std::string& csv);

}  // namespace WhatsNew
