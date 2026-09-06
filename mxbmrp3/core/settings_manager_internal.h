// ============================================================================
// core/settings_manager_internal.h
// Shared internal constants for the SettingsManager translation units
// (settings_manager*.cpp): the file's location and its format version, which
// the writer (settings_manager_save.cpp) stamps and the loader
// (settings_manager.cpp) migrates from. Header-inline so every TU sees one
// definition without ODR conflicts.
// ============================================================================
#pragma once

namespace SettingsInternal {

inline constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
inline constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

// Settings format version - bump this when making incompatible changes
// Version 1: Original format with bitmasks (implicit, no version field)
// Version 2: Named keys instead of bitmasks for columns/rows/elements
// Version 3: String enums instead of integers for all enum settings
// Version 4: Base sections + sparse profile sections (reduced INI size)
// 5: [Colors]/[Fonts] became SPARSE -- only slots the user pinned are written, so
//    absence means "follow the theme". A file at 4 or below wrote all ten colours
//    and all six fonts unconditionally, and the load path pins every key it sees;
//    see the migration in loadSettings().
// 6: the gamepad AND the pit board are chosen by PACK NAME (gamepads/<name>/,
//    pitboards/<name>/) rather than by texture variant index. Files at 5 or below
//    store the old index; the migration in loadSettings() maps the shipped
//    variants onto their pack names.
// 7: Notices and Timing offsetX means the panel's CENTRE, like the Gap Bar and
//    Version, instead of a delta from a centre computed at render time. The
//    migration in loadSettings() adds the anchor in.
// 8: the Radar joins them. Its offsetX meant a LEFT EDGE, so unlike 7 the shift
//    is half the panel's width and depends on the stored scale -- hence its own
//    version: a file already stamped 7 would skip the shift.
inline constexpr int SETTINGS_VERSION = 9;

// The on-disk shape has been stable since v4 (base [HudName] sections + sparse
// [HudName:Profile] overrides). The load dispatch keys off THIS floor, not off
// == SETTINGS_VERSION, so a file written by any version >= this one still loads
// its HUD sections after SETTINGS_VERSION is later bumped. Gating on
// == SETTINGS_VERSION would silently wipe every user's HUD settings the moment
// the version is bumped (a v4 file then matches neither the v4+ nor the v3
// branch and every [HudName] section is skipped). Only bump this floor when
// the base/profile section layout itself changes incompatibly.
inline constexpr int FIRST_BASE_SECTION_VERSION = 4;

}  // namespace SettingsInternal
