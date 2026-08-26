// ============================================================================
// core/analytics_spotter.h
// The spotter, reduced to ONE analytics label — the same shape as
// analytics_theme.h, for the same reasons.
//
// ONE PROPERTY, NOT A FLAG PLUS A NAME. "Is it on" is not a separate question
// from "what is it speaking": off IS a value, exactly as "none" is for a
// panel theme. So there is no feat_spotter — adoption is `spotter != "none"`.
//
// A pack is a folder in mxbmrp3_data/spotters/ and the setting stores its
// NAME, so the raw value is a string a user typed: shipped packs are ours and
// safe to report verbatim, anything else collapses to "custom" rather than
// shipping a folder name off someone's disk.
//
// THE VALUE IS THE PACK, NOT THE BACKEND. A pack that ships only text is spoken
// by the OS voice and one with recordings plays them, and this property does not
// distinguish them: the question it answers is which VOICE a player chose, and
// `default` is a choice like any other.
//
// There was a "tts" value for "on, no pack", and it could never be produced:
// reloadCuePack() rewrites an empty name to `default` and runs (through
// HudManager::initialize) before analytics samples the property, so the empty
// case does not survive to be reported. It also asked a question this property
// is not for — a Wine/Proton audibility split, which the pack name already
// implies for anyone who needs it.
//
// SUBTITLES ARE NOT IN HERE. This property answers what the player HEARS, and
// a captions-only spotter has no voice; folding it in would make "none"
// ambiguous for the one reading the property exists to give.
//
// Pure and header-only so the unit suite can reach it: analytics_manager.h is
// Win32/WinHTTP-bound and does not link into tests/unit.
// ============================================================================
#pragma once

#include <string>

namespace AnalyticsSpotter {

// The packs that ship in mxbmrp3_data/spotters/, reported by name because
// they are ours. Sorted for readability only; membership is what matters.
//
// Adding a pack folder without adding it here would silently file its users
// under "custom" — so tests/unit/test_analytics_spotter.cpp walks the shipped
// directory and fails on exactly that omission.
// The packs WE publish, which is not the same as the packs we bundle: only
// `default` (text only, spoken by Windows TTS) ships with the plugin, and the
// recorded voices are a separate download. All of them are still ours, so
// reporting them by name is still reporting our own catalogue — the thing this
// list exists to keep out of the data is a folder name off a user's disk.
inline constexpr const char* kPublishedPacks[] = { "default", "af_heart",
                                                 "am_michael", "bm_george" };

inline bool isPublished(const std::string& name) {
    for (const char* s : kPublishedPacks) {
        if (name == s) return true;
    }
    return false;
}

// WHICH VOICE the player chose:
//   spoken audio off       -> "none"
//   on, a pack of ours     -> its name  ("default", "am_michael", ...)
//   on, another installed  -> "custom"  (third-party or hand-recorded)
//   on, named but absent   -> "missing" (stale setting; the base pack speaks)
//
// "missing" is the one value that is not a voice, and it earns its place by
// being a fault rather than a choice: the stored name names a folder that is
// not there, so reporting the name would claim adoption that does not exist and
// "custom" would invent it. What the player actually hears in that case is the
// base pack, which is a different thing from having chosen it.
//
// `installed` is the caller's real lookup, which matches names exactly — so a
// case variant of a shipped name is a different folder, and reporting it as
// "custom" is the truth rather than a near-miss.
inline std::string label(bool enabled, const std::string& packName,
                         bool installed) {
    if (!enabled) return "none";
    if (!installed) return "missing";
    return isPublished(packName) ? packName : "custom";
}

}  // namespace AnalyticsSpotter
