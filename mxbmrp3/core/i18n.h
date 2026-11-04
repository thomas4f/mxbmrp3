// ============================================================================
// core/i18n.h
// Translating what the plugin shows: TR("English") at the call site, and the
// active language it resolves against.
//
//   ctx.addSectionHeading(TR("Preferences"));
//   snprintf(buf, sizeof(buf), TR("%d laps"), n);           // one value: printf
//   const char* cut = TRC("director", "Cut");               // same English, other sense
//   // TRANSLATORS: max 9 characters (the value box).
//   ctx.addCycleControl(TR("PB scope"), on ? TR("Class") : TR("Bike"), ...);
//
// THE ENGLISH STAYS IN THE CODE. It is both what the player sees with no
// language picked and the key a language pack translates (core/i18n_catalog.h
// says why gettext .po). A string with no translation, a missing pack and a
// broken pack all show the English -- per string, never all-or-nothing, which
// is what the tooltips.json dropped in #178 got wrong.
//
// COST. Each TR() call site owns one static slot holding the resolved pointer
// and the language generation it was resolved for. Steady state is one integer
// compare and a pointer return: no hash, no allocation, no formatting, so TR()
// is fine inside a rebuild that runs every frame (Map's). After a language
// change the generation moves and each site re-resolves ONCE, on its next call.
// The slot is constant-initialised, so there is no static-init guard either.
//
// RULES (convention, reviewed; see the PR series in design/i18n.md):
//   - Game thread only. A background worker passes data or an enum and the
//     text is resolved where it is drawn.
//   - Only literals: xgettext (tools/i18n/pot.sh) reads TR's argument from the
//     source. A string kept in a table marks itself with N_("...") and is
//     translated where it is shown, with I18n::tr(ptr).
//   - A sentence with two or more values uses {name} placeholders, not printf:
//     word order differs between languages and MSVC's snprintf has no %1$s.
//   - Never translate what is stored or matched: INI values, pack names, ids.
//   - Fixed boxes carry a "TRANSLATORS: max N characters" comment; translators
//     see it in Poedit.
// ============================================================================
#pragma once

#include "i18n_catalog.h"
#include "i18n_mark.h"

#include <unordered_map>
#include <string>
#include <vector>

namespace I18n {

// The active catalogue (nullptr = English) and its generation. Inline so a unit
// test can drive TR() without linking the loader in i18n.cpp.
inline Catalog* g_active = nullptr;
inline unsigned int g_generation = 1;

// The translation of `english`, or `english` itself. A hash lookup: for a table
// entry marked N_() at the point it is SHOWN (a rebuild), not for per-frame code
// -- use TR() there.
inline const char* tr(const char* english) {
    if (!g_active || !english) return english;
    const char* t = g_active->find(nullptr, english);
    return t ? t : english;
}
inline const char* trc(const char* context, const char* english) {
    if (!g_active || !english) return english;
    const char* t = g_active->find(context, english);
    return t ? t : english;
}

// The English a shown string was translated from, or `shown` itself when it was
// never translated (English active, or a string that is not ours). The settings
// Find box indexes both, so a player can search the English word a forum post used.
inline const char* englishOf(const char* shown) {
    if (!g_active || !shown) return shown;
    const char* e = g_active->englishOf(shown);
    return e ? e : shown;
}

struct Slot {
    const char* text;
    unsigned int generation;
};

inline const char* resolve(Slot& s, const char* context, const char* english) {
    if (s.generation != g_generation) {
        s.text = context ? trc(context, english) : tr(english);
        s.generation = g_generation;
    }
    return s.text;
}

// A fixed table entry's translation, cached per language like TR(): one hash
// lookup per language change, then an integer compare. `slot` must belong to
// that one entry -- an array of Slots parallel to the table, indexed alike.
// For a HUD rebuild that walks a table of N_() names.
inline const char* trEntry(Slot& slot, const char* english) {
    return resolve(slot, nullptr, english);
}

// A name picked at run time from a fixed set of N_() literals (a switch's
// return, a PluginConstants string): cached by POINTER per language, so the
// steady state is one pointer-keyed lookup -- no key string built, nothing
// allocated after the first sight of each name.
inline const char* trCached(const char* english) {
    if (!g_active || !english) return english;
    static std::unordered_map<const char*, const char*> cache;
    static unsigned int cacheGeneration = 0;
    if (cacheGeneration != g_generation) { cache.clear(); cacheGeneration = g_generation; }
    auto it = cache.find(english);
    if (it != cache.end()) return it->second;
    const char* shown = tr(english);
    cache.emplace(english, shown);
    return shown;
}

// Make `cat` the active language (nullptr = English) and move the generation.
// The previous catalogue must outlive every pointer it handed out; the loader
// keeps them for the session (a language change is rare and small).
inline void activate(Catalog* cat) {
    g_active = cat;
    ++g_generation;
}

// --- The language packs (i18n.cpp; Windows file I/O) -------------------------
//
// A pack is mxbmrp3_data\languages\<folder>\ holding language.ini ([pack] name,
// optional base, optional pseudo) and strings.po. Stored and selected by FOLDER
// name like every pack type; "" is English. An unknown name shows English
// without rewriting the stored value, so restoring the folder restores the choice.
//
// "system" -- the default -- follows Windows' display language: the pack whose
// folder is that language's name ("sv-SE"), else its language part ("sv"),
// else English. The choice is what is stored, never the resolved folder, so a
// pack installed later is picked up without the player touching the menu.

struct LanguageInfo {
    std::string folder;        // what the INI stores
    std::string displayName;   // [pack] name, CP1252; the folder when absent
    bool pseudo = false;       // the developer pseudo-locale
};

inline constexpr const char* kSystemLanguage = "system";

// The installed pack Windows' display language `locale` ("sv-SE") selects:
// an exact folder match, else the language part ("sv"); case-insensitive, the
// pseudo-locale never. "" = none (English). Pure, for the unit test.
inline std::string matchSystemLanguage(const std::string& locale, const std::vector<LanguageInfo>& packs) {
    auto same = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
            const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
            if (x != y) return false;
        }
        return true;
    };
    const std::string lang = locale.substr(0, locale.find('-'));
    for (const std::string& want : { locale, lang }) {
        if (want.empty()) continue;
        for (const LanguageInfo& p : packs) {
            if (!p.pseudo && same(p.folder, want)) return p.folder;
        }
    }
    return std::string();
}

// The pack a language.ini's `base` names, or "" when it names none: only an
// installed pack's FOLDER counts, never a path, and never the pack itself. It was
// joined onto the languages folder as written, so `base = ..\..\x` read any
// strings.po on the disk. Resolved by name the way every art pack's base is.
// Pure, for the unit test.
inline std::string baseFolder(const std::string& base, const std::string& self,
                              const std::vector<LanguageInfo>& packs) {
    if (base.empty() || base == self) return std::string();
    for (const LanguageInfo& p : packs) {
        if (p.folder == base) return p.folder;
    }
    return std::string();
}

// The installed packs, sorted by folder (read at startup and on RELOAD_CONFIG).
const std::vector<LanguageInfo>& languages();
// Re-read the pack list and the active pack from disk (RELOAD_CONFIG).
void reload();
// Select by folder name ("" = English, kSystemLanguage = follow Windows); loads
// it and redraws every HUD.
void setLanguage(const std::string& folder);
// Resolves the selection if nothing has yet: an INI without a language key
// still follows Windows.
void ensureLoaded();
// The selection as stored (a folder, "" or kSystemLanguage).
const std::string& languageFolder();
// The pack actually in effect: what "system" resolved to, "" for English (none
// selected, none matched, not installed, or failed to load).
const std::string& activeFolder();
// The active language's display name ("English" when none or unknown).
const char* languageDisplayName();

}  // namespace I18n

// TR / TRC: a captureless lambda owns the call site's slot. Its argument must be
// a string literal -- see the RULES above.
#define TR(english) \
    ([]() noexcept -> const char* { \
        static ::I18n::Slot i18nSlot_{ nullptr, 0u }; \
        return ::I18n::resolve(i18nSlot_, nullptr, english); }())
#define TRC(context, english) \
    ([]() noexcept -> const char* { \
        static ::I18n::Slot i18nSlot_{ nullptr, 0u }; \
        return ::I18n::resolve(i18nSlot_, context, english); }())
