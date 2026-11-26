// ============================================================================
// hud/deck_hud_draw.cpp
// The one drawer every deck shares: the layout's boxes, meters and texts
// (hud/deck_layout.h) over what the deck gathered (deck_hud_gather.cpp).
// Runs only on a rebuild, never per frame.
//
// METERS. A bar, a row of blocks and an arc of blocks all colour each piece by
// DeckMeter::blockState (deck_meter.h), so the three agree on what is lit, hot
// or in the zone. A bar is cut at every point where its colour can change
// (parts, the zone's edges, the hot point, the value) and each run between two
// cuts is one quad: a few quads for a whole bar, not one per pixel.
// ============================================================================
#include "deck_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>

#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/asset_manager.h"

using namespace PluginConstants;
using namespace DeckLayout;

namespace {
constexpr float PI = 3.14159265358979f;
// A bar meter's cuts: its parts' ends, the zone and hot points (twice for a
// centred meter) and the value's edges.
constexpr int MAX_CUTS = DeckMeter::MAX_PARTS * 2 + 16;
// Where a string's baseline sits, as a share of its font size from the top of
// its em: a tail drops by the size it loses times this to share the baseline.
constexpr float BASELINE_SHARE = 0.8f;
// An `arrow`: the triangle's width, and the room it takes before the text, as
// shares of the text's size (about the figures' cap height, and a space).
constexpr float ARROW_SIZE = 0.34f;
constexpr float ARROW_ADVANCE = 0.5f;
}  // namespace

int DeckHud::themed(int sprite) const {
    return m_drawLayout ? m_drawLayout->themedSprite(m_drawTheme, sprite) : sprite;
}

// A theme's own picture in a theme that bakes its colours (tint = 0) draws as
// painted, at the box's opacity, as its slices do; anything else takes `fill`
unsigned long DeckHud::imageColor(int sprite, int drawn, unsigned long fill) const {
    if (drawn == sprite || activeTheme()->tintable) return fill;
    return PluginUtils::applyOpacity(ColorPalette::WHITE, static_cast<float>((fill >> 24) & 0xFF) / 255.0f);
}

// A style's [fonts] sit where a theme's do: under the player's pick
// (Appearance, or this deck's own), over the theme's. Colours are the
// theme's palette alone: a style has none of its own.
int DeckHud::getFont(FontCategory category) const {
    const int own = m_drawLayout ? m_drawLayout->geometry.fonts[static_cast<int>(category)] : 0;
    if (own <= 0 || hasFontOverride(category) || FontConfig::getInstance().isOverridden(category))
        return BaseHud::getFont(category);
    return own;
}

unsigned long DeckHud::resolve(const Color& c, ColorSlot fallback) const {
    if (!c.set) return getColor(fallback);
    return c.slot >= 0 ? getColor(static_cast<ColorSlot>(c.slot)) : static_cast<unsigned long>(c.abgr);
}

void DeckHud::addCorners(const float* c, unsigned long color, int sprite) {
    if (((color >> 24) & 0xFF) == 0) return;   // nothing to see: no quad to pay for
    float ox = 0.0f, oy = 0.0f;
    applyOffset(ox, oy);
    SPluginQuad_t q;
    for (int k = 0; k < 4; ++k) {
        q.m_aafPos[k][0] = ox + lx(c[k * 2]);
        q.m_aafPos[k][1] = oy + ly(c[k * 2 + 1]);
    }
    q.m_iSprite = sprite;
    q.m_ulColor = color;
    m_quads.push_back(q);
}

namespace {
// The slant-edge wedge for a lean `px` design px wide (tools/edge_gen.py): the
// variant whose alpha ramp spans about one pixel of it at 1080p, 0 = none
// installed. Resolved once: sprites register at startup and never move.
int slantEdgeSprite(float px) {
    static const int sprites[4] = {
        AssetManager::getInstance().getSpriteIndex("slant_edge", 1),
        AssetManager::getInstance().getSpriteIndex("slant_edge", 2),
        AssetManager::getInstance().getSpriteIndex("slant_edge", 3),
        AssetManager::getInstance().getSpriteIndex("slant_edge", 4) };
    const int v = px < 1.5f ? 0 : px < 3.0f ? 1 : px < 6.0f ? 2 : 3;
    return sprites[v] > 0 ? sprites[v] : 0;
}
}  // namespace

// shear = px the top edge is moved right of the bottom (a meter's skew):
// neighbouring parts sheared alike still meet edge to edge. The game draws a
// quad's edges hard, so a sheared SOLID quad is drawn as a core plus a wedge on
// each slanted side in `smooth` (SMOOTH_LEFT / SMOOTH_RIGHT), textured with the
// slant-edge ramp: the lean comes out smooth on every renderer. A side shared
// with a neighbour (parts drawn with no gap) stays hard -- two ramps over one
// edge would let the background through. Each wedge is the one picture (its
// covered half at the bottom right) turned to its side -- turned, never
// mirrored, so the winding the game requires holds. A part narrower than its
// lean stays one quad.
void DeckHud::addRimPx(float l, float t, float r, float b, float k, unsigned long color) {
    k = std::min({ k, (r - l) * 0.5f, (b - t) * 0.5f });
    addRectPx(l, t, r, t + k, color);
    addRectPx(l, b - k, r, b, color);
    addRectPx(l, t + k, l + k, b - k, color);
    addRectPx(r - k, t + k, r, b - k, color);
}

void DeckHud::addRectPx(float l, float t, float r, float b, unsigned long color, int sprite, float shear,
                           unsigned smooth) {
    if (r <= l || b <= t) return;
    const float lean = std::fabs(shear);
    const int edge = sprite == SpriteIndex::SOLID_COLOR && lean > 0.0f && smooth != 0 && r - l > lean
                   ? slantEdgeSprite(lx(lean) * 1920.0f) : 0;   // its px at 1080p
    if (edge <= 0) {
        const float c[8] = { l + shear, t, l, b, r, b, r + shear, t };   // TL, BL, BR, TR
        addCorners(c, color, sprite);
        return;
    }
    // A wedge's box, its corners TL, BL, BR, TR listed from the picture's TL on:
    // with shear > 0 the left side is the picture as painted and the right
    // turned half round; shear < 0 turns them a quarter each way.
    const auto wedge = [&](float x0, float x1, int turn) {
        const float box[8] = { x0, t, x0, b, x1, b, x1, t };
        float c[8];
        for (int k = 0; k < 4; ++k) {
            c[k * 2] = box[((k + turn) % 4) * 2];
            c[k * 2 + 1] = box[((k + turn) % 4) * 2 + 1];
        }
        addCorners(c, color, edge);
    };
    const bool left = (smooth & SMOOTH_LEFT) != 0, right = (smooth & SMOOTH_RIGHT) != 0;
    // The core: upright on a smooth side (its wedge fills the lean), sheared on a hard one
    const float inL = shear > 0.0f ? l + lean : l, inR = shear > 0.0f ? r : r - lean;
    const float core[8] = { left ? inL : l + shear, t, left ? inL : l, b, right ? inR : r, b, right ? inR : r + shear, t };
    if (left) wedge(shear > 0.0f ? l : l - lean, shear > 0.0f ? l + lean : l, shear > 0.0f ? 0 : 1);
    addCorners(core, color, sprite);
    if (right) wedge(shear > 0.0f ? r : r - lean, shear > 0.0f ? r + lean : r, shear > 0.0f ? 2 : 3);
}

namespace {
// Where a `tail` starts: the first '/' or letter after the first character
// ("3/22" -> "/22", "2.4 s" -> "s"), or the end.
int tailStart(const char* s) {
    for (int i = 1; s[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '/' || std::isalpha(c)) return i;
    }
    return static_cast<int>(std::strlen(s));
}

// The head's own length: a tail sits tight against it ("2.4 s" -> "2.4"+"s")
int headLength(const char* s, int at) {
    while (at > 1 && s[at - 1] == ' ') --at;
    return at;
}
}  // namespace

void DeckHud::drawDeck(const Deck& deck) {
    drawPass(deck, /*rows=*/false);
    if (deck.rows <= 0 || m_rows.rowCount <= 0) return;
    // A list deck: the row template once per row, the row's strings in the
    // ROW_ fields and the template moved down to the row, so a row is drawn
    // like any other element (fit, hug, when, tint). On a rebuild only.
    Deck row = deck;
    // A row sliding to a new place (rowShift) sits between rows: the pitch
    // and the break are crossed in proportion, so a slide over the break
    // moves evenly rather than jumping it at the end.
    const auto offsetAt = [&deck, this](float slot) {
        const int breakAt = m_rows.rowBreakAt;
        const float pastBreak = breakAt >= 0 ? std::clamp(slot - static_cast<float>(breakAt - 1), 0.0f, 1.0f) : 0.0f;
        return slot * deck.rowPitch + pastBreak * deck.rowBreak;
    };
    const size_t rowsQuad = m_quads.size(), rowsString = m_strings.size();
    bool sliding = false;
    for (int r = 0; r < m_rows.rowCount; ++r) {
        for (int f = 0; f < ROW_FIELD_COUNT; ++f) {
            snprintf(m_rows.text[ROW_FIELD_FIRST + f], sizeof(m_rows.text[0]), "%s", m_rows.rowText[r][f]);
            m_rows.tone[ROW_FIELD_FIRST + f] = m_rows.rowTone[r][f];
        }
        const float shift = m_rows.rowShift[r];
        sliding = sliding || shift != 0.0f;
        const float dy = shift != 0.0f ? offsetAt(static_cast<float>(r) + shift) : rowOffset(deck, r, m_rows.rowBreakAt);
        // On a leaning card the row moves along the lean too (row-lean)
        const float dx = -dy * deck.rowLean;
        for (int i = 0; i < deck.boxCount; ++i) {
            for (int k = 0; k < 8; k += 2) row.boxes[i].quad[k] = deck.boxes[i].quad[k] + dx;
            for (int k = 1; k < 8; k += 2) row.boxes[i].quad[k] = deck.boxes[i].quad[k] + dy;
        }
        for (int i = 0; i < deck.textCount; ++i) {
            row.texts[i].x = deck.texts[i].x + dx;
            row.texts[i].y = deck.texts[i].y + dy;
        }
        drawPass(row, /*rows=*/true);
    }
    // A row sliding in from past the list would hang over the title or below
    // the deck: while any slides, the rows go to the edge as one (clip_marks.h)
    // at the area the rows take at rest -- cut by our renderers, faded by the
    // game. At rest nothing is marked.
    if (!sliding) return;
    float top = 0.0f, bottom = 0.0f;
    bool any = false;
    for (int i = 0; i < deck.boxCount; ++i) {
        const Box& b = deck.boxes[i];
        if (!b.present || !b.row) continue;
        const float t = std::min({ b.quad[1], b.quad[3], b.quad[5], b.quad[7] });
        const float u = std::max({ b.quad[1], b.quad[3], b.quad[5], b.quad[7] });
        top = any ? std::min(top, t) : t;
        bottom = any ? std::max(bottom, u) : u;
        any = true;
    }
    if (!any) return;
    bottom += rowOffset(deck, m_rows.rowCount - 1, m_rows.rowBreakAt);
    // A row's chip may hang past the deck's right edge (the penalty): the cut
    // runs to the furthest a row's text may reach, plus the widest hug pad
    float right = deck.width, pad = 0.0f;
    for (int i = 0; i < deck.boxCount; ++i) {
        if (deck.boxes[i].present && deck.boxes[i].row && deck.boxes[i].hug != F_NONE) pad = std::max(pad, deck.boxes[i].pad);
    }
    for (int i = 0; i < deck.textCount; ++i) {
        const Text& t = deck.texts[i];
        if (t.present && t.row && t.align == ALIGN_LEFT && t.fit > 0.0f) right = std::max(right, t.x + t.fit + pad);
    }
    m_clip.frame(rowsQuad, m_quads.size(), rowsString, m_strings.size(),
                 ClipMarks::Rect{ lx(0.0f), ly(top), lx(right), ly(bottom) });
}

void DeckHud::drawPass(const Deck& deck, bool rows) {
    const float bgOpacity = std::clamp(m_fBackgroundOpacity, 0.0f, 1.0f);
    const AssetManager& assets = AssetManager::getInstance();
    // A carousel marks what it draws apart from the art (its readouts) for its
    // name and its steps (finishCarousel): whatever an element added, from
    // here to the end of its turn round the loop
    struct ContentMark {
        DeckHud& d;
        const size_t quads, strings;
        const bool on;
        ContentMark(DeckHud& deckHud, bool content)
            : d(deckHud), quads(deckHud.m_quads.size()), strings(deckHud.m_strings.size()),
              on(content && deckHud.m_carousel) {}
        ~ContentMark() { if (on) d.markContent(quads, strings); }
        ContentMark(const ContentMark&) = delete;
        ContentMark& operator=(const ContentMark&) = delete;
    };
    // Texts are laid out first (fit, tail, arrow: the drawn width), so a box
    // can hug one; then boxes, meters and texts are drawn in that order.
    struct Laid {
        const char* str = nullptr;
        char cut[sizeof(m_rows.text[0])];
        int font = 0;
        float fs = 0.0f;
        float left = 0.0f, right = 0.0f;   // design px, the whole drawn run
    };
    Laid laid[MAX_TEXTS];
    int hugText[FIELD_COUNT];
    for (int& h : hugText) h = -1;
    for (int i = 0; i < deck.textCount; ++i) {
        const Text& t = deck.texts[i];
        Laid& L = laid[i];
        if (!t.present || t.row != rows || t.field < 0 || t.field >= FIELD_COUNT || !m_rows.text[t.field][0]) continue;
        L.str = m_rows.text[t.field];
        L.font = getFont(static_cast<FontCategory>(t.fontCategory));
        L.fs = ly(t.size);
        // A face whose figures stand taller than the band (VT323, Caveat Brush)
        // shrinks until they fit it: the fonts share a digit width, not a height
        const float ink = t.h > 0.0f ? assets.digitInk(L.font) : 0.0f;
        if (ink > 0.0f && ink * L.fs > ly(t.h)) L.fs = ly(t.h) / ink;
        if (t.pills) {
            const float w = drawPills(t, L.font, L.fs, 0, /*draw=*/false);
            L.left = t.align == ALIGN_LEFT ? t.x : t.align == ALIGN_RIGHT ? t.x - w : t.x - w * 0.5f;
            L.right = L.left + w;
            if (hugText[t.field] < 0) hugText[t.field] = i;
            continue;
        }
        // A string wider than its `fit` (a long translation) shrinks, then is cut
        const auto width = [&](const char* str, float fs) {
            if (t.tail <= 0.0f) return assets.textWidth(L.font, str, fs);
            const int at = tailStart(str);
            return assets.textWidth(L.font, str, fs, headLength(str, at)) + assets.textWidth(L.font, str + at, fs * t.tail);
        };
        if (t.fit > 0.0f) {
            const float room = lx(t.fit);
            const float smallest = L.fs * FIT_MIN_SHARE;
            while (L.fs > smallest && width(L.str, L.fs) > room * 1.0001f) L.fs = std::max(smallest, L.fs * 0.9f);
            const int len = static_cast<int>(std::strlen(L.str));
            const int fits = assets.fitChars(L.font, L.str, room, L.fs);
            if (fits < len && width(L.str, L.fs) > room * 1.0001f) {
                snprintf(L.cut, sizeof(L.cut), "%.*s", std::max(fits, 0), L.str);
                L.str = L.cut;
            }
        }
        const float arrowW = t.arrow != ARROW_NONE ? t.size * ARROW_ADVANCE : 0.0f;   // the triangle and its gap
        float w = width(L.str, L.fs) / lx(1.0f) + arrowW;
        // A carousel's title holds the widest of its names, so the dots after
        // it keep one place from deck to deck (the tab with them)
        if (t.field == TITLE && markerWidth(t) > 0.0f) {
            const float widest = widestName(L.font, L.fs);
            w = std::max(w, t.fit > 0.0f ? std::min(widest, t.fit) : widest);
        }
        L.left = t.align == ALIGN_LEFT ? t.x : t.align == ALIGN_RIGHT ? t.x - w : t.x - w * 0.5f;
        if (t.after != F_NONE && hugText[t.after] >= 0 && laid[hugText[t.after]].str) L.left = laid[hugText[t.after]].right + t.x;
        L.right = L.left + w;
        // A carousel's dots run on from its title, inside the tab hugging it
        if (t.field == TITLE) L.right += markerWidth(t);
        if (hugText[t.field] < 0) hugText[t.field] = i;
    }
    // A text placed before another: its whole run ends x short of that one's
    // start, wherever the list has it
    for (int i = 0; i < deck.textCount; ++i) {
        const Text& t = deck.texts[i];
        Laid& L = laid[i];
        if (!L.str || t.before == F_NONE || t.pills) continue;   // pills draw from their own x
        const int to = hugText[t.before];
        if (to < 0 || to == i || !laid[to].str) continue;
        const float w = L.right - L.left;
        L.right = laid[to].left - t.x;
        L.left = L.right - w;
    }

    // Boxes, in order: the first is under the rest.
    for (int i = 0; i < deck.boxCount; ++i) {
        const Box& b = deck.boxes[i];
        if (!b.present || b.row != rows) continue;
        if (b.when != F_NONE && !m_rows.text[b.when][0]) continue;
        // The deck's glow, under the box (and under what the boxes after it draw)
        if (b.glow > 0.0f && m_rows.glow > 0.0f && glowActive()) {
            const ContentMark glow(*this, true);
            const float gx = std::min(b.quad[0], b.quad[2]), gy = b.quad[1];
            addGlow(lx(gx), ly(gy), lx(std::max(b.quad[4], b.quad[6]) - gx), ly(b.quad[3] - gy), ly(b.glow),
                    m_rows.glowColor, m_rows.glow);
        }
        if (b.art == ART_NONE) continue;
        // The deck itself is its first box (the body) and its title's tab;
        // every other box (a divider, a chip, the stamp's plate) is the readouts'
        const bool frame = (i == 0 && !rows) || b.hug == TITLE || b.when == TITLE;
        const ContentMark mark(*this, !frame);
        // The deck's icon: content, so at the box's own opacity, not the deck's
        if (b.art == ART_ICON) {
            if (m_rows.icon <= 0) continue;
            const unsigned long ink = m_rows.iconInk ? m_rows.iconInk : resolve(b.color, ColorSlot::PRIMARY);
            addRectPx(b.quad[0], b.quad[1], b.quad[4], b.quad[5], PluginUtils::applyOpacity(ink, b.opacity), m_rows.icon);
            continue;
        }
        // A Now Playing button: its icon, in the colour the deck gave its state
        if (b.art == ART_CONTROL) {
            if (b.control < 0 || m_rows.controlSprite[b.control] <= 0) continue;
            addRectPx(b.quad[0], b.quad[1], b.quad[4], b.quad[5],
                      PluginUtils::applyOpacity(m_rows.controlInk[b.control], b.opacity), m_rows.controlSprite[b.control]);
            continue;
        }
        unsigned long color = resolve(b.color, b.art == ART_BUTTON ? ColorSlot::ACCENT : ColorSlot::BACKGROUND);
        if (b.tint && b.when != F_NONE)
            color = m_rows.ink[b.when] ? m_rows.ink[b.when] : toneColor(m_rows.tone[b.when], nullptr, color);
        float x = b.quad[0], y = b.quad[1];
        float w = b.quad[4] - b.quad[0], h = b.quad[5] - b.quad[1];
        if (b.hug != F_NONE) {
            const int ti = hugText[b.hug];
            if (ti < 0 || !laid[ti].str) continue;
            x = laid[ti].left - b.pad;
            const int to = b.hugTo != F_NONE ? hugText[b.hugTo] : -1;
            const float right = to >= 0 && laid[to].str ? std::max(laid[to].right, laid[ti].right) : laid[ti].right;
            w = right - laid[ti].left + 2.0f * b.pad;
        }
        const unsigned long fill = PluginUtils::applyOpacity(color, std::clamp(bgOpacity * b.opacity, 0.0f, 1.0f));
        // Theme art needs an upright rect; a leaning box is always solid. An
        // image is upright art with its slant painted in, so a hugging one draws.
        // The theme's slanted frame, where it has one, replaces an upright,
        // unhugged `caps` box's own art and every `uncapped` box (the layout's
        // outline it would double); any other `caps` box keeps its own art
        if (b.uncapped || (b.caps && b.upright && b.hug == F_NONE)) {
            const ThemeAsset* theme = activeTheme();
            if (theme->hasSlantCaps()) {
                // The frame stands in for the deck's panel, so at the panel's
                // opacity: the box's own is the fallback art's
                if (b.caps) addSlantFrame(*theme, x, y, w, h, PluginUtils::applyOpacity(color, bgOpacity));
                continue;
            }
        }
        if ((b.upright || (b.art == ART_IMAGE && b.hug != F_NONE)) && addArt(b.art, b.sprite, x, y, w, h, fill)) {
        } else if (b.hug != F_NONE) {
            addRectPx(x, y, x + w, y + h, fill);
        } else {
            addCorners(b.quad, fill);
        }
        if (b.border > 0.0f && b.upright) {
            const unsigned long rim = PluginUtils::applyOpacity(resolve(b.borderColor, ColorSlot::MUTED),
                                                                std::clamp(bgOpacity * b.borderOpacity, 0.0f, 1.0f));
            addRimPx(x, y, x + w, y + h, b.border, rim);
        }
    }

    // Meters, over the boxes (none in a row), each on its well where it has one:
    // the strip's rect (and its lean) grown by well-pad, in the well's art. Every
    // well first, so a meter sharing a strip (the Inputs traces) is not covered
    const int meters = rows ? 0 : deck.meterCount;
    for (int i = 0; i < meters; ++i) {
        const MeterEl& el = deck.meters[i];
        if (!el.present || el.meter < 0 || el.meter >= METER_COUNT || el.wellArt == ART_NONE || el.kind == METER_ARC) continue;
        if (!m_rows.meter[el.meter].show) continue;
        const float p = el.wellPad;
        const float x = el.rect[0] - p, y = el.rect[1] - p;
        const float w = el.rect[2] + std::fabs(el.skew) + 2.0f * p, h = el.rect[3] + 2.0f * p;
        const unsigned long fill = PluginUtils::applyOpacity(resolve(el.wellColor, ColorSlot::BACKGROUND),
                                                             std::clamp(bgOpacity, 0.0f, 1.0f));
        const ContentMark mark(*this, true);
        if (!addArt(el.wellArt, el.wellSprite, x, y, w, h, fill)) addRectPx(x, y, x + w, y + h, fill);
    }
    for (int i = 0; i < meters; ++i) {
        const MeterEl& el = deck.meters[i];
        if (!el.present || el.meter < 0 || el.meter >= METER_COUNT) continue;
        const DeckMeter::Data& d = m_rows.meter[el.meter];
        if (!d.show) continue;
        const ContentMark mark(*this, true);
        drawMeter(el, d);
    }

    // Texts, over everything
    const int64_t now = DigitRoll::nowUs();
    for (int i = 0; i < deck.textCount; ++i) {
        const Text& t = deck.texts[i];
        const Laid& L = laid[i];
        if (!L.str) continue;
        const ContentMark mark(*this, t.field != TITLE);
        const unsigned long color = m_rows.ink[t.field] ? m_rows.ink[t.field]
                                  : toneColor(m_rows.tone[t.field], &t, resolve(t.color, ColorSlot::PRIMARY));
        if (t.field == TITLE && m_carousel) {
            // What a click steps: the title's run, half its size wider each side
            m_carTitleL = L.left - t.size * 0.5f;
            m_carTitleR = L.right + t.size * 0.5f;
            m_carTitleT = t.y;
            m_carTitleB = t.y + t.h;
            if (markerWidth(t) > 0.0f) drawMarker(t, L.right - markerWidth(t), color);
        }
        const float y = inkCenteredY(ly(t.y), ly(t.h), L.fs);
        if (t.pills) {
            drawPills(t, L.font, L.fs, color, /*draw=*/true);
            continue;
        }
        if (m_kind == Kind::Speed && t.field == GEAR) {
            m_gearRoll.update(L.str, 0, now, color);   // same text: only its colour
            addRolledString(m_gearRoll, lx(t.x), y, static_cast<int>(t.align), L.font, L.fs, ly(t.y), ly(t.y) + ly(t.h));
            continue;
        }
        float x = L.left;
        if (t.arrow != ARROW_NONE) {
            // A triangle the size of the figures' ink, centred on the band
            const float a = t.size * ARROW_SIZE, cy = t.y + t.h * 0.5f, l = x, r = x + a;
            const float top = cy - a * 0.45f, bottom = cy + a * 0.45f, mid = (l + r) * 0.5f;
            const float up[8] = { mid, top, l, bottom, r, bottom, mid, top };
            const float down[8] = { l, top, mid, bottom, mid, bottom, r, top };
            addCorners(t.arrow == ARROW_UP ? up : down, color);
            x += t.size * ARROW_ADVANCE;
        }
        if (t.tail <= 0.0f) {
            addString(L.str, lx(x), y, PluginConstants::Justify::LEFT, L.font, color, L.fs);
            continue;
        }
        // The head, then the tail on the same baseline: smaller, fainter
        const int at = tailStart(L.str);
        char head[sizeof(m_rows.text[0])];
        snprintf(head, sizeof(head), "%.*s", headLength(L.str, at), L.str);
        addString(head, lx(x), y, PluginConstants::Justify::LEFT, L.font, color, L.fs);
        if (L.str[at]) {
            const float tfs = L.fs * t.tail;
            const float tx = lx(x) + assets.textWidth(L.font, head, L.fs);
            addString(L.str + at, tx, y + (L.fs - tfs) * BASELINE_SHARE, PluginConstants::Justify::LEFT, L.font,
                      PluginUtils::applyOpacity(color, TAIL_OPACITY), tfs);
        }
    }
}

// A tone's colour on a text (t) or a tinted box (null): what the layout names,
// else the palette's meaning for it.
unsigned long DeckHud::toneColor(uint8_t tone, const Text* t, unsigned long normal) const {
    switch (tone) {
    case TONE_EMPTY: return t ? resolve(t->emptyColor, ColorSlot::MUTED) : getColor(ColorSlot::MUTED);
    case TONE_HOT:   return t ? resolve(t->hotColor, ColorSlot::NEGATIVE) : getColor(ColorSlot::NEGATIVE);
    case TONE_GOOD:  return t ? resolve(t->goodColor, ColorSlot::POSITIVE) : getColor(ColorSlot::POSITIVE);
    case TONE_BAD:   return t ? resolve(t->badColor, ColorSlot::NEGATIVE) : getColor(ColorSlot::NEGATIVE);
    case TONE_BEST:  return t ? resolve(t->bestColor, ColorSlot::POSITIVE) : getColor(ColorSlot::POSITIVE);
    default:         return normal;
    }
}

// The pills' width in design px (a row's whole run; a stack's newest pill);
// with draw, also their quads and words, each word in its own tone
// (Rows::pillTone) where the deck gave one.
float DeckHud::drawPills(const Text& t, int font, float fs, unsigned long color, bool draw) {
    const AssetManager& assets = AssetManager::getInstance();
    const float unit = lx(1.0f);
    const int n = std::clamp(m_rows.pillCount, 0, MAX_PILLS);
    float width[MAX_PILLS];
    for (int k = 0; k < n; ++k)
        width[k] = assets.textWidth(font, m_rows.pillWord[k], fs) / unit + 2.0f * t.pillPad;
    // A row keeps the newest that fit, oldest dropped first; a stack keeps
    // the newest `stack` whole and the one fading past them
    int first = n;
    float total = 0.0f;
    if (t.stack > 0) {
        first = 0;
        total = n > 0 ? width[n - 1] : 0.0f;
    } else {
        for (int k = n - 1; k >= 0; --k) {
            const float next = total + width[k] + (total > 0.0f ? t.pillGap : 0.0f);
            if (t.fit > 0.0f && next > t.fit) break;
            total = next;
            first = k;
        }
    }
    if (!draw) return total;

    const float bg = m_fBackgroundOpacity;
    const unsigned long base = resolve(t.pillColor, ColorSlot::MUTED);
    const unsigned long rimColor = resolve(t.pillBorderColor, ColorSlot::MUTED);
    const int pill = t.pillSprite > 0 ? themed(t.pillSprite) : SpriteIndex::SOLID_COLOR;
    const int sprite = pill < 0 ? SpriteIndex::SOLID_COLOR : pill;
    const float pitch = t.pillH + t.pillGap;
    float x = t.align == ALIGN_LEFT ? t.x : t.align == ALIGN_RIGHT ? t.x - total : t.x - total * 0.5f;
    for (int k = first; k < n; ++k) {
        // In a stack: lifted above the newest's place (and further right on
        // a lean), fading in from below and out past the last whole one
        float px = x, rise = 0.0f, alpha = 1.0f;
        if (t.stack > 0) {
            const float lift = m_rows.pillLift[k];
            alpha = std::min(std::clamp(lift + 1.0f, 0.0f, 1.0f), std::clamp(static_cast<float>(t.stack) - lift, 0.0f, 1.0f));
            if (alpha <= 0.0f) continue;
            rise = lift * pitch;
            px = (t.align == ALIGN_LEFT ? t.x : t.align == ALIGN_RIGHT ? t.x - width[k] : t.x - width[k] * 0.5f) +
                 t.stackLean * rise;
        }
        const float cy = t.y + t.h * 0.5f - rise;
        const float top = cy - t.pillH * 0.5f, bottom = cy + t.pillH * 0.5f, right = px + width[k];
        const uint8_t tone = m_rows.pillTone[k];
        const bool toned = tone != TONE_NORMAL;
        const unsigned long ink = toned ? toneColor(tone, &t, color) : color;
        // Each layer's opacity, faded with the pill (alpha is already 0..1);
        // the word keeps its ink's own alpha, scaled by the pill's
        const auto layer = [alpha](unsigned long c, float opacity) {
            return PluginUtils::applyOpacity(c, std::clamp(opacity, 0.0f, 1.0f) * alpha);
        };
        addRectPx(px, top, right, bottom, layer(base, bg * t.pillOpacity), sprite);
        if (toned && t.pillTint > 0.0f)
            addRectPx(px, top, right, bottom, layer(ink, bg * t.pillTint), sprite);
        if (t.pillBorder > 0.0f && t.pillSprite <= 0)
            addRimPx(px, top, right, bottom, t.pillBorder, layer(rimColor, bg * t.pillBorderOpacity));
        addString(m_rows.pillWord[k], lx(px + t.pillPad), inkCenteredY(ly(t.y - rise), ly(t.h), fs), PluginConstants::Justify::LEFT,
                  font, PluginUtils::modulateAlpha(ink, PluginUtils::applyOpacity(0, alpha)), fs);
        x += width[k] + t.pillGap;
    }
    return total;
}

// A box's art over an upright rect, design px: the theme's slices or a layout
// image. False where it has none to draw (a solid box, a theme without that
// slice set), so the caller draws its plain fill.
bool DeckHud::addArt(DeckLayout::Art art, int sprite, float x, float y, float w, float h, unsigned long fill) {
    switch (art) {
    case ART_PANEL:
        addBackgroundQuad(lx(x), ly(y), lx(w), ly(h));
        return true;
    case ART_CARD:
        if (!hasThemedCard()) return false;
        addThemedCard(lx(x), ly(y), lx(w), ly(h), false, false);
        return true;
    case ART_TITLE:
        // The theme's title bar (its card where it has none), as its HUDs' band
        return addThemedTitleBand(lx(x), ly(y), lx(w), ly(h));
    case ART_BUTTON:
        return addThemedButton(lx(x), ly(y), lx(w), ly(h), fill);
    case ART_IMAGE: {
        if (sprite <= 0) return false;
        const int drawn = themed(sprite);
        if (drawn < 0) return true;   // hidden (a 1x1 picture): nothing to draw
        addRectPx(x, y, x + w, y + h, imageColor(sprite, drawn, fill), drawn);
        return true;
    }
    default:
        return false;
    }
}

// Left end, the frame's top / centre / bottom stretched across, right end: the
// ends as tall as the rect and as wide as their pictures' shape, the bands as
// thick as the frame's border. Baked art (tint = 0) keeps its colours, at the
// fill's opacity. The bands meet each end edge to edge, as a 9-slice's pieces
// do, never under it: below full opacity an overlap is drawn twice, and showed
// as a dark stripe down each seam on every renderer.
void DeckHud::addSlantFrame(const ThemeAsset& theme, float x, float y, float w, float h, unsigned long fill) {
    const unsigned long color = theme.tintable
        ? fill : PluginUtils::applyOpacity(ColorPalette::WHITE, static_cast<float>((fill >> 24) & 0xFF) / 255.0f);
    const float capL = std::min(h * theme.slantLeftAspect, w * 0.5f);
    const float capR = std::min(h * theme.slantRightAspect, w * 0.5f);
    const float band = std::min(theme.frameBorder * DeckLayout::COLUMN_PX, h * 0.5f);
    const float l = x + capL, r = x + w - capR;
    addRectPx(x, y, l, y + h, color, theme.slantLeftSprite);
    if (r > l) {
        addRectPx(l, y, r, y + band, color, theme.edgeSprites[0]);
        addRectPx(l, y + band, r, y + h - band, color, theme.centerSprite);
        addRectPx(l, y + h - band, r, y + h, color, theme.edgeSprites[1]);
    }
    addRectPx(r, y, x + w, y + h, color, theme.slantRightSprite);
}

void DeckHud::drawMeter(const MeterEl& el, const DeckMeter::Data& d) {
    using DeckMeter::State;
    const float dimShare = std::clamp(el.dimOpacity, 0.0f, 1.0f);
    const unsigned long lit = resolve(el.color, ColorSlot::ACCENT);
    const unsigned long hot = resolve(el.hotColor, ColorSlot::NEGATIVE);
    const unsigned long zone = resolve(el.zoneColor, ColorSlot::POSITIVE);
    const unsigned long danger = resolve(el.dangerColor, ColorSlot::NEGATIVE);
    const unsigned long colors[DeckMeter::STATE_COUNT] = {
        PluginUtils::applyOpacity(resolve(el.dimColor, ColorSlot::MUTED), dimShare),   // DIM
        lit,                                                                            // LIT
        PluginUtils::applyOpacity(hot, dimShare),                                       // HOT_DIM
        hot,                                                                            // HOT_LIT
        PluginUtils::applyOpacity(zone, dimShare),                                      // ZONE_DIM
        zone,                                                                           // ZONE_LIT
        resolve(el.goodColor, ColorSlot::POSITIVE),                                     // GOOD
        resolve(el.badColor, ColorSlot::NEGATIVE),                                      // BAD
        PluginUtils::applyOpacity(danger, dimShare),                                    // DANGER_DIM
        danger,                                                                         // DANGER_LIT
    };
    // Where block i of n starts and ends along the meter: the deck's own parts
    // when the layout leaves the count to it (a sector is as long as it is),
    // else n even blocks.
    const bool byParts = el.count <= 0 && d.parts > 0;
    const int n = std::clamp(el.count > 0 ? el.count : (d.parts > 0 ? d.parts : 10), 1, 48);
    const auto span = [&](int i, float& a, float& b) {
        if (byParts) {
            a = i > 0 ? d.partEnd[i - 1] : 0.0f;
            b = d.partEnd[i];
        } else {
            a = static_cast<float>(i) / static_cast<float>(n);
            b = static_cast<float>(i + 1) / static_cast<float>(n);
        }
    };

    if (el.kind == METER_TRACE) {
        // The meter's history (the Inputs deck's two), the newest at the right;
        // the line leans with the rect, its top `skew` right of its bottom
        const float x = el.rect[0], y = el.rect[1], w = el.rect[2], h = el.rect[3];
        if (w <= 0.0f || h <= 0.0f || el.line <= 0.0f) return;
        const HistoryBuffers& history = PluginData::getInstance().getHistoryBuffers();
        const auto* ring = el.meter == THROTTLE ? &history.throttle : el.meter == FRONT_BRAKE ? &history.frontBrake : nullptr;
        if (!ring) return;
        // It offsets on its own (applyOffset), so the panel-relative rect
        addStripChartHistoryLine(*ring, lit, lx(x), ly(y), lx(w), ly(h), ly(el.line),
                                 HistoryBuffers::MAX_TELEMETRY_HISTORY, lx(el.skew));
        return;
    }

    if (el.kind == METER_COLUMN) {
        // A track the rect's height, filled from the bottom to the value; the
        // fill leans as the track does, so its edges stay on the track's
        const float x = el.rect[0], y = el.rect[1], w = el.rect[2], h = el.rect[3];
        if (w <= 0.0f || h <= 0.0f) return;
        addRectPx(x, y, x + w, y + h, colors[State::DIM], SpriteIndex::SOLID_COLOR, el.skew);
        const float fh = h * std::clamp(d.value, 0.0f, 1.0f);
        if (fh > 0.0f) addRectPx(x, y + h - fh, x + w, y + h, lit, SpriteIndex::SOLID_COLOR, el.skew * fh / h);
        return;
    }

    if (el.kind == METER_BLOCKS) {
        const float x = el.rect[0], y = el.rect[1], w = el.rect[2], h = el.rect[3];
        if (w <= 0.0f || h <= 0.0f) return;
        const float half = el.gap * 0.5f;
        for (int i = 0; i < n; ++i) {
            float a, b;
            span(i, a, b);
            const State s = DeckMeter::blockState(d, a, b, DeckMeter::toneAt(d, (a + b) * 0.5f));
            // Blocks with no gap meet: only the strip's two ends are smoothed
            const unsigned smooth = (i == 0 || half > 0.0f ? SMOOTH_LEFT : 0u) | (i == n - 1 || half > 0.0f ? SMOOTH_RIGHT : 0u);
            addRectPx(x + w * a + (i > 0 ? half : 0.0f), y, x + w * b - (i < n - 1 ? half : 0.0f), y + h, colors[s],
                      SpriteIndex::SOLID_COLOR, el.skew, smooth);
        }
        drawKnob(el, d);
        return;
    }

    if (el.kind == METER_ARC) {
        const float cx = el.arc[0], cy = el.arc[1], r = el.arc[2], th = el.arc[3];
        if (r <= 0.0f || th <= 0.0f) return;
        const float ri = std::max(r - th, 0.0f);
        const float halfGap = el.gap * 0.5f;
        // 0 degrees points left, 90 up, 180 right (y grows down).
        const auto at = [&](float deg, float rad, float& px, float& py) {
            const float a = deg * PI / 180.0f;
            px = cx - std::cos(a) * rad;
            py = cy - std::sin(a) * rad;
        };
        // A block wider than ARC_STEP_DEG is drawn as several, so a long band
        // stays round instead of turning into a chord
        constexpr float ARC_STEP_DEG = 6.0f;
        const float dir = el.sweep < 0.0f ? -1.0f : 1.0f;
        for (int i = 0; i < n; ++i) {
            float a, b;
            span(i, a, b);
            const State s = DeckMeter::blockState(d, a, b, DeckMeter::toneAt(d, (a + b) * 0.5f));
            const float d0 = el.from + el.sweep * a + (i > 0 ? halfGap * dir : 0.0f);
            const float d1 = el.from + el.sweep * b - (i < n - 1 ? halfGap * dir : 0.0f);
            const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(d1 - d0) / ARC_STEP_DEG)), 1, 30);
            for (int k = 0; k < steps; ++k) {
                const float e0 = d0 + (d1 - d0) * static_cast<float>(k) / static_cast<float>(steps);
                const float e1 = d0 + (d1 - d0) * static_cast<float>(k + 1) / static_cast<float>(steps);
                // A backward sweep mirrors the block: swap the radii to keep
                // the winding the game draws (it drops a reversed quad)
                const float ra = dir > 0.0f ? r : ri, rb = dir > 0.0f ? ri : r;
                float c[8];
                at(e0, ra, c[0], c[1]);
                at(e0, rb, c[2], c[3]);
                at(e1, rb, c[4], c[5]);
                at(e1, ra, c[6], c[7]);
                addCorners(c, colors[s]);
            }
        }
        // The needle, across the band at the value (a rotated rectangle: its
        // winding is the same at any angle)
        if (el.needle > 0.0f && d.band) {
            const float pos = d.centered ? 0.5f + std::clamp(d.value, -1.0f, 1.0f) * 0.5f : std::clamp(d.value, 0.0f, 1.0f);
            const float a = (el.from + el.sweep * pos) * PI / 180.0f;
            const float ux = -std::cos(a), uy = -std::sin(a);   // outward
            const float nx = -uy * el.needle * 0.5f, ny = ux * el.needle * 0.5f;
            const float r0 = std::max(ri - el.needleIn, 0.0f), r1 = r + el.over;
            const State ns = DeckMeter::needleState(d);
            const unsigned long col = ns == State::LIT ? resolve(el.needleColor, ColorSlot::PRIMARY)
                                    : ns == State::DIM ? resolve(el.dimColor, ColorSlot::MUTED) : colors[ns];
            const float c[8] = { cx + ux * r1 - nx, cy + uy * r1 - ny, cx + ux * r0 - nx, cy + uy * r0 - ny,
                                 cx + ux * r0 + nx, cy + uy * r0 + ny, cx + ux * r1 + nx, cy + uy * r1 + ny };
            addCorners(c, col);
        }
        return;
    }

    // METER_BAR: one strip, cut where its colour can change.
    const float x = el.rect[0], y = el.rect[1], w = el.rect[2], h = el.rect[3];
    if (w <= 0.0f || h <= 0.0f) return;
    float cuts[MAX_CUTS];
    int nc = 0;
    const auto cut = [&](float f) {
        if (nc < MAX_CUTS && std::isfinite(f) && f > 0.0f && f < 1.0f) cuts[nc++] = f;
    };
    // A scale point (the zone, the hot point) sits at f from the near end, or
    // at f either side of the middle on a centred meter.
    const auto cutScale = [&](float f) {
        if (d.centered) {
            cut(0.5f - f * 0.5f);
            cut(0.5f + f * 0.5f);
        } else {
            cut(f);
        }
    };
    for (int i = 0; i + 1 < d.parts; ++i) cut(d.partEnd[i]);
    if (d.zoneTo > d.zoneFrom) {
        cutScale(d.zoneFrom);
        cutScale(d.zoneTo);
    }
    cutScale(d.hotFrom);
    if (d.centered) {
        cut(0.5f);
        cut(0.5f + d.value * 0.5f);
    } else {
        cut(d.value);
    }
    cuts[nc++] = 1.0f;
    std::sort(cuts, cuts + nc);

    const float half = el.gap * 0.5f / w;
    float a = 0.0f;
    for (int i = 0; i < nc; ++i) {
        const float b = cuts[i];
        if (b - a <= 1e-4f) continue;
        // Gaps only between parts
        const int pa = DeckMeter::partAt(d, a + 1e-4f), pb = DeckMeter::partAt(d, b - 1e-4f);
        const bool partStart = d.parts > 0 && a > 0.0f && DeckMeter::partAt(d, a - 1e-4f) != pa;
        const bool partEnd = d.parts > 0 && b < 1.0f && DeckMeter::partAt(d, b + 1e-4f) != pb;
        const State s = DeckMeter::blockState(d, a, b, DeckMeter::toneAt(d, (a + b) * 0.5f));
        const float l = a + (partStart ? half : 0.0f), r = b - (partEnd ? half : 0.0f);
        // A side another piece meets (a colour change, no gap) stays hard
        const unsigned smooth = (a <= 0.0f || (partStart && half > 0.0f) ? SMOOTH_LEFT : 0u) |
                                (b >= 1.0f || (partEnd && half > 0.0f) ? SMOOTH_RIGHT : 0u);
        addRectPx(x + w * l, y, x + w * r, y + h, colors[s], SpriteIndex::SOLID_COLOR, el.skew, smooth);
        a = b;
    }
    if (!d.centered) {
        drawKnob(el, d);
        return;
    }
    // The middle's tick, then the marks over it, standing `over` past the strip
    const float top = y - el.over, bottom = y + h + el.over;
    if (el.mid > 0.0f) {
        const float cx = x + w * 0.5f;
        addRectPx(cx - el.mid * 0.5f, top, cx + el.mid * 0.5f, bottom, resolve(el.midColor, ColorSlot::PRIMARY),
                  SpriteIndex::SOLID_COLOR, el.skew);
    }
    const float markW = el.mark > 0.0f ? el.mark : h;
    const unsigned long unlit = resolve(el.dimColor, ColorSlot::MUTED);
    // Unlit first, so a lit mark sitting on the same spot draws over it
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < std::min(d.marks, DeckMeter::Data::MAX_MARKS); ++i) {
            if (d.markLit[i] != (pass == 1)) continue;
            const float cx = x + w * (0.5f + std::clamp(d.markAt[i], -1.0f, 1.0f) * 0.5f);
            const float l = std::clamp(cx - markW * 0.5f, x, x + w - markW);
            addRectPx(l, top, l + markW, bottom, d.markLit[i] ? lit : unlit, SpriteIndex::SOLID_COLOR, el.skew);
        }
    }
}

// A strip's knob at the value (bar and blocks): the hint that it drags
void DeckHud::drawKnob(const MeterEl& el, const DeckMeter::Data& d) {
    if (el.knob <= 0.0f || !d.knob) return;
    const float x = el.rect[0], y = el.rect[1], w = el.rect[2], h = el.rect[3];
    const float cx = x + w * std::clamp(d.value, 0.0f, 1.0f);
    const float l = std::clamp(cx - el.knob * 0.5f, x, x + w - el.knob);
    const unsigned long ink = resolve(el.knobColor, ColorSlot::ACCENT);
    addRectPx(l, y - el.over, l + el.knob, y + h + el.over, d.knobHot ? PluginUtils::lightenColor(ink, 0.25f) : ink,
              SpriteIndex::SOLID_COLOR, el.skew);
}
