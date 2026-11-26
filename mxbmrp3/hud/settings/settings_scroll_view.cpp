// ============================================================================
// hud/settings/settings_scroll_view.cpp
// The settings panel's scrolling lists: the layout half (beginScroll ..
// endScroll, which draws a list and its scrollbar) and the input half (the
// thumb, the track, the wheel, and the glide between rows). The wheel is
// core/mouse_wheel.h's, which hands over only the ticks it kept for the list
// under the cursor. The geometry and timing are scroll_view.h's; the why is
// there too.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../digit_roll.h"
#include "../clip_marks.h"
#include "../hud_scroll.h"   // HudScroll::drawBar, every list's bar
#include "../../core/color_config.h"
#include "../../core/ui_config.h"
#include "../../core/input_manager.h"
#include "../../core/mouse_wheel.h"

#include <algorithm>

using namespace PluginConstants;

namespace {
// The bar's hit strip (ScrollView::HIT_CHARS) reaches this far past each side
// of the drawn bar (ScrollView::BAR_CHARS), in character cells.
constexpr float HIT_SLOP_CELLS = (ScrollView::HIT_CHARS - ScrollView::BAR_CHARS) * 0.5f;
// The thumb is never shorter than this many rows, so a long list's is still
// something to grab.
constexpr float MIN_THUMB_LINES = 1.0f;

// A settings list's moves, for the shared input core (ScrollView::press /
// follow): over its rowTops, at least STEP_LINES a step, a dragged thumb
// resting on the nearest row.
struct ListMoves {
    ScrollView::State& s;
    long long nowUs;
    Motion::Level level;
    bool head(float to) {
        if (std::fabs(to - s.target) <= ScrollView::EPS) return false;
        ScrollView::glideTo(s, to, nowUs, level);
        return true;
    }
    bool step(int dir) { return head(ScrollView::step(s.rowTops, s.maxRest, s.target, dir)); }
    bool page(int dir) { return head(ScrollView::page(s.rowTops, s.maxRest, s.target, s.viewLines, dir)); }
    bool dragTo(float lines) { return head(ScrollView::snap(s.rowTops, s.maxRest, lines)); }
    void dragEnd() { ScrollView::glideTo(s, ScrollView::snap(s.rowTops, s.maxRest, s.dragLines), nowUs, level); }
};
}  // namespace

// THE FRAME a list is drawn in (hud/clip_marks.h): the view, and in a carded
// list the cards' pads around it too -- the bare section's own box -- so a
// row and its card leave at the same line. A list without cards (the release
// notes) never reaches into the heading above it.
static ClipMarks::Rect scrollFrame(float top, float viewH, bool carded, float padT, float padB) {
    return ClipMarks::rows(top - (carded ? padT : 0.0f), top + viewH + (carded ? padB : 0.0f));
}

void SettingsLayoutContext::beginScroll(ScrollView::Id id, float viewLines) {
    ScrollView::State& s = parent->m_scrolls[id];
    m_scroll = &s;
    m_scrollId = id;
    m_scrollTop = currentY;
    if (static_cast<int>(id) == listFill.list) viewLines += listFill.lines;
    m_scrollViewH = viewLines * lineHeightNormal;
    m_scrollOffset = s.shown * lineHeightNormal;
    m_scrollMoving = s.gliding || s.dragging;
    // The Find index reads every row, wherever the list stands.
    m_scrollCull = !parent->m_find.indexing;
    m_scrollJump = -1.0f;
    m_rowOpen = false;
    m_groupCount = 0;
    s.rowTops.clear();
    s.viewLines = viewLines;
    s.lineH = lineHeightNormal;
    m_scrollRowRight = labelX + rowSpanWidth();
}

bool SettingsLayoutContext::scrollRow(float lines) {
    closeScrollRow();
    if (!m_scroll) return true;
    const float top = currentY;
    m_scroll->rowTops.push_back((top - m_scrollTop) / lineHeightNormal);
    const bool carded = m_groupCount > 0 && m_groups[m_groupCount - 1].quad >= 0;
    if (lines >= 0.0f && m_groupCount > 0) {
        ScrollGroup& g = m_groups[m_groupCount - 1];
        g.top = std::min(g.top, top - m_scrollOffset);
        g.bot = std::max(g.bot, top + lines * lineHeightNormal - m_scrollOffset);
    }
    // A row wholly outside the frame is not drawn at all.
    if (lines >= 0.0f && m_scrollCull) {
        const float h = lines * lineHeightNormal;
        const ClipMarks::Rect box = scrollFrame(m_scrollTop, m_scrollViewH, carded, parent->cardPadTopY(),
                                                parent->cardPadBotY());
        if (ClipMarks::outside(ClipMarks::rows(top - m_scrollOffset, top + h - m_scrollOffset), box, ScrollView::EPS)) {
            currentY += h;
            return false;
        }
    }
    m_rowOpen = true;
    m_rowTop = top;
    m_rowQuad = parent->m_quads.size();
    m_rowString = parent->m_strings.size();
    m_rowRegion = parent->m_clickRegions.size();
    return true;
}

void SettingsLayoutContext::scrollToHere() {
    if (m_scroll) m_scrollJump = (currentY - m_scrollTop) / lineHeightNormal;
}

// The open row's primitives, moved up by the list's offset, and handed to
// the frame's edge (hud/clip_marks.h): crossing it, our renderers cut the row
// there and the game fades it out over a third of a line past it -- at rest
// hides it, so a resting list shows no part row in game. A region on a row
// that is not wholly in the frame keeps its index (hover is matched by index)
// but can no longer be hit.
void SettingsLayoutContext::closeScrollRow() {
    if (!m_rowOpen) return;
    m_rowOpen = false;
    const float dy = m_scrollOffset;
    ClipMarks::shiftUp(parent->m_quads, parent->m_strings, m_rowQuad, parent->m_quads.size(), m_rowString,
                       parent->m_strings.size(), dy);
    const bool carded = m_groupCount > 0 && m_groups[m_groupCount - 1].quad >= 0;
    const ClipMarks::Rect box = scrollFrame(m_scrollTop, m_scrollViewH, carded, parent->cardPadTopY(),
                                            parent->cardPadBotY());
    const ClipMarks::Rect row = ClipMarks::rows(m_rowTop - dy, currentY - dy);
    const float a = parent->m_clip.edge(parent->m_quads, parent->m_strings, m_rowQuad, parent->m_quads.size(),
                                        m_rowString, parent->m_strings.size(), row, box,
                                        ScrollView::listEdge(lineHeightNormal, m_scrollMoving));
    const bool whole = a >= 1.0f && ClipMarks::overhang(row, box) <= ClipMarks::INSIDE_EPS;
    for (size_t i = m_rowRegion; i < parent->m_clickRegions.size(); ++i) {
        SettingsHud::ClickRegion& r = parent->m_clickRegions[i];
        r.y -= dy;
        if (!whole) r.width = r.height = 0.0f;
    }
}

void SettingsLayoutContext::scrollGroup() {
    closeScrollRow();
    if (!m_scroll) return;
    // The seam between two cards: the closing card's pad, the gap, the next one's.
    if (m_groupCount > 0) currentY += parent->cardPadBotY() + parent->contentGapY() + parent->cardPadTopY();
    if (m_groupCount >= MAX_SCROLL_GROUPS) return;
    ScrollGroup& g = m_groups[m_groupCount++];
    g.quad = static_cast<int>(parent->m_quads.size());
    g.string = static_cast<int>(parent->m_strings.size());
    g.top = 1e9f;
    g.bot = -1e9f;
    parent->addThemedCard(0.0f, 0.0f, 0.0f, 0.0f, /*reserveOnly=*/true);
    if (static_cast<int>(parent->m_quads.size()) == g.quad) g.quad = -1;   // no card art: air only
}

void SettingsLayoutContext::endScroll() {
    closeScrollRow();
    if (!m_scroll) return;
    ScrollView::State& s = *m_scroll;
    const float contentH = currentY - m_scrollTop;
    s.contentLines = contentH / lineHeightNormal;
    s.maxRest = ScrollView::maxRest(s.rowTops, s.contentLines, s.viewLines);
    // A list that shrank under its rest (a rider left), or a row asked to be
    // the rest: settle there and draw again. Not glided -- nobody moved it.
    const float want = m_scrollJump >= 0.0f ? m_scrollJump : s.target;
    const float settled = ScrollView::snap(s.rowTops, s.maxRest, want);
    if (std::fabs(settled - s.target) > ScrollView::EPS ||
        (m_scrollJump >= 0.0f && std::fabs(settled - s.shown) > ScrollView::EPS)) {
        s.target = s.shown = settled;
        s.gliding = false;
        parent->setDataDirty();
    }

    const float cw = charWidth();
    s.laidOut = true;
    s.viewX = labelX;
    s.viewY = m_scrollTop;
    s.viewH = m_scrollViewH;

    // The bar sits in the panel's right padding, beside the group cards rather
    // than inside them, centred between their edge and the panel's -- the HUDs'
    // rule (hud/hud_scroll.h), so the two look alike under every theme. A
    // themed card's edge never crosses it, it runs straight past the seams
    // between cards, and the cards keep their full width. A padding thinner
    // than the bar takes it flush with the panel's edge. The view reaches it.
    const float pad = labelX - planCardLeftX;
    const float boxR = m_scrollRowRight + pad;
    s.barW = cw * ScrollView::BAR_CHARS;
    s.barX = ScrollView::barLeft(boxR, planPanelRightX, s.barW);
    s.viewW = s.barX + s.barW - labelX;

    // Each group card laid out whole, around every row it has, and handed to
    // the frame (ClipMarks::card): our renderers cut it at the frame like its
    // rows; the game's is clamped there, left open on the side its rows go on.
    // Its fill cover follows what the game draws. A card wholly outside the
    // frame stays the empty reserve it was.
    const ClipMarks::Rect frame = scrollFrame(m_scrollTop, m_scrollViewH, true, parent->cardPadTopY(),
                                              parent->cardPadBotY());
    for (int i = 0; i < m_groupCount; ++i) {
        const ScrollGroup& g = m_groups[i];
        if (g.quad < 0 || g.bot <= g.top) continue;
        const float top = g.top - parent->cardPadTopY(), bot = g.bot + parent->cardPadBotY();
        if (bot <= frame.t || top >= frame.b) continue;
        const size_t q0 = static_cast<size_t>(g.quad), q1 = q0 + NineSlice::SLICE_COUNT;
        if (q1 > parent->m_quads.size()) continue;
        parent->rewriteThemedCard(g.quad, planCardLeftX, top, boxR - planCardLeftX, bot - top);
        const ClipMarks::Rect drawn = parent->m_clip.card(parent->m_quads, q0, q1, static_cast<size_t>(g.string),
                                                          frame, parent->getOffsetX(), parent->getOffsetY());
        auto& covers = parent->m_sectionCards;
        for (size_t k = 0; k < covers.size(); ++k) {
            if (covers[k].firstQuad != g.quad) continue;
            if (drawn.b > drawn.t) covers[k] = { drawn.t, drawn.b, drawn.l, drawn.r, g.quad };
            else covers.erase(covers.begin() + static_cast<std::ptrdiff_t>(k));
            break;
        }
    }
    m_groupCount = 0;

    // The bar, only when there is somewhere to go: an arrow cell at each end
    // (a step a click), a faint track between them and a thumb for the share
    // of the list in view.
    s.thumbH = 0.0f;
    s.trackY = m_scrollTop + lineHeightNormal;
    s.trackH = std::max(0.0f, s.viewH - 2.0f * lineHeightNormal);
    if (s.maxRest > ScrollView::EPS) {
        const float at = s.dragging ? s.dragLines : s.shown;
        float thumbY = 0.0f, thumbH = 0.0f;
        ScrollView::thumb(s.trackH, s.contentLines, s.viewLines, at, s.maxRest,
                          MIN_THUMB_LINES * lineHeightNormal, thumbY, thumbH);
        s.thumbY = s.trackY + thumbY;
        s.thumbH = thumbH;
        // Three regions, one list: the up arrow, the track, the down arrow, so
        // each lights on its own. Each is the whole hit strip, wider than the
        // bar on both sides.
        const float slop = cw * HIT_SLOP_CELLS;
        const float partY[3] = { m_scrollTop, s.trackY, s.trackY + s.trackH };
        const float partH[3] = { lineHeightNormal, s.trackH, lineHeightNormal };
        const int firstRegion = static_cast<int>(parent->m_clickRegions.size());
        for (int i = 0; i < 3; ++i) {
            SettingsHud::ClickRegion part(s.barX - slop, partY[i], s.barW + 2.0f * slop, partH[i],
                                          SettingsHud::ClickRegion::SCROLL_BAR);
            part.steppedIndex = m_scrollId;
            parent->m_clickRegions.push_back(part);
        }
        const int hovered = parent->m_hoveredRegionIndex - firstRegion;
        const ColorConfig& colors = ColorConfig::getInstance();
        const bool noLit[2] = { false, false };
        HudScroll::drawBar(*parent, s, hovered, noLit, colors.getMuted(), colors.getAccent(), fontSize);
    }
    currentY = m_scrollTop + m_scrollViewH;
    m_lastListId = m_scrollId;
    m_lastListEndY = currentY;
    m_scroll = nullptr;
    m_scrollId = -1;
}

// THE SECTION ASKS FOR THE ROOM, which meets the floor; the list then grows by
// what the engine actually gave the section -- that room plus the panel's ceil
// remainder, which the last section absorbs once the floor no longer binds --
// so list and section stay one box. Measured in the engine's arithmetic, as
// the tallest tab is, never summed by hand.
//
// THE CONTENT COLUMN ALONE is measured: the body is as tall as its taller
// column, and the sidebar often is that, so the body's own shortfall would be
// zero while the list still stopped short of the sidebar's bottom. The sidebar
// steps out and back rather than the want being copied.
PanelPlan& SettingsLayoutContext::planFilled(SettingsHud& hud, const ScaledDimensions& dim, PanelWant& want,
                                             int endList, const std::vector<float>& sections,
                                             ScrollView::Fill& fill) {
    fill = ScrollView::Fill();
    std::vector<PanelWant::ColumnWant>& cols = want.bands.front().columns;
    const bool fills = endList >= 0 && !sections.empty() && !cols.empty();
    if (fills) {
        // MEMOISED (SettingsHud::FillMemo): the vector compare and copy reuse
        // their storage, so a hit allocates nothing.
        SettingsHud::FillMemo& memo = hud.m_fillMemo;
        const SettingsHud::MetricsKey key = SettingsHud::MetricsKey::of(dim);
        if (!(memo.key == key) || memo.contentW != cols.back().contentW || memo.captionW != want.captionW ||
            memo.sections != cols.back().sectionH) {
            PanelWant::ColumnWant sidebar = std::move(cols.front());
            cols.erase(cols.begin());
            memo.height = hud.planBodyHeight(dim, want);
            cols.insert(cols.begin(), std::move(sidebar));
            memo.key = key;
            memo.contentW = cols.back().contentW;
            memo.captionW = want.captionW;
            memo.sections = cols.back().sectionH;
        }
        const float spare = want.minBodyH - memo.height;
        if (spare > 0.0f) cols.back().sectionH.back() += spare;
    }
    PanelPlan& plan = hud.planPanel(dim, want);
    if (!fills) return plan;
    const PanelBox::ColumnGeom& content = plan.col(0, cols.size() - 1);
    const float given = content.sections.empty() ? 0.0f : plan.H(content.sections.back().h);
    const float grow = given - sections.back();
    if (grow > 0.0f) fill = ScrollView::Fill{ endList, grow / dim.lineHeightNormal };
    return plan;
}

int SettingsLayoutContext::listAtEnd() const {
    return (m_lastListId >= 0 && std::fabs(currentY - m_lastListEndY) <= ScrollView::EPS * lineHeightNormal)
               ? m_lastListId : -1;
}

void SettingsLayoutContext::addListHeading(const char* text, bool findSection) {
    if (findSection) parent->m_find.setSection(text);   // the Find index names a row's group, as a section's
    parent->addString(text, labelX, currentY, Justify::LEFT,
                      Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), fontSize);
    currentY += lineHeightNormal;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// A press on a list's scrollbar: on an end arrow it steps the list (and keeps
// stepping while held, updateScrolls); on the thumb it is held (the list
// follows the pointer until release); on the track either side it moves a
// view's worth toward the press. True when the press was a scrollbar's.
bool SettingsHud::pressScrollBar(int regionIndex, float cursorY) {
    if (regionIndex < 0 || m_clickRegions[regionIndex].type != ClickRegion::SCROLL_BAR) return false;
    const int id = m_clickRegions[regionIndex].steppedIndex;
    if (id < 0 || id >= ScrollView::COUNT) return true;
    ScrollView::State& s = m_scrolls[id];
    const float y = cursorY - m_fOffsetY;
    ListMoves moves{ s, DigitRoll::nowUs(), UiConfig::getInstance().getMotion() };
    const int part = ScrollView::partAtY(s, y);
    ScrollView::press(s, part, y, moves.nowUs, moves);
    // A half-made Prestige trade does not wait on a button scrolled away from
    // (an arrow or the track moved the list; holding the thumb has not yet).
    if (!s.dragging && m_activeTab == TAB_ACHIEVEMENTS) m_prestigeConfirmed = false;
    setDataDirty();
    return true;
}

// Once a frame while the panel is open: a held thumb, the wheel, and the
// glide. Rebuilds only when a list moved.
void SettingsHud::updateScrolls(float cursorX, float cursorY, bool cursorValid, bool leftHeld) {
    const long long now = DigitRoll::nowUs();
    const Motion::Level motion = UiConfig::getInstance().getMotion();
    const long long fastMs = UiConfig::getInstance().getHoldRepeatFastMs();
    const float x = cursorX - m_fOffsetX, y = cursorY - m_fOffsetY;
    bool moved = false;

    // THE WHEEL (core/mouse_wheel.h): asked for while a list of this build has
    // somewhere to go, kept only for the one under the cursor, on the game
    // window (the companion window gets its own wheel messages).
    InputManager& input = InputManager::getInstance();
    bool scrolls = false;
    int under = -1;
    for (int i = 0; i < ScrollView::COUNT; ++i) {
        const ScrollView::State& s = m_scrolls[i];
        if (!s.laidOut || s.maxRest <= ScrollView::EPS) continue;
        scrolls = true;
        if (under < 0 && cursorValid && input.getActiveSurface() == InputManager::Surface::Game &&
            isPointInRect(x, y, s.viewX, s.viewY, s.viewW, s.viewH))
            under = i;
    }
    const int notches = scrolls ? MouseWheel::getInstance().take(under >= 0) : 0;
    if (notches != 0) {
        ScrollView::State& s = m_scrolls[under];
        ListMoves moves{ s, now, motion };
        moved |= moves.step(-notches);   // a notch away from the player goes up the list
        input.noteCursorActivity();      // scrolling is using the mouse
    }

    // The one input core (ScrollView::follow), with each list's moves
    for (ScrollView::State& s : m_scrolls) {
        ListMoves moves{ s, now, motion };
        moved |= ScrollView::follow(s, x, y, cursorValid, leftHeld, now, motion, fastMs, moves);
    }
    if (!moved) return;
    // A half-made Prestige trade does not wait on a button scrolled away from.
    if (m_activeTab == TAB_ACHIEVEMENTS) m_prestigeConfirmed = false;
    rebuildAndRecord();
}

#if defined(MXBMRP3_TEST_BUILD)
void SettingsHud::testStepScroll(int id, int steps) {
    if (id < 0 || id >= ScrollView::COUNT) return;
    ScrollView::State& s = m_scrolls[id];
    if (!s.laidOut) return;
    ScrollView::glideTo(s, ScrollView::step(s.rowTops, s.maxRest, s.target, steps), DigitRoll::nowUs(),
                        UiConfig::getInstance().getMotion());
    rebuildAndRecord();
}
#endif
