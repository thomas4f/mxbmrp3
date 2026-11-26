// ============================================================================
// hud/hud_scroll.cpp
// HudScroll's input and drawing - see hud_scroll.h. The pure half (layout,
// steps, thumb) is header-only so the unit test compiles it alone.
// ============================================================================
#include "hud_scroll.h"

#include "base_hud.h"
#include "clip_marks.h"
#include "digit_roll.h"
#include "../core/asset_manager.h"
#include "../core/hud_manager.h"
#include "../core/i18n.h"
#include "../core/input_manager.h"
#include "../core/mouse_wheel.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/ui_config.h"

#include <cmath>
#include <cstdio>

bool HudScroll::poll(const BaseHud& hud) {
    InputManager& input = InputManager::getInstance();
    bool changed = false;
    m_tookClick = false;
    setClock(DigitRoll::nowUs(), UiConfig::getInstance().getMotion());

    // The pointer coming or going is a rebuild of its own: a HUD may only
    // measure its whole list while the bar can be used (Stream Chat).
    const bool pointerUp = input.isCursorEnabled();
    if (pointerUp != m_pointerUp) {
        m_pointerUp = pointerUp;
        changed = true;
    }

    // Pointer away: nothing to click, nothing drawn, and the list back at its
    // live edge (its home, for a static list).
    if (!pointerUp) {
        m_view.dragging = false;
        m_view.arrowDir = 0;
        changed |= reset();
        changed |= m_shown || m_hoverPart != ScrollView::PART_NONE;
        m_shown = false;
        m_hoverPart = ScrollView::PART_NONE;
        return changed;
    }

    CursorPosition cursor = input.getCursorPosition();
    hud.mapCursorToHudSpace(cursor.x, cursor.y);
    const bool over = cursor.isValid && hud.isPointInBounds(cursor.x, cursor.y);
    // The bar is laid out in build space (pre-offset), like the rows.
    const float x = cursor.x - hud.getOffsetX();
    const float y = cursor.y - hud.getOffsetY();
    const int part = m_shown && cursor.isValid ? ScrollView::partAt(m_view, x, y) : ScrollView::PART_NONE;

    // The one input core (ScrollView::press / follow), with this list's moves
    const MouseButton& left = input.getLeftButton();
    Moves moves{ *this };
    if (part != ScrollView::PART_NONE && left.isClicked() && hud.ownsClick()) {
        m_tookClick = true;
        ScrollView::press(m_view, part, y, m_nowUs, moves);
        changed = true;
    }
    changed |= ScrollView::follow(m_view, x, y, cursor.isValid, left.isPressed, m_nowUs, m_level,
                                  UiConfig::getInstance().getHoldRepeatFastMs(), moves);

    // THE WHEEL (core/mouse_wheel.h): asked for while the list has somewhere to
    // go, kept while the cursor is on the HUD and SHOWING, on the game window --
    // a cursor gone idle over a HUD mid-race leaves the wheel to the game. Only
    // the HUD on top under it takes a tick: never one under the settings panel.
    if (scrollable()) {
        const bool overForWheel = over && input.shouldShowCursor() &&
                                  input.getActiveSurface() == InputManager::Surface::Game &&
                                  HudManager::getInstance().ownsWheel(&hud);
        const int notches = MouseWheel::getInstance().take(overForWheel);
        if (notches != 0) {
            changed |= moves.step(-notches);   // a notch away from the player goes up the list
            input.noteCursorActivity();        // scrolling is using the mouse
        }
    }

    if (part != m_hoverPart) {
        m_hoverPart = part;
        changed = true;
    }
    const bool shown = scrollable() && (over || !m_following || m_view.dragging || m_unseen > 0);
    if (shown != m_shown) {
        m_shown = shown;
        changed = true;
    }
    return changed;
}

void HudScroll::draw(BaseHud& hud, float cardR, float panelR, float top, float height, float rowH,
                     float fontSize) {
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    ScrollView::State& v = m_view;
    v.barW = cw * ScrollView::BAR_CHARS;
    v.barX = ScrollView::barLeft(cardR, panelR, v.barW);
    v.viewY = top;
    v.viewH = height;
    v.lineH = rowH;
    v.trackY = top + rowH;
    v.trackH = std::max(0.0f, height - 2.0f * rowH);
    float thumbY = 0.0f;
    thumb(v.trackH, rowH, thumbY, v.thumbH);
    v.thumbY = v.trackY + thumbY;
    m_note[0] = '\0';
    if (!m_shown || !scrollable()) return;
    if (m_unseen > 0) snprintf(m_note, sizeof(m_note), TR("%d new"), m_unseen);
    // The live edge's arrow is lit while rows wait beyond it.
    const bool lit[2] = { m_unseen > 0 && m_edge == Edge::START, m_unseen > 0 && m_edge == Edge::END };
    drawBar(hud, v, m_hoverPart, lit, hud.getColor(ColorSlot::MUTED), hud.getColor(ColorSlot::ACCENT), fontSize);
}

void HudScroll::drawBar(BaseHud& hud, const ScrollView::State& s, int hoveredPart, const bool litEnd[2],
                        unsigned long muted, unsigned long accent, float fontSize) {
    const unsigned long idle = PluginUtils::applyOpacity(muted, 0.7f);
    const auto solid = [&hud](float x, float y, float w, float h, unsigned long color) {
        if (w <= 0.0f || h <= 0.0f) return;
        SPluginQuad_t q;
        hud.applyOffset(x, y);
        BaseHud::setQuadPositions(q, x, y, w, h);
        q.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;   // solid-quad-exempt: a scrollbar's track and thumb are flat by definition
        q.m_ulColor = color;
        hud.m_quads.push_back(q);
    };
    solid(s.barX, s.trackY, s.barW, s.trackH, PluginUtils::applyOpacity(muted, 0.15f));
    solid(s.barX, s.thumbY, s.barW, s.thumbH,
          (s.dragging || hoveredPart == ScrollView::PART_TRACK) ? accent : idle);

    // The arrows: the dropdown's caret, up and down ("^"/"v" with icons off).
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const int caret = useIcons ? AssetManager::getInstance().getIconSpriteIndex("hud-caret-up") : 0;
    const float cx = s.barX + s.barW * 0.5f;
    for (int end = 0; end < 2; ++end) {
        const int dir = end == 0 ? -1 : 1;
        const bool lit = hoveredPart == end * 2 || s.arrowDir == dir || litEnd[end];
        const float cellY = end == 0 ? s.viewY : s.trackY + s.trackH;
        if (caret > 0) {
            hud.addRotatedSpriteQuad(cx, cellY + s.lineH * 0.5f, fontSize * 0.2f, end == 0 ? 1.0f : -1.0f, 0.0f,
                                     caret, lit ? accent : idle);
        } else if (!useIcons) {
            hud.addString(end == 0 ? "^" : "v", cx, cellY, PluginConstants::Justify::CENTER,
                          PluginConstants::Fonts::getNormal(), lit ? accent : idle, fontSize);
        }
    }
}

void HudScroll::clipRows(BaseHud& hud, size_t q0, size_t q1, size_t s0, size_t s1, float top,
                         float rowH) const {
    const float frac = (m_view.shown - static_cast<float>(drawFirst())) * rowH;
    if (frac <= ScrollView::EPS * rowH || rowH <= 0.0f) return;
    auto& quads = hud.m_quads;
    auto& strings = hud.m_strings;
    const size_t qEnd = std::min(q1, quads.size()), sEnd = std::min(s1, strings.size());
    q0 = std::min(q0, qEnd);
    s0 = std::min(s0, sEnd);
    ClipMarks::shiftUp(quads, strings, q0, qEnd, s0, sEnd, frac);

    // The view's frame, cut only at its top and bottom; a row is the band it
    // was laid out in, moved with it. Positions are in the HUD's placed
    // space, the frame in build space, as every mark is.
    const float oy = hud.getOffsetY();
    const ClipMarks::Rect box = ClipMarks::rows(top, top + static_cast<float>(m_visible) * rowH);
    const ::Edge rule = ScrollView::listEdge(rowH, m_view.gliding);   // the clip rule, not HudScroll::Edge
    const float rowsTop = top - frac;
    const auto rowOf = [&](float y) { return static_cast<int>(std::floor((y - oy - rowsTop) / rowH)); };
    const auto band = [&](int row) {
        const float t = rowsTop + static_cast<float>(row) * rowH;
        return ClipMarks::rows(t, t + rowH);
    };
    // Each run of primitives in one row, quads then strings: the marks keep
    // index order on both arrays, which is what the renderers' cut walks.
    for (size_t a = q0; a < qEnd;) {
        const auto& p = quads[a].m_aafPos;
        const int row = rowOf((p[0][1] + p[1][1]) * 0.5f);
        size_t b = a + 1;
        while (b < qEnd && rowOf((quads[b].m_aafPos[0][1] + quads[b].m_aafPos[1][1]) * 0.5f) == row) ++b;
        hud.m_clip.edge(quads, strings, a, b, s0, s0, band(row), box, rule);
        a = b;
    }
    // A string's y is its text's top, a little under its row's top.
    const float into = rowH * 0.25f;
    for (size_t a = s0; a < sEnd;) {
        const int row = rowOf(strings[a].m_afPos[1] + into);
        size_t b = a + 1;
        while (b < sEnd && rowOf(strings[b].m_afPos[1] + into) == row) ++b;
        hud.m_clip.edge(quads, strings, qEnd, qEnd, a, b, band(row), box, rule);
        a = b;
    }
}
