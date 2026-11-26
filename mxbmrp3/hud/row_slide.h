// ============================================================================
// mxbmrp3/hud/row_slide.h
// RowSlide: a list's rows sliding to their new place when a rider's position
// changes. ONE implementation for every list that slides its riders: the
// Standings HUD and the Standings deck.
//
// A rebuild hands over its rows (begin, add per row, commit): a rider whose
// race POSITION moved since the last commit starts a slide from where that
// position change puts them to the row they now sit on. The previous row is
// estimated from the position delta, not read from the last commit's slots:
// the visible window can scroll on its own (a filter, top-N pinning, a
// spectate switch), and the raw slot would turn a scroll into a long slide.
// Only riders that were on the list at the last commit slide, and a slide
// starts at most one row past either end.
//
// A list slides only while Motion is on (Appearance; the caller checks), and
// every list slides for DURATION_MS, so the HUD and the deck move together.
// Times are microseconds on the Motion clock (DigitRoll::nowUs()), which the
// tests can set.
//
// Every frame between rebuilds asks offset() (rows still to travel, eased)
// and prune() (drops finished slides). Pinned by the row slide cases in
// tests/unit/test_standings_context_window.cpp.
// ============================================================================
#pragma once

#include "../core/flat_int_map.h"
#include "../core/motion.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

class RowSlide {
public:
    static constexpr float DURATION_MS = 500.0f;  // How long a row takes to its new place

    struct Slide {
        int fromSlot;       // The row it slides from
        int toSlot;         // The row it slides to
        int64_t startUs;
    };

    // A rebuild's rows: begin(), add() each rider's row, then commit().
    void begin() {
        m_currentPositions.clear();
        m_currentSlots.clear();
    }
    void add(int raceNum, int position, int slot) {
        m_currentPositions[raceNum] = position;
        m_currentSlots[raceNum] = slot;
    }
    void commit(int maxSlot, int64_t nowUs) {
        for (const auto& [raceNum, currentPos] : m_currentPositions) {
            auto prevIt = m_previousPositions.find(raceNum);
            if (prevIt == m_previousPositions.end() || prevIt->second == currentPos) continue;
            if (m_previousSlots.find(raceNum) == m_previousSlots.end()) continue;
            auto slotIt = m_currentSlots.find(raceNum);
            if (slotIt == m_currentSlots.end()) continue;
            const int currentSlot = slotIt->second;
            const int posDelta = prevIt->second - currentPos;  // positive = moved up
            const int fromSlot = std::max(-1, std::min(currentSlot + posDelta, maxSlot + 1));
            m_active[raceNum] = { fromSlot, currentSlot, nowUs };
        }
        // SWAP, not move: the scratch maps keep their buffers, so a steady
        // state allocates nothing (the next begin() clears what was previous).
        m_previousPositions.swap(m_currentPositions);
        m_previousSlots.swap(m_currentSlots);
    }

    // The slide's ease, 0..1 in to 0..1 travelled: fast out, settling in
    // (Motion's ease-out, no overshoot). Anything that should move with the
    // rows (the Combo deck's chips) uses it.
    static float easeOut(float t) { return Motion::easeOut(t, 0.0f); }

    // Rows still to travel (eased out, shrinking to 0): + = still below its
    // row, - = still above it. 0 without a slide.
    float offset(int raceNum, int64_t nowUs) const {
        const float t = progress(raceNum, nowUs);
        if (t >= 1.0f) return 0.0f;
        const Slide& s = m_active.find(raceNum)->second;
        return static_cast<float>(s.fromSlot - s.toSlot) * (1.0f - easeOut(t));
    }
    // 1 at the start of a slide down to 0 at its end, linear; 0 without one.
    float fade(int raceNum, int64_t nowUs) const {
        return 1.0f - progress(raceNum, nowUs);
    }
    const Slide* find(int raceNum) const {
        auto it = m_active.find(raceNum);
        return it == m_active.end() ? nullptr : &it->second;
    }
    bool active() const { return !m_active.empty(); }

    // Drops the slides that have ended. True when this ended the last one.
    bool prune(int64_t nowUs) {
        if (m_active.empty()) return false;
        for (auto it = m_active.begin(); it != m_active.end(); ) {
            if (elapsedMs(it->second, nowUs) >= DURATION_MS) it = m_active.erase(it);
            else ++it;
        }
        return m_active.empty();
    }

    // Forgets the rows too, so the next commit slides nothing.
    void forget() {
        m_active.clear();
        m_previousPositions.clear();
        m_previousSlots.clear();
    }

private:
    static float elapsedMs(const Slide& s, int64_t nowUs) {
        return static_cast<float>(nowUs - s.startUs) / 1000.0f;
    }
    // 0..1 through the rider's slide; 1 without one
    float progress(int raceNum, int64_t nowUs) const {
        auto it = m_active.find(raceNum);
        if (it == m_active.end()) return 1.0f;
        return std::min(1.0f, std::max(0.0f, elapsedMs(it->second, nowUs) / DURATION_MS));
    }

    std::unordered_map<int, Slide> m_active;   // raceNum -> slide in flight
    FlatIntMap<int> m_previousPositions;       // raceNum -> position at the last commit
    FlatIntMap<int> m_previousSlots;           // raceNum -> row at the last commit
    FlatIntMap<int> m_currentPositions;        // this commit's (clear()+swap() keep the buffers)
    FlatIntMap<int> m_currentSlots;
};
