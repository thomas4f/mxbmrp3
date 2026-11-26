// ============================================================================
// hud/hud_scroll.h
// SCROLLBACK FOR A HUD'S LIST: Event Log, Stream Chat, Lap Log (live lists)
// and Records, Friends (static). Each HUD keeps more rows than it shows; this
// lets the player move through them, by whole rows, with the settings lists'
// scrollbar - an arrow cell at each end (a row a click, repeating while held
// on the panel's hold curve), a track (a view's worth toward the press) and a
// thumb to drag. The HUD keeps its size and row count: the bar is drawn in
// the panel's right padding, beside the card (whose content may run to its
// edge: the Lap Log's times do).
//
// POLLED INPUT, like every other HUD click: the bar answers only while
// the pointer is up (settings open, or the Pointer summoned) and is drawn only
// then, while the pointer is over the HUD, or the list is away from its live
// edge, or the thumb is held. Racing, streams and screenshots never see it.
// The wheel steps a list too, a row a notch, while the pointer shows and is
// on the HUD (core/mouse_wheel.h).
//
// FOLLOWING. A live list has a LIVE EDGE, where new rows arrive (the end of
// the display order for oldest-first, the start for newest-first). At that
// edge it follows them; moved away it stays on the rows it shows while new
// ones arrive (counted as unseen, shown on the bar's foot), and it goes back
// to the edge when the player returns there or the pointer goes away - so a
// HUD is never raced with stuck in old history. A static list has no live
// edge but a HOME (its start, or where the HUD wants it to rest), and goes
// back there when the pointer goes away.
//
// POSITIONS ARE DISPLAY ROWS: `first()` is the index, in the HUD's own
// display order, of the top row it rests on. Nothing here allocates; a HUD
// calls layout() from its rebuild and poll() from its update(), and only
// rebuilds when poll() says the bar or the position changed.
//
// ONE MECHANISM WITH THE SETTINGS LISTS (settings/scroll_view.h): the list's
// position, its glide, the bar's input (press, follow) and its drawing
// (drawBar) are theirs; a HUD list only says how far its moves go (Moves:
// whole rows, one a step). A move the player makes glides on Motion's clock, so between rows the HUD draws
// one row more (drawRows()) from the row layout() returns, and clipRows()
// moves them by the fraction and hands each row to the one edge rule
// (hud/clip_marks.h) -- cut at the list's frame on our renderers, faded past
// it on the game's. Rows arriving at the live edge, and the pointer leaving,
// jump: a list that glided every time a chat line came in would never rest.
//
// The pure half (layout, steps, thumb) is pinned by tests/unit/
// test_hud_scroll.cpp; the HUDs' side by tests/integration/tests/
// hud_scroll_test.cpp.
// ============================================================================
#pragma once

#include "settings/scroll_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

class BaseHud;

class HudScroll {
public:
    // Where new rows arrive, in display order.
    enum class Edge : uint8_t { NONE, START, END };

    explicit HudScroll(Edge edge = Edge::NONE) : m_edge(edge) {}

    // A static list's HOME: where it rests until moved, and goes back to when
    // the pointer goes away (Records: its rows around the player's PB). At home
    // the HUD may draw its own composed view rather than a plain window.
    void setHome(int home) { m_home = std::max(0, home); }
    bool atHome() const { return m_following; }

    // A different list in the same HUD (another rider's laps, a new session):
    // forget the old one's length and go to the new one's live edge.
    void restart() {
        m_total = 0;
        m_first = 0;
        m_unseen = 0;
        m_following = true;
        jump();
    }

    // A list whose order the player can flip (newest first / last) moves its
    // live edge with it; the list goes back to that edge.
    void setEdge(Edge edge) {
        if (edge == m_edge) return;
        m_edge = edge;
        m_following = true;
        m_unseen = 0;
        m_first = liveFirst();
        jump();
    }

    // From the HUD's rebuild, before it picks its rows: the list's length in
    // display rows, how many the HUD shows, and how many rows ARRIVED at the
    // live edge since the last call (0 for a static list; may exceed the
    // growth when the HUD's store drops old rows to make room). Returns the
    // first row to DRAW: the resting row, or mid-glide the one the view's top
    // edge crosses -- drawRows() of them from there.
    int layout(int total, int visible, int arrived) {
        const int was = m_first;
        total = std::max(0, total);
        visible = std::max(1, visible);
        arrived = std::max(0, arrived);
        const int grew = total - m_total;
        m_total = total;
        m_visible = visible;
        if (m_following) {
            m_first = liveFirst();
        } else if (arrived > 0 && m_edge != Edge::NONE) {
            // Stay on the same rows: new ones at the start push them down;
            // old ones dropped from the start pull them up.
            if (m_edge == Edge::START) m_first += arrived;
            else m_first -= std::max(0, arrived - std::max(0, grew));
            m_unseen += arrived;
        }
        m_first = std::clamp(m_first, 0, maxFirst());
        m_view.maxRest = static_cast<float>(maxFirst());   // a dragged thumb's reach
        settle();
        // The rows the glide is drawing moved with them; a list following its
        // live edge as rows arrive lands there rather than glides.
        if (m_view.gliding && !m_following && m_first != was) {
            const float d = static_cast<float>(m_first - was);
            m_view.target += d;
            m_view.shown += d;
            m_view.from += d;
        }
        if (!m_view.gliding || (m_following && m_first != was) || m_view.shown < 0.0f ||
            m_view.shown > static_cast<float>(maxFirst()))
            jump();
        return drawFirst();
    }

    int first() const { return m_first; }
    // Where the list is drawn this frame, in rows (fractional mid-glide; read
    // by the unit test), the first row to draw and how many: one more than
    // the view while a row is cut.
    float at() const { return m_view.shown; }
    int drawFirst() const { return std::max(0, static_cast<int>(std::floor(m_view.shown + ScrollView::EPS))); }
    int drawRows() const {
        const int extra = m_view.shown - static_cast<float>(drawFirst()) > ScrollView::EPS ? 1 : 0;
        return std::max(0, std::min(m_total - drawFirst(), m_visible + extra));
    }
    // The row more a glide draws past the view (0 or 1): it is cut there, so
    // whatever the HUD lays out after the list goes where the view ends, as at
    // rest -- every HUD takes it back by this, never by its own count.
    int extraRows() const { return std::max(0, drawRows() - m_visible); }
    bool gliding() const { return m_view.gliding; }   // the unit test's view of the glide
    // Motion's clock and level, for the glide (poll() sets them; a list whose
    // owner never does just lands, as with Motion off).
    void setClock(long long nowUs, Motion::Level level) {
        m_nowUs = nowUs;
        m_level = level;
    }
    // Advance the glide; true while the drawn position moved.
    bool tick() { return ScrollView::tick(m_view, m_nowUs, m_level); }
    int total() const { return m_total; }
    int visible() const { return m_visible; }
    int maxFirst() const { return std::max(0, m_total - m_visible); }
    int unseen() const { return m_unseen; }
    bool following() const { return m_following; }
    bool scrollable() const { return maxFirst() > 0; }
    // Drawn this rebuild (poll() decides; a rebuild draws what it decided).
    bool shown() const { return m_shown; }

    // Moves the list `rows` rows (negative = toward the start), clamped.
    // True when it moved.
    bool step(int rows) { return moveTo(m_first + rows); }
    // A view's worth toward `dir` (-1/+1), less the row kept for context.
    bool page(int dir) { return step(dir * std::max(1, m_visible - 1)); }
    // Back to the live edge (or the start of a static list), unseen cleared;
    // it lands there (the pointer has gone).
    bool reset() {
        const bool moved = moveTo(liveFirst());
        jump();
        const bool hadUnseen = m_unseen != 0;
        m_unseen = 0;
        return moved || hadUnseen;
    }

    // From the HUD's rebuild, after the drawRows() rows it drew -- quads
    // [q0,q1), strings [s0,s1), the first at `top` (build space), `rowH`
    // apart: moves them by the glide's fraction and hands each row to the
    // edge rule at the view's frame. Nothing to do at rest on a whole row.
    void clipRows(BaseHud& hud, size_t q0, size_t q1, size_t s0, size_t s1, float top, float rowH) const;

    // The thumb on a track `trackH` tall (never shorter than `minH`): where it
    // sits and how tall it is. Shared math with the settings lists.
    void thumb(float trackH, float minH, float& y, float& h) const {
        const float at = m_view.dragging ? m_view.dragLines : m_view.shown;
        ScrollView::thumb(trackH, static_cast<float>(m_total), static_cast<float>(m_visible), at,
                          static_cast<float>(maxFirst()), minH, y, h);
    }

    // From the HUD's update(), every frame it is visible: reads the pointer and
    // the left button (InputManager), answers a press on the bar, runs a held
    // arrow or thumb, and decides whether the bar shows. `hud` maps the cursor
    // to its build space and arbitrates the click. True when the HUD must
    // rebuild: the list moved, the bar appeared or went, or a part lit.
    bool poll(const BaseHud& hud);
    // True when this frame's left click was the bar's (the HUD skips its own
    // click handling for it).
    bool tookClick() const { return m_tookClick; }

    // From the HUD's rebuild, after its rows: lays the bar in the padding
    // between the card's right edge `cardRight` and the panel's `panelRight`,
    // from `top` (build space) `height` tall at `rowH` per row, and draws it
    // when shown().
    void draw(BaseHud& hud, float cardRight, float panelRight, float top, float height, float rowH,
              float fontSize);

    // EVERY LIST'S BAR, the HUDs' and the settings lists' alike, from a State
    // laid out in build space: a faint track, the thumb (the accent while held
    // or hovered), and the dropdown's caret at each end ("^"/"v" with icons
    // off), lit while hovered, held, or `litEnd` asks (rows waiting there).
    static void drawBar(BaseHud& hud, const ScrollView::State& s, int hoveredPart, const bool litEnd[2],
                        unsigned long muted, unsigned long accent, float fontSize);
    // "N new" while rows wait beyond the live edge and the bar shows, else "":
    // the HUD puts it on its caption row (addPlanTitleNote). Formatted by
    // draw(), so it costs nothing per frame.
    const char* note() const { return m_note; }

    // A HUD list's moves, for the shared input core (ScrollView::press/follow):
    // whole rows, the thumb resting on the nearest. Public so the unit test
    // drives the list through the moves poll() hands the core.
    struct Moves {
        HudScroll& s;
        bool step(int dir) { return s.step(dir); }
        bool page(int dir) { return s.page(dir); }
        bool dragTo(float rows) { return s.moveTo(static_cast<int>(std::lround(rows))); }
        void dragEnd() {}
    };

private:
    int liveFirst() const {
        return m_edge == Edge::END ? maxFirst() : (m_edge == Edge::START ? 0 : std::min(m_home, maxFirst()));
    }
    // The player's moves: they glide (step, page, the thumb).
    bool moveTo(int to) {
        to = std::clamp(to, 0, maxFirst());
        if (to == m_first) return false;
        m_first = to;
        settle();
        ScrollView::glideTo(m_view, static_cast<float>(m_first), m_nowUs, m_level);
        return true;
    }
    // Everything else lands where the list rests.
    void jump() {
        m_view.target = m_view.shown = m_view.from = static_cast<float>(m_first);
        m_view.gliding = false;
    }
    // Following = at the live edge (at home, for a static list).
    void settle() {
        m_following = m_first == liveFirst();
        if (m_following) m_unseen = 0;
    }

    Edge m_edge;
    int m_first = 0;
    int m_total = 0;
    int m_visible = 1;
    int m_unseen = 0;
    int m_home = 0;
    bool m_following = true;
    ScrollView::State m_view;   // in rows: target == m_first; the bar's input and layout
    long long m_nowUs = 0;
    Motion::Level m_level = Motion::Level::OFF;

    // Input state, from poll().
    bool m_pointerUp = false;
    bool m_shown = false;
    bool m_tookClick = false;
    int m_hoverPart = ScrollView::PART_NONE;
    char m_note[24] = {};
};
