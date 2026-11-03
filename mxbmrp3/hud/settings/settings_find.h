// ============================================================================
// hud/settings/settings_find.h
// The settings panel's Find box: an index of the settings rows (tab, label,
// tooltip, current value) and the match that turns a typed query into the
// results page. The index holds what each tab DRAWS in its current state: rows
// behind an Off switch, a closed group or another page of a list are not in it.
// Pure: no SettingsHud, no renderer, so the matching is reachable from a unit test.
//
// WHERE THE INDEX COMES FROM. There is no table of rows to search -- every tab
// draws its rows from code. But every labelled row passes one helper,
// SettingsLayoutContext::addRowTooltip(), and the value-drawing helpers draw its
// value right after. So the index is filled by laying every tab out once, with
// the drawing thrown away (SettingsHud::measureTab), while `indexing` is set:
// addRow() takes the row, noteValue() the value the same row draws next. Built
// when the Find box opens, never per frame; matching runs only when the query
// changes.
//
// RANKING. Every word of the query must appear (case- and accent-insensitive). A row whose
// LABEL holds them all is a name hit; one that also needs its tab's or section's
// name ("map scale" -> Map > Scale: most HUD rows are labelled just "Visible",
// "Scale", "Opacity") is a place hit; one that needs its tooltip for any word is a
// description hit. Each tab is also an entry of its own (isTab, labelled with the
// tab's name), so "map" finds the Map tab itself. Tab hits come first, then
// widgets named by the query (the Widgets tab's rows, each one a small HUD), then
// name hits, place hits and description hits, each in index order -- which is
// sidebar order, because the index is filled in it.
//
// Pinned by tests/unit/test_settings_find.cpp.
// ============================================================================
#pragma once

#include "../../core/i18n.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SettingsFind {

struct Entry {
    int tab = -1;
    char label[40] = {};
    // The English the label was translated from (a static literal), or nullptr
    // when it was drawn untranslated. Matched as well as the label, so a player in
    // any language can search the English word a forum post used.
    const char* labelEnglish = nullptr;
    const char* tabName = "";  // the tab's registry name ("Map"): static, outlives the index
    char section[24] = {};     // the section heading the row sits under ("Layout")
    char tooltipId[48] = {};   // the row's own id: lands the jump, shows the hover text
    const char* tooltip = "";  // the tooltip text as shown (TooltipManager), for matching
    // Its English, when a language pack translated it (both are static/session
    // lifetime strings, never a buffer): matched too, like labelEnglish.
    const char* tooltipEnglish = "";
    char value[24] = {};       // what the row showed when indexed; empty = no value drawn
    bool isTab = false;        // the tab itself (label = its name, tooltipId = its tab id)
    bool isWidget = false;     // a widget's row on the Widgets tab
};

// One CP1252 character folded for matching: lower case, and an accented letter
// to its plain one (É, é and e all match), so "ecart" finds "Écart" and a
// keyboard without the accent still finds the row. Labels are CP1252 bytes --
// the font's code page, which every translation is converted to at load.
inline unsigned char foldChar(unsigned char c) {
    if (c < 0x80) return static_cast<unsigned char>(std::tolower(c));
    // 0xC0..0xFF: À..ÿ, upper and lower halves 0x20 apart.
    static const char kLatin1[] =
        "aaaaaaaceeeeiiii"   // C0-CF  ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏ
        "dnooooo*ouuuuyts"   // D0-DF  ÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß
        "aaaaaaaceeeeiiii"   // E0-EF
        "dnooooo/ouuuuyty";  // F0-FF
    if (c >= 0xC0) return static_cast<unsigned char>(kLatin1[c - 0xC0]);
    switch (c) {
        case 0x8A: case 0x9A: return 's';   // Š š
        case 0x8E: case 0x9E: return 'z';   // Ž ž
        case 0x8C: case 0x9C: return 'o';   // Œ œ (first letter)
        case 0x9F: return 'y';              // Ÿ
        default: return c;
    }
}

// Case- and accent-insensitive: does `s` start with `word` (length n)?
inline bool startsWithWord(const char* s, const char* word, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!s[i] || foldChar(static_cast<unsigned char>(s[i])) !=
                     foldChar(static_cast<unsigned char>(word[i]))) {
            return false;
        }
    }
    return true;
}

// Case- and accent-insensitive: does `hay` contain `needle` (length n)?
inline bool containsWord(const char* hay, const char* needle, size_t n) {
    if (n == 0) return true;
    for (const char* h = hay; *h; ++h) {
        if (startsWithWord(h, needle, n)) return true;
    }
    return false;
}

enum class Hit { None, Name, Place, Description };

// How `e` matches `query` (words split on spaces; an empty query matches nothing).
inline Hit classify(const Entry& e, const char* query) {
    bool anyWord = false, needsPlace = false, needsTooltip = false;
    const char* p = query;
    while (*p) {
        while (*p == ' ') ++p;
        const char* w = p;
        while (*p && *p != ' ') ++p;
        const size_t n = static_cast<size_t>(p - w);
        if (n == 0) continue;
        anyWord = true;
        if (containsWord(e.label, w, n)) continue;
        if (e.labelEnglish && containsWord(e.labelEnglish, w, n)) continue;
        if (containsWord(e.tabName, w, n) || containsWord(e.section, w, n)) { needsPlace = true; continue; }
        if (!containsWord(e.tooltip, w, n) &&
            (e.tooltipEnglish == e.tooltip || !containsWord(e.tooltipEnglish, w, n))) return Hit::None;
        needsTooltip = true;
    }
    if (!anyWord) return Hit::None;
    return needsTooltip ? Hit::Description : needsPlace ? Hit::Place : Hit::Name;
}

// How many hits of each kind match() found, in the order they come back.
struct Counts { int tabs = 0, widgets = 0, names = 0, places = 0, descriptions = 0; };

// Fill `hits` with indices into `index`: tabs named by the query, widgets named by
// it, then name hits, place hits and description hits.
inline Counts match(const std::vector<Entry>& index, const char* query, std::vector<int>& hits) {
    hits.clear();
    int counts[5] = {};
    for (int k = 0; k < 5; ++k) {
        for (int i = 0; i < static_cast<int>(index.size()); ++i) {
            const Entry& e = index[i];
            const Hit h = classify(e, query);
            const int tier = h == Hit::Name ? (e.isTab ? 0 : e.isWidget ? 1 : 2)
                           : h == Hit::Place ? 3 : h == Hit::Description ? 4 : -1;
            if (tier == k) { hits.push_back(i); ++counts[k]; }
        }
    }
    return Counts{ counts[0], counts[1], counts[2], counts[3], counts[4] };
}

// The Find box's state on SettingsHud (one member, so the class grows by one line).
struct State {
    std::vector<Entry> index;
    bool indexing = false;      // a measure pass is filling `index`
    bool indexStale = true;     // rebuild before the next results page draws
    int openRow = -1;           // the entry the row being drawn added (noteValue's target)
    const char* tabName = "";   // the tab being drawn (its registry name)
    bool widgetRows = false;    // the tab being drawn is Widgets: its rows are widgets
    char section[24] = {};      // the heading the rows being drawn sit under
    std::string query;          // what the results show
    std::string matchedQuery;   // what `hits` was computed for
    bool matchedFresh = false;  // false = recompute (the index was rebuilt)
    std::vector<int> hits;
    Counts counts;              // how `hits` splits: tabs, widgets, names, places, descriptions
    int page = 0;
    int returnTab = 0;          // the tab the box was opened from (Esc goes back)
    std::string landTooltipId;  // the row a result click opened: lit until the pointer moves
    float landX = 0.0f, landY = 0.0f;

    // From addRowTooltip: a labelled row with an id joins the index.
    void addRow(int tab, const char* label, const char* tooltipId, const char* tooltipText) {
        openRow = -1;
        if (!indexing || !label || !label[0] || !tooltipId || !tooltipId[0]) return;
        Entry e;
        e.tab = tab;
        e.tabName = tabName ? tabName : "";
        e.isWidget = widgetRows;
        std::snprintf(e.label, sizeof(e.label), "%s", label);
        const char* english = I18n::englishOf(label);
        e.labelEnglish = english != label ? english : nullptr;
        std::snprintf(e.section, sizeof(e.section), "%s", section);
        std::snprintf(e.tooltipId, sizeof(e.tooltipId), "%s", tooltipId);
        e.tooltip = tooltipText ? tooltipText : "";
        e.tooltipEnglish = I18n::englishOf(e.tooltip);
        index.push_back(e);
        openRow = static_cast<int>(index.size()) - 1;
    }
    // The tab itself, ahead of its rows. Its value is set from its Visible row
    // once the tab has been laid out (tabVisible()).
    void addTab(int tab, const char* name, const char* tabTooltipId, const char* tooltipText) {
        Entry e;
        e.tab = tab;
        e.isTab = true;
        e.tabName = name ? name : "";
        std::snprintf(e.label, sizeof(e.label), "%s", e.tabName);
        const char* english = I18n::englishOf(e.tabName);
        e.labelEnglish = english != e.tabName ? english : nullptr;
        std::snprintf(e.tooltipId, sizeof(e.tooltipId), "%s", tabTooltipId ? tabTooltipId : "");
        e.tooltip = tooltipText ? tooltipText : "";
        e.tooltipEnglish = I18n::englishOf(e.tooltip);
        index.push_back(e);
    }
    // After a tab's rows: its entry shows what its Visible row shows (a HUD's on/off).
    void tabVisible(size_t tabEntry) {
        for (size_t i = tabEntry + 1; i < index.size(); ++i) {
            if (index[i].tab != index[tabEntry].tab) break;
            // By the ENGLISH label: the translated one is a different word per language.
            const char* english = index[i].labelEnglish ? index[i].labelEnglish : index[i].label;
            if (std::strcmp(english, "Visible") == 0) {
                std::snprintf(index[tabEntry].value, sizeof(Entry::value), "%s", index[i].value);
                return;
            }
        }
    }
    // From addSectionHeading: the rows that follow sit under `title`.
    void setSection(const char* title) {
        if (indexing) std::snprintf(section, sizeof(section), "%s", title ? title : "");
    }
    // From the value-drawing helpers: the value the row addRow() just took shows.
    void noteValue(const char* value) {
        if (!indexing || openRow < 0 || !value) return;
        char* out = index[static_cast<size_t>(openRow)].value;
        std::snprintf(out, sizeof(Entry::value), "%s", value);
        for (size_t n = std::strlen(out); n > 0 && out[n - 1] == ' '; --n) out[n - 1] = '\0';
        openRow = -1;
    }
    // Recompute `hits` if the query or the index moved since the last time.
    void refresh() {
        if (matchedFresh && matchedQuery == query) return;
        if (matchedQuery != query) page = 0;   // a new query starts on its first page
        counts = match(index, query.c_str(), hits);
        matchedQuery = query;
        matchedFresh = true;
    }
};

}  // namespace SettingsFind
