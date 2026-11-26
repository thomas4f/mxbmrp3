// ============================================================================
// core/asset_manager_decks.cpp
// AssetManager's deck layouts (themes/<name>/decks.ini, decks-slanted.ini):
// the ini reader, discovery with base inheritance, and the getters. The
// geometry it fills is hud/deck_layout.h; Cabinet Dark's decks.ini documents
// every key for a layout author.
// ============================================================================
#include "asset_manager.h"
#include "asset_manager_internal.h"
#include "font_config.h"
#include "layout_config.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iterator>

namespace {

using namespace DeckLayout;

// Up to `max` numbers from "x, y, w, h" (commas and/or spaces); how many were read.
int parseNumbers(const char* text, float* out, int max) {
    int n = 0;
    const char* p = text;
    while (p && *p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p) break;
        if (!std::isfinite(v)) break;   // "nan", "inf", 1e40: not a place on a deck
        out[n++] = v;
        p = end;
    }
    return n;
}

// A palette slot by name, or #rrggbb.
bool parseColor(const char* text, Color& out) {
    const int slot = colorSlotFromName(text);
    if (slot >= 0) {
        out.slot = slot;
        out.set = true;
        return true;
    }
    unsigned long abgr = 0;
    if (PluginUtils::parseRgbHex(text, abgr)) {
        out.slot = -1;
        out.abgr = static_cast<uint32_t>(abgr);
        out.set = true;
        return true;
    }
    return false;
}

template <size_t N>
int indexOfName(const char* const (&names)[N], const char* name) {
    for (size_t i = 0; i < N; ++i) {
        if (PluginUtils::iequals(name, names[i])) return static_cast<int>(i);
    }
    return -1;
}

// "speed-box-3" -> deck DECK_SPEED, element "box", number 3; "speed-box-plate"
// -> the same with label "plate" and number 0. Element "" for [speed].
bool splitSection(const char* section, size_t len, int& deck, char* element, size_t elementSize, int& number,
                  char* label, size_t labelSize) {
    deck = -1;
    for (int d = 0; d < DECK_COUNT; ++d) {
        const size_t n = std::strlen(kDeckNames[d]);
        if (len >= n && std::strncmp(section, kDeckNames[d], n) == 0 && (len == n || section[n] == '-')) {
            deck = d;
            section += n;
            len -= n;
            break;
        }
    }
    if (deck < 0) return false;
    element[0] = '\0';
    label[0] = '\0';
    number = 0;
    if (len == 0) return true;
    ++section;   // the '-'
    --len;
    size_t i = 0;
    while (i < len && section[i] != '-' && i + 1 < elementSize) {
        element[i] = section[i];
        ++i;
    }
    element[i] = '\0';
    if (i >= len || section[i] != '-') return true;
    const char* rest = section + i + 1;
    const size_t restLen = len - i - 1;
    bool digits = restLen > 0;
    for (size_t k = 0; k < restLen; ++k) digits = digits && rest[k] >= '0' && rest[k] <= '9';
    if (digits) {
        // Read only the restLen digits, saturating: atoi overflows (undefined)
        // on a long run of digits, and reads past restLen
        number = 0;
        for (size_t k = 0; k < restLen; ++k) number = std::min(number * 10 + (rest[k] - '0'), 99999);
    } else {
        // Too long for a place's name: kept whole in `label` to warn with, and
        // no place is given (number stays 0)
        snprintf(label, labelSize, "%.*s", static_cast<int>(restLen), rest);
        if (restLen >= static_cast<size_t>(SECTION_NAME)) number = -1;
    }
    return true;
}

// Art by name, in DeckLayout::Art order.
constexpr const char* kArtNames[] = { "solid", "panel", "card", "button", "none", "image", "icon", "title", "control" };
static_assert(std::size(kArtNames) == ART_CONTROL + 1, "one name per DeckLayout::Art");

void warnKey(const DeckAsset& d, const char* key, const char* raw, const char* what) {
    DEBUG_WARN_F("Deck '%s': '%s = %s' %s - ignored", d.name.c_str(), key, raw ? raw : "", what);
}

// A section's place: its number, or for a name the place a section of that
// name already has (a skin changing its base's), else the next one after
// every element before it. 0: no place (warned).
int placeFor(const DeckAsset& layout, const char* key, const char* value, const char* element, int number,
             const char* label, char (*names)[SECTION_NAME], int& count, int max) {
    char range[48];
    snprintf(range, sizeof(range), "is past the %d %ss a deck holds", max, element);
    if (number < 0) {
        warnKey(layout, key, value, "is a longer name than a section takes (23 characters)");
        return 0;
    }
    if (!label[0]) {
        if (number < 1 || number > max) {
            warnKey(layout, key, value, range);
            return 0;
        }
        count = std::max(count, number);
        return number;
    }
    for (int i = 0; i < count; ++i) {
        if (PluginUtils::iequals(names[i], label)) return i + 1;
    }
    if (count >= max) {
        warnKey(layout, key, value, range);
        return 0;
    }
    snprintf(names[count], SECTION_NAME, "%s", label);
    return ++count;
}

// A field this deck fills, by name: -1 (warned) for a name that is not a
// field, or a field the deck never fills, with the ones it does.
int deckField(const DeckAsset& layout, int deckId, const char* key, const char* text) {
    const int f = indexOfName(kFieldNames, text);
    if (f >= 0 && deckFills(deckId, f)) return f;
    char list[512];
    size_t used = 0;
    list[0] = '\0';
    for (int i = 0; i < FIELD_COUNT && used < sizeof(list); ++i) {
        if (!deckFills(deckId, i)) continue;
        const int n = snprintf(list + used, sizeof(list) - used, "%s%s", used ? ", " : "", kFieldNames[i]);
        if (n < 0) break;
        used += static_cast<size_t>(n);
    }
    DEBUG_WARN_F("Deck '%s': '%s = %s' %s - ignored (the %s deck fills: %s)", layout.name.c_str(), key, text,
                 f < 0 ? "is not a field name" : "is a field this deck never fills", kDeckNames[deckId], list);
    return -1;
}

// A meter this deck draws, by name, the same way.
int deckMeter(const DeckAsset& layout, int deckId, const char* key, const char* text) {
    const int m = indexOfName(kMeterNames, text);
    if (m >= 0 && deckHasMeter(deckId, m)) return m;
    char list[128];
    size_t used = 0;
    list[0] = '\0';
    for (int i = 0; i < METER_COUNT && used < sizeof(list); ++i) {
        if (!deckHasMeter(deckId, i)) continue;
        const int n = snprintf(list + used, sizeof(list) - used, "%s%s", used ? ", " : "", kMeterNames[i]);
        if (n < 0) break;
        used += static_cast<size_t>(n);
    }
    DEBUG_WARN_F("Deck '%s': '%s = %s' %s - ignored (the %s deck draws: %s)", layout.name.c_str(), key, text,
                 m < 0 ? "is not a meter name" : "is a meter this deck never draws", kDeckNames[deckId],
                 list[0] ? list : "no meter");
    return -1;
}

// A colour key on any element: true when `prop` named one (handled, warned on a bad value).
bool colorKey(DeckAsset& layout, const char* key, const char* prop, const char* text,
              std::initializer_list<std::pair<const char*, Color*>> slots) {
    for (const auto& s : slots) {
        if (std::strcmp(prop, s.first) == 0) {
            if (!parseColor(text, *s.second)) warnKey(layout, key, text, "is not a colour name or #rrggbb");
            return true;
        }
    }
    return false;
}

// An image's .tga stem, as named: no path, no extension, short enough to hold.
bool copyImageName(DeckAsset& layout, const char* key, const char* text, char (&out)[DeckLayout::IMAGE_NAME]) {
    const size_t n = text ? std::strlen(text) : 0;
    if (n == 0 || n >= sizeof(out) || std::strpbrk(text, "\\/:.")) {
        warnKey(layout, key, text, "is not an image name (deck_<name>.tga in the theme folder, without deck_ and .tga)");
        return false;
    }
    std::memcpy(out, text, n + 1);
    return true;
}

// Each image a box names, to the sprite the layout registered for it
// (DeckAsset::imageStems). Once per read, never per frame; an unknown name warns
// and draws solid, so a typo shows as a plain box rather than nothing.
void resolveImages(DeckAsset& layout) {
    const auto find = [&](const char* stem, const char* what) {
        const int sprite = stem[0] ? layout.imageSprite(stem) : 0;
        if (sprite == 0)
            DEBUG_WARN_F("Deck '%s': %s image '%s' has no %s.tga in the theme folder - drawn solid",
                         layout.name.c_str(), what, stem, stem);
        return sprite;
    };
    for (Deck& d : layout.geometry.decks) {
        for (Box& b : d.boxes) {
            if (b.present) b.sprite = b.art == ART_IMAGE ? find(b.image, "box") : 0;
        }
        for (int i = 0; i < d.textCount; ++i) {
            Text& t = d.texts[i];
            t.pillSprite = t.pillImage[0] ? find(t.pillImage, "pill") : 0;
        }
        for (MeterEl& m : d.meters) {
            m.wellSprite = m.present && m.wellArt == ART_IMAGE ? find(m.wellImage, "well") : 0;
        }
    }
}

// A key's numbers from deck.ini's units to design px (kIniUnits).
void toPx(const char* element, const char* prop, float* v, int n) {
    for (int i = 0; i < n; ++i) v[i] *= iniScale(element, prop, i);
}

bool applyBoxKey(DeckAsset& layout, int deckId, Box& b, const char* key, const char* prop, float value, const char* text,
                 bool numeric) {
    if (std::strcmp(prop, "rect") == 0) {
        if (parseNumbers(text, b.rect, 4) != 4) warnKey(layout, key, text, "needs four numbers: x, y, w, h");
        else toPx("box", prop, b.rect, 4);
        b.hasCorners = false;
        return true;
    }
    if (std::strcmp(prop, "corners") == 0) {
        if (parseNumbers(text, b.corners, 8) == 8) {
            toPx("box", prop, b.corners, 8);
            b.hasCorners = true;
        } else warnKey(layout, key, text, "needs eight numbers: the top-left, bottom-left, bottom-right and top-right x, y");
        return true;
    }
    if (std::strcmp(prop, "skew") == 0 && numeric) { b.skew = value; return true; }
    if (std::strcmp(prop, "opacity") == 0 && numeric) { b.opacity = std::clamp(value, 0.0f, 1.0f); return true; }
    if (std::strcmp(prop, "tint") == 0 && numeric) { b.tint = value != 0.0f; return true; }
    if (std::strcmp(prop, "pad") == 0 && numeric) { b.pad = value; return true; }
    if (std::strcmp(prop, "border") == 0 && numeric) { b.border = std::max(value, 0.0f); return true; }
    if (std::strcmp(prop, "border-opacity") == 0 && numeric) { b.borderOpacity = std::clamp(value, 0.0f, 1.0f); return true; }
    if (std::strcmp(prop, "row") == 0 && numeric) { b.row = value != 0.0f; return true; }
    if (std::strcmp(prop, "glow") == 0 && numeric) { b.glow = std::max(value, 0.0f); return true; }
    if (std::strcmp(prop, "caps") == 0 && numeric) { b.caps = value != 0.0f; return true; }
    if (std::strcmp(prop, "uncapped") == 0 && numeric) { b.uncapped = value != 0.0f; return true; }
    // `none` clears a field a base layout set (a skin's box that does not hug)
    if (std::strcmp(prop, "hug") == 0 || std::strcmp(prop, "hug-to") == 0) {
        const bool none = PluginUtils::iequals(text, "none");
        const int f = none ? static_cast<int>(F_NONE) : deckField(layout, deckId, key, text);
        if (none || f >= 0) (prop[3] ? b.hugTo : b.hug) = static_cast<Field>(f);
        return true;
    }
    if (std::strcmp(prop, "art") == 0) {
        const int art = indexOfName(kArtNames, text);
        if (art >= 0) b.art = static_cast<Art>(art);
        else warnKey(layout, key, text, "is not solid, panel, card, button, title, none, image, icon or control");
        return true;
    }
    if (std::strcmp(prop, "control") == 0) {
        const int c = indexOfName(kControlNames, text);
        if (c >= 0) {
            b.control = static_cast<Control>(c);
            b.art = ART_CONTROL;
        } else {
            warnKey(layout, key, text, "is not shuffle, prev, play, next, loop or volume");
        }
        return true;
    }
    if (std::strcmp(prop, "image") == 0) {
        if (!copyImageName(layout, key, text, b.image)) return true;
        b.art = ART_IMAGE;
        return true;
    }
    if (std::strcmp(prop, "when") == 0) {
        const bool none = PluginUtils::iequals(text, "none");
        const int f = none ? static_cast<int>(F_NONE) : deckField(layout, deckId, key, text);
        if (none || f >= 0) b.when = static_cast<Field>(f);
        return true;
    }
    return colorKey(layout, key, prop, text, { { "color", &b.color }, { "border-color", &b.borderColor } });
}

bool applyTextKey(DeckAsset& layout, int deckId, Text& t, const char* key, const char* prop, float value, const char* text,
                  bool numeric) {
    if (std::strcmp(prop, "show") == 0) {
        const int f = deckField(layout, deckId, key, text);
        if (f >= 0) t.field = static_cast<Field>(f);
        return true;
    }
    if (numeric) {
        if (std::strcmp(prop, "x") == 0) { t.x = value; return true; }
        if (std::strcmp(prop, "y") == 0) { t.y = value; return true; }
        if (std::strcmp(prop, "h") == 0) { t.h = value; return true; }
        if (std::strcmp(prop, "size") == 0) { t.size = value; return true; }
        if (std::strcmp(prop, "fit") == 0) { t.fit = value; return true; }
        if (std::strcmp(prop, "tail") == 0) { t.tail = std::clamp(value, 0.0f, 1.0f); return true; }
        if (std::strcmp(prop, "pills") == 0) { t.pills = value != 0.0f; return true; }
        if (std::strcmp(prop, "pill-opacity") == 0) { t.pillOpacity = std::clamp(value, 0.0f, 1.0f); return true; }
        if (std::strcmp(prop, "pill-tint") == 0) { t.pillTint = std::clamp(value, 0.0f, 1.0f); return true; }
        if (std::strcmp(prop, "pill-pad") == 0) { t.pillPad = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "pill-gap") == 0) { t.pillGap = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "pill-h") == 0) { t.pillH = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "pill-border") == 0) { t.pillBorder = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "pill-border-opacity") == 0) { t.pillBorderOpacity = std::clamp(value, 0.0f, 1.0f); return true; }
        if (std::strcmp(prop, "stack") == 0) { t.stack = std::clamp(static_cast<int>(value), 0, DeckLayout::MAX_PILLS - 1); return true; }
        if (std::strcmp(prop, "stack-lean") == 0) { t.stackLean = value; return true; }
        if (std::strcmp(prop, "row") == 0) { t.row = value != 0.0f; return true; }
    }
    if (std::strcmp(prop, "pill-image") == 0) {
        copyImageName(layout, key, text, t.pillImage);
        return true;
    }
    if (std::strcmp(prop, "after") == 0 || std::strcmp(prop, "before") == 0) {
        const int f = deckField(layout, deckId, key, text);
        if (f >= 0) (prop[0] == 'a' ? t.after : t.before) = static_cast<Field>(f);
        return true;
    }
    if (std::strcmp(prop, "arrow") == 0) {
        if (PluginUtils::iequals(text, "up")) t.arrow = ARROW_UP;
        else if (PluginUtils::iequals(text, "down")) t.arrow = ARROW_DOWN;
        else if (PluginUtils::iequals(text, "none")) t.arrow = ARROW_NONE;
        else warnKey(layout, key, text, "is not up, down or none");
        return true;
    }
    if (std::strcmp(prop, "align") == 0) {
        if (PluginUtils::iequals(text, "left")) t.align = ALIGN_LEFT;
        else if (PluginUtils::iequals(text, "center") || PluginUtils::iequals(text, "centre")) t.align = ALIGN_CENTER;
        else if (PluginUtils::iequals(text, "right")) t.align = ALIGN_RIGHT;
        else warnKey(layout, key, text, "is not left, center or right");
        return true;
    }
    if (std::strcmp(prop, "font") == 0) {
        // A category, never a face: the face is the player's pick for it, else
        // the layout's [fonts] one, so a font chosen in Appearance reaches the decks
        const int category = fontCategoryFromName(text);
        if (category >= 0) {
            t.fontCategory = category;
        } else {
            warnKey(layout, key, text, "is not a font category (title, normal, strong, digits, marker, small); "
                                       "[fonts] names the layout's face for each (the Title font is used)");
            t.fontCategory = static_cast<int>(FontCategory::TITLE);
        }
        return true;
    }
    return colorKey(layout, key, prop, text, {
        { "color", &t.color }, { "empty-color", &t.emptyColor }, { "hot-color", &t.hotColor },
        { "good-color", &t.goodColor }, { "bad-color", &t.badColor }, { "best-color", &t.bestColor },
        { "pill-color", &t.pillColor }, { "pill-border-color", &t.pillBorderColor } });
}

bool applyMeterKey(DeckAsset& layout, int deckId, MeterEl& m, const char* key, const char* prop, float value,
                   const char* text, bool numeric) {
    if (std::strcmp(prop, "show") == 0) {
        const int i = deckMeter(layout, deckId, key, text);
        if (i >= 0) m.meter = static_cast<Meter>(i);
        return true;
    }
    if (std::strcmp(prop, "kind") == 0) {
        static constexpr const char* kKinds[] = { "bar", "blocks", "arc", "trace", "column" };
        const int k = indexOfName(kKinds, text);
        if (k >= 0) m.kind = static_cast<MeterKind>(k);
        else warnKey(layout, key, text, "is not bar, blocks, arc, trace or column");
        return true;
    }
    if (std::strcmp(prop, "rect") == 0) {
        if (parseNumbers(text, m.rect, 4) != 4) warnKey(layout, key, text, "needs four numbers: x, y, w, h");
        else toPx("meter", prop, m.rect, 4);
        return true;
    }
    if (std::strcmp(prop, "arc") == 0) {
        if (parseNumbers(text, m.arc, 4) != 4) {
            warnKey(layout, key, text, "needs four numbers: centre x, centre y, radius, thickness");
        } else {
            toPx("meter", prop, m.arc, 4);
        }
        return true;
    }
    if (numeric) {
        if (std::strcmp(prop, "count") == 0) { m.count = std::clamp(static_cast<int>(value), 0, 48); return true; }
        if (std::strcmp(prop, "gap") == 0) { m.gapAsWritten = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "skew") == 0) { m.skew = value; return true; }
        if (std::strcmp(prop, "from") == 0) { m.from = value + ARC_FROM_UP; return true; }
        if (std::strcmp(prop, "sweep") == 0) { m.sweep = value; return true; }
        if (std::strcmp(prop, "dim-opacity") == 0) { m.dimOpacity = std::clamp(value, 0.0f, 1.0f); return true; }
        if (std::strcmp(prop, "mid") == 0) { m.mid = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "mark") == 0) { m.mark = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "over") == 0) { m.over = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "needle") == 0) { m.needle = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "needle-in") == 0) { m.needleIn = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "line") == 0) { m.line = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "well-pad") == 0) { m.wellPad = std::max(value, 0.0f); return true; }
        if (std::strcmp(prop, "knob") == 0) { m.knob = std::max(value, 0.0f); return true; }
    }
    if (std::strcmp(prop, "well") == 0) {
        const int art = indexOfName(kArtNames, text);
        if (art >= 0 && art != ART_IMAGE && art != ART_ICON && art != ART_CONTROL) m.wellArt = static_cast<Art>(art);
        else warnKey(layout, key, text, "is not solid, panel, card, button, title or none (an image: well-image)");
        return true;
    }
    if (std::strcmp(prop, "well-image") == 0) {
        if (copyImageName(layout, key, text, m.wellImage)) m.wellArt = ART_IMAGE;
        return true;
    }
    return colorKey(layout, key, prop, text, {
        { "color", &m.color }, { "dim-color", &m.dimColor }, { "hot-color", &m.hotColor },
        { "zone-color", &m.zoneColor }, { "good-color", &m.goodColor }, { "bad-color", &m.badColor },
        { "mid-color", &m.midColor }, { "needle-color", &m.needleColor }, { "danger-color", &m.dangerColor },
        { "well-color", &m.wellColor }, { "knob-color", &m.knobColor } });
}

// [fonts] <category> = <font>: the layout's face for a category, as a theme's
// [fonts] gives one, used where the player has not picked their own. A skin
// starts from its base's and states only the ones it changes; `theme` drops one.
void setLayoutFont(DeckAsset& layout, const char* key, const char* category, const char* font) {
    static_assert(DeckLayout::FONT_CATEGORIES == static_cast<int>(FontCategory::COUNT),
                  "one layout face per font category");
    const int c = fontCategoryFromName(category);
    if (c < 0) {
        warnKey(layout, key, category, "is not a font category (title, normal, strong, digits, marker, small)");
        return;
    }
    if (PluginUtils::iequals(font, "theme")) {   // a skin dropping its base's: the theme's face
        layout.geometry.fonts[c] = 0;
        return;
    }
    const int index = AssetManager::getInstance().getFontIndexByName(font);
    if (index <= 0) {
        warnKey(layout, key, font, "is not an installed font (the theme's is used)");
        layout.geometry.fonts[c] = 0;
        return;
    }
    layout.geometry.fonts[c] = index;
}

bool applyDeckKey(DeckAsset& layout, const char* key, float value, const char* raw, bool numeric) {
    if (std::strcmp(key, PackIni::kKeyName) == 0) {
        if (raw && *raw) layout.displayName = raw;
        return true;
    }
    if (std::strcmp(key, PackIni::kKeyBase) == 0) {
        if (raw && *raw) layout.baseName = raw;
        return true;
    }
    if (std::strncmp(key, "fonts.", 6) == 0) {
        setLayoutFont(layout, key, key + 6, raw ? raw : "");
        return true;
    }

    const char* dot = std::strrchr(key, '.');
    if (!dot) return false;
    int deckId = -1, number = 0;
    char element[16];
    char label[64];
    if (!splitSection(key, static_cast<size_t>(dot - key), deckId, element, sizeof(element), number, label,
                      sizeof(label))) {
        return false;
    }
    const char* prop = dot + 1;
    Deck& d = layout.geometry.decks[deckId];
    const char* text = raw ? raw : "";
    // A one-number length in design px from here on; a key of several
    // numbers scales them as it parses them (toPx)
    value *= iniScale(element, prop, 0);

    if (element[0] == '\0') {
        if (std::strcmp(prop, "size") == 0) {
            float wh[2] = {};
            if (parseNumbers(text, wh, 2) == 2) {
                toPx("", prop, wh, 2);
                d.width = std::max(wh[0], 0.0f);
                d.height = std::max(wh[1], 0.0f);
            } else {
                warnKey(layout, key, text, "needs two numbers: width, height");
            }
            return true;
        }
        if (numeric && std::strcmp(prop, "rows") == 0) { d.rows = std::clamp(static_cast<int>(value), 0, MAX_ROWS); return true; }
        if (numeric && std::strcmp(prop, "row-pitch") == 0) { d.rowPitch = std::max(value, 0.0f); return true; }
        if (numeric && std::strcmp(prop, "row-break") == 0) { d.rowBreak = std::max(value, 0.0f); return true; }
        if (numeric && std::strcmp(prop, "row-lean") == 0) { d.rowLean = value; return true; }
        return false;
    }

    if (std::strcmp(element, "box") == 0) {
        const int at = placeFor(layout, key, text, element, number, label, layout.geometry.names.box[deckId],
                                d.boxCount, MAX_BOXES);
        if (at == 0) return true;
        Box& b = d.boxes[at - 1];
        b.present = true;
        return applyBoxKey(layout, deckId, b, key, prop, value, text, numeric);
    }
    if (std::strcmp(element, "text") == 0) {
        const int at = placeFor(layout, key, text, element, number, label, layout.geometry.names.text[deckId],
                                d.textCount, MAX_TEXTS);
        if (at == 0) return true;
        Text& t = d.texts[at - 1];
        t.present = true;
        return applyTextKey(layout, deckId, t, key, prop, value, text, numeric);
    }
    if (std::strcmp(element, "meter") == 0) {
        const int at = placeFor(layout, key, text, element, number, label, layout.geometry.names.meter[deckId],
                                d.meterCount, MAX_METERS);
        if (at == 0) return true;
        MeterEl& m = d.meters[at - 1];
        m.present = true;
        return applyMeterKey(layout, deckId, m, key, prop, value, text, numeric);
    }
    return false;
}

}  // namespace

void AssetManager::readDeckIni(const std::string& iniPath, DeckAsset& deck) {
    // A hand-edited file: one bad line must not abort the asset scan. BOTH
    // handlers, for the reason readPitboardIni states.
    try {
        layoutForEachIniPairRaw(iniPath, [](const char* key, float parsed, const char* rawValue,
                                            bool numeric, void* raw) -> bool {
            DeckAsset& d = *static_cast<DeckAsset*>(raw);
            if (!applyDeckKey(d, key, parsed, rawValue, numeric)) {
                DEBUG_WARN_F("Deck '%s': unknown key '%s' - ignored (cabinet-dark's decks.ini "
                             "lists every key)", d.name.c_str(), key);
            }
            return true;
        }, &deck);
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Deck '%s': failed to read '%s': %s", deck.name.c_str(), iniPath.c_str(), e.what());
    } catch (...) {
        DEBUG_WARN_F("Deck '%s': failed to read '%s'", deck.name.c_str(), iniPath.c_str());
    }
    DeckLayout::finalize(deck.geometry);
}

// The two layout files a theme folder may hold (DeckAsset::slanted)
const char* AssetManager::deckIniName(bool slanted) { return slanted ? "decks-slanted.ini" : "decks.ini"; }

// A theme's deck layouts: themes/<name>/decks.ini (Regular) and decks-slanted.ini
// (Slanted). Two phases, as discoverPitboards(): layouts with no base, then
// skins over them, chained (a skin starts from its base's geometry, so it states
// only what it changes). A layout's `base` names another theme, whose layout of
// the same style it builds on; in decks-slanted.ini `base = regular` is this
// theme's own Regular layout (Slant over Cabinet).
void AssetManager::discoverDecks() {
    const std::string themesRoot = std::string(DISCOVERY_DIR) + "\\" + THEMES_SUBDIR + "\\";
    struct Found { std::string theme, ini, base; bool slanted; };
    std::vector<Found> found;
    for (const ThemeAsset& theme : m_themes) {
        for (const bool slanted : { false, true }) {
            const std::string ini = themesRoot + theme.name + "\\" + deckIniName(slanted);
            if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            found.push_back({ theme.name, ini, readPackStringKey(ini, PackIni::kKeyBase), slanted });
        }
    }

    // Deck images sit past the gauges block -- one contiguous run per type,
    // appended in the order setupDefaultResources registers them.
    int spriteIndex = m_firstIconSpriteIndex + static_cast<int>(m_icons.size());
    for (const ThemeAsset& t : m_themes) spriteIndex += static_cast<int>(t.spriteFiles.size());
    spriteIndex += static_cast<int>(m_gamepads.size()) * GamepadSprite::COUNT;
    spriteIndex += static_cast<int>(m_pitboards.size()) * PitboardSprite::COUNT;
    spriteIndex += static_cast<int>(m_gauges.size()) * GaugeSprite::COUNT;
    // The pictures the layout names (`art = image`, a well, a pill): each the
    // first deck_<name>.tga found in its own theme's folder, then down its base
    // chain; in name order. Only those named, so a theme holding both layouts
    // registers Slant's pictures once, for Slant.
    const auto addImages = [&](DeckAsset& deck) {
        std::vector<std::string> named;
        const auto want = [&](const char* stem) { if (stem[0]) named.emplace_back(stem); };
        for (const Deck& d : deck.geometry.decks) {
            for (const Box& b : d.boxes) if (b.present && b.art == ART_IMAGE) want(b.image);
            for (int i = 0; i < d.textCount; ++i) want(d.texts[i].pillImage);
            for (const MeterEl& m : d.meters) if (m.present && m.wellArt == ART_IMAGE) want(m.wellImage);
        }
        std::sort(named.begin(), named.end());   // the same sprite order on every machine
        named.erase(std::unique(named.begin(), named.end()), named.end());
        for (const std::string& stem : named) {
            const std::string file = "deck_" + stem;
            int w = 0, h = 0;
            std::string from;
            for (const DeckAsset* at = &deck; at && from.empty();) {
                if (readTgaDimensions(themesRoot + at->name + "\\" + file + ".tga", w, h)) from = at->name;
                const DeckAsset* next = at->baseName.empty() ? nullptr : findDeck(at->baseName, at->slanted);
                if (!next && at->slanted && at->baseName == DECK_BASE_REGULAR) next = findDeck(at->name, false);
                at = next == at ? nullptr : next;
            }
            if (from.empty()) continue;   // resolveImages warns and draws it solid
            if (deck.imageStems.empty()) deck.firstImage = spriteIndex;
            deck.imageStems.push_back(stem);
            deck.imageBlank.push_back(w == 1 && h == 1 ? 1 : 0);
            deck.imagePaths.push_back(getThemePath(from, file.c_str()));
            ++spriteIndex;
        }
    };
    // Each theme's own picture for an image the layout has (DeckAsset::themedImages):
    // deck_<stem>.tga in the theme's folder, else its base theme's; registered
    // after the layout's own, theme by theme in discovery order. The layout's
    // own theme draws its own pictures: nothing to add.
    const auto addThemedImages = [&](DeckAsset& deck) {
        const size_t own = deck.imageStems.size();
        deck.themedImages.assign(m_themes.size() * own, 0);
        int w = 0, h = 0;
        for (size_t ti = 0; ti < m_themes.size(); ++ti) {
            const ThemeAsset& theme = m_themes[ti];
            for (size_t k = 0; k < own; ++k) {
                const std::string file = "deck_" + deck.imageStems[k];
                std::string from;
                if (readTgaDimensions(themesRoot + theme.name + "\\" + file + ".tga", w, h)) from = theme.name;
                else if (!theme.baseName.empty() &&
                         readTgaDimensions(themesRoot + theme.baseName + "\\" + file + ".tga", w, h)) from = theme.baseName;
                if (from.empty()) continue;
                if (getThemePath(from, file.c_str()) == deck.imagePaths[k]) continue;   // the layout's own picture
                if (w == 1 && h == 1) { deck.themedImages[ti * own + k] = -1; continue; }   // hidden
                if (from != theme.name) {   // the base theme's picture: share its sprite
                    int shared = 0;         // (bases precede skins in m_themes)
                    for (size_t bi = 0; bi < ti && !shared; ++bi) {
                        if (m_themes[bi].name == from) shared = deck.themedImages[bi * own + k];
                    }
                    if (shared > 0) { deck.themedImages[ti * own + k] = shared; continue; }
                }
                deck.themedImages[ti * own + k] = spriteIndex++;
                deck.imagePaths.push_back(getThemePath(from, file.c_str()));
            }
        }
    };
    const auto add = [&](const Found& f, const DeckAsset* baseLayout) {
        DeckAsset deck;
        deck.name = f.theme;
        deck.slanted = f.slanted;
        deck.baseName = f.base;
        if (baseLayout) deck.geometry = baseLayout->geometry;   // the base's drawing first; the layout overrides
        readDeckIni(f.ini, deck);
        addImages(deck);
        addThemedImages(deck);
        resolveImages(deck);
        DEBUG_INFO_F("AssetManager: Found %s deck layout in theme '%s'", f.slanted ? "Slanted" : "Regular", f.theme.c_str());
        m_decks.push_back(std::move(deck));
    };
    std::vector<Found> skins;
    for (const Found& f : found) {
        if (f.base.empty()) add(f, nullptr);
        else skins.push_back(f);
    }
    // Then skins, a pass at a time: a skin over a skin (Carbon Light over Carbon
    // Dark over Cabinet) waits until its base is in. A pass that adds none ends
    // it; what is left names a base that is missing, or a loop.
    for (bool added = true; added && !skins.empty();) {
        added = false;
        for (size_t s = 0; s < skins.size();) {
            const Found& f = skins[s];
            const DeckAsset* baseLayout = f.slanted && f.base == DECK_BASE_REGULAR ? findDeck(f.theme, false)
                                                                                  : findDeck(f.base, f.slanted);
            if (!baseLayout) { ++s; continue; }
            add(f, baseLayout);
            skins.erase(skins.begin() + static_cast<std::ptrdiff_t>(s));
            added = true;
        }
    }
    for (const Found& f : skins) {
        DEBUG_WARN_F("AssetManager: theme '%s' %s skipped - its base '%s' has no such layout, or the bases loop",
                     f.theme.c_str(), deckIniName(f.slanted), f.base.c_str());
    }

    DEBUG_INFO_F("AssetManager: Found %zu deck layout(s)", m_decks.size());
}

// RELOAD_CONFIG re-reads each layout over its base's freshly re-read geometry
// (bases come first in m_decks, as discovery appended them). The pictures, like
// the base binding, are discovery-time; only the layout is re-read.
void AssetManager::reloadDeckLayouts() {
    const std::string themesRoot = std::string(DISCOVERY_DIR) + "\\" + THEMES_SUBDIR + "\\";
    for (DeckAsset& deck : m_decks) {
        deck.geometry = DeckLayout::DeckGeometry{};
        if (!deck.baseName.empty()) {
            const bool own = deck.slanted && deck.baseName == DECK_BASE_REGULAR;
            for (const DeckAsset& candidate : m_decks) {
                if (&candidate == &deck) break;
                if (candidate.name == (own ? deck.name : deck.baseName) && candidate.slanted == (own ? false : deck.slanted)) {
                    deck.geometry = candidate.geometry;
                    break;
                }
            }
        }
        readDeckIni(themesRoot + deck.name + "\\" + deckIniName(deck.slanted), deck);
        resolveImages(deck);
    }
}

int DeckAsset::themedSprite(int themeIndex, int sprite) const {
    const int own = static_cast<int>(imageStems.size());
    const int k = sprite - firstImage;
    if (own == 0 || k < 0 || k >= own) return sprite;
    if (themeIndex < 0) return imageBlank[static_cast<size_t>(k)] ? -1 : sprite;
    const size_t at = static_cast<size_t>(themeIndex) * static_cast<size_t>(own) + static_cast<size_t>(k);
    if (at < themedImages.size() && themedImages[at] != 0) return themedImages[at];
    return imageBlank[static_cast<size_t>(k)] ? -1 : sprite;
}

int DeckAsset::imageSprite(const char* stem) const {
    for (size_t k = 0; k < imageStems.size(); ++k)
        if (PluginUtils::iequals(imageStems[k].c_str(), stem)) return firstImage + static_cast<int>(k);
    return 0;
}

const DeckAsset* AssetManager::findDeck(const std::string& theme, bool slanted) const {
    for (const DeckAsset& d : m_decks) {
        if (d.slanted == slanted && d.name == theme) return &d;
    }
    return nullptr;
}

const DeckAsset* AssetManager::getThemeDeck(const ThemeAsset& theme, bool slanted) const {
    if (const DeckAsset* own = findDeck(theme.name, slanted)) return own;
    if (const DeckAsset* base = theme.baseName.empty() ? nullptr : findDeck(theme.baseName, slanted)) return base;
    if (const DeckAsset* shipped = findDeck(DEFAULT_DECK_THEME, slanted)) return shipped;
    for (const DeckAsset& d : m_decks) {
        if (d.slanted == slanted) return &d;
    }
    return m_decks.empty() ? nullptr : &m_decks.front();
}
