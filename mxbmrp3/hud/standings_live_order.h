// ============================================================================
// hud/standings_live_order.h
// Standings' optional live running order: the official classification, with
// passes the track positions have shown for at least HOLD_MS applied ahead of
// the game's own update. The Position widget and the Dash's Standings page
// read it too, through StandingsHud::runningOrder(). Pure integer logic; no
// PluginData, no BaseHud.
//
// WHY. The game updates positions only when a rider crosses a split or the
// line, so a pass mid-sector shows seconds later (a median 7.5 s on a real
// online tape). The track-position feed covers the whole field about once a
// frame, so the order between those updates can be read from it.
//
// THE RULES, and the plausible edit that breaks each:
//   - The official order is the BASELINE, recomputed from scratch on every
//     call. A pass is a pair (officially ahead, officially behind) whose track
//     positions disagree; once the official order agrees too, the pair simply
//     stops existing. Keeping a separate running order instead would drift
//     from the game's with nothing to pull it back.
//   - A pass counts once it has held HOLD_MS, and is taken back only once the
//     reverse has held HOLD_MS. Without the first hold, two riders side by side
//     swap rows every few frames; without the second, an applied pass flickers
//     back the moment the passed rider draws level.
//   - Evidence only between riders on the SAME lap with a FRESH sample. The
//     game bumps the lap count a few frames before the position wraps at the
//     line, so a cross-lap comparison reads as a rider a lap ahead (the
//     same-lap moment before the wrap is shorter than the hold). And after a
//     gate start the game does not count the first crossing of the line as a
//     lap at all: riders already over it read 0.0x on lap 0 while those still
//     short of it read 0.9x on lap 0, so for seconds the field looks
//     inverted. Hence also less than half a lap apart (MAX_EVIDENCE_SPAN):
//     two riders genuinely that far apart are not passing each other. A rider
//     missing from the latest batch (a quarter of batches miss one to four
//     riders) has a stale position, and one going the wrong way (a crash, a
//     reset) is not racing the others. None of that is evidence either way:
//     the pair keeps whatever state it had, so a pass already shown does not
//     snap back for a moment and then show again.
//   - Only `racing` riders move. Everyone else (pits, finished, retired,
//     lapped, no position yet) keeps their official row, and the racing riders
//     are reordered among the remaining rows. Lapped is the one the tape
//     caught: a lapped rider carries the same lap count as the riders a lap
//     ahead, so on track it reads as passing them.
//
// CHECKED against Thomas's 28 Oct online tape (race 2, up to 21 riders),
// replayed through the plugin: every live pass was later confirmed by the
// official order, typically several seconds before it.
//
// COST. One call per Standings rebuild and per 100 ms poll, not per frame:
// O(n^2) pair checks for n racing riders with a binary-searched pair table,
// and no allocation once the buffers have grown.
//
// Pinned by tests/unit/test_standings_live_order.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace StandingsLiveOrder {

struct Rider {
    int raceNum = -1;
    int numLaps = 0;         // official lap count
    float trackPos = 0.0f;   // 0..1 along the centreline
    bool racing = false;     // may move: on track, on the leader's lap, not finished
    bool fresh = false;      // in the latest track-position batch, going the right way
};

class Tracker {
public:
    static constexpr int64_t HOLD_MS = 500;
    // Riders further apart than this on the same lap count are read as one
    // of them having crossed the line uncounted (see the header), not a pass.
    static constexpr float MAX_EVIDENCE_SPAN = 0.5f;

    // `riders` is the whole official order, leader first. `out` receives the
    // live order: the same race numbers, the same length. Returns true while a
    // pass is still waiting out a hold, so the caller knows to call again even
    // if no new data arrives.
    bool update(const std::vector<Rider>& riders, int64_t nowMs, std::vector<int>& out) {
        out.clear();
        m_slots.clear();
        for (size_t i = 0; i < riders.size(); ++i) {
            out.push_back(riders[i].raceNum);
            if (riders[i].racing) m_slots.push_back(static_cast<int>(i));
        }

        bool pending = false;
        m_next.clear();
        for (size_t a = 0; a < m_slots.size(); ++a) {
            const Rider& ahead = riders[m_slots[a]];
            for (size_t b = a + 1; b < m_slots.size(); ++b) {
                const Rider& behind = riders[m_slots[b]];
                const Pair* old = find(ahead.raceNum, behind.raceNum);
                const bool evidence = ahead.fresh && behind.fresh && ahead.numLaps == behind.numLaps &&
                    std::fabs(behind.trackPos - ahead.trackPos) < MAX_EVIDENCE_SPAN;
                if (!evidence) {
                    if (old) m_next.push_back(*old);  // no evidence either way: as it was
                    continue;
                }
                const bool passed = behind.trackPos > ahead.trackPos;
                if (!old) {
                    if (passed) {
                        m_next.push_back({ key(ahead.raceNum, behind.raceNum), nowMs, -1, false });
                        pending = true;
                    }
                    continue;
                }
                Pair p = *old;
                if (!p.applied) {
                    if (!passed) continue;  // never held: forget it
                    if (nowMs - p.since >= HOLD_MS) p.applied = true;
                    else pending = true;
                } else if (passed) {
                    p.revertSince = -1;
                } else {
                    if (p.revertSince < 0) p.revertSince = nowMs;
                    if (nowMs - p.revertSince >= HOLD_MS) continue;  // back to official
                    pending = true;
                }
                m_next.push_back(p);
            }
        }
        // Pairs of riders who no longer race, or whose official order now
        // agrees with the track, were not carried over: that is the re-baseline.
        std::sort(m_next.begin(), m_next.end(),
                  [](const Pair& l, const Pair& r) { return l.key < r.key; });
        m_pairs.swap(m_next);

        // Apply: an applied pair moves the officially-behind rider up past the
        // one officially ahead. Pairs are keyed in official order, so a swapped
        // pair never swaps back and the passes end within n rounds.
        m_order.clear();
        for (int slot : m_slots) m_order.push_back(riders[slot].raceNum);
        for (size_t round = 0; round < m_order.size(); ++round) {
            bool swapped = false;
            for (size_t k = 0; k + 1 < m_order.size(); ++k) {
                const Pair* p = find(m_order[k], m_order[k + 1]);
                if (p && p->applied) {
                    std::swap(m_order[k], m_order[k + 1]);
                    swapped = true;
                }
            }
            if (!swapped) break;
        }
        for (size_t k = 0; k < m_slots.size(); ++k) out[m_slots[k]] = m_order[k];
        return pending;
    }

    void reset() { m_pairs.clear(); }
    size_t pairCount() const { return m_pairs.size(); }

private:
    struct Pair {
        uint64_t key;          // (officially ahead, officially behind)
        int64_t since;         // when the pass was first seen
        int64_t revertSince;   // when an applied pass was first seen undone; -1 = not
        bool applied;
    };

    static uint64_t key(int ahead, int behind) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(ahead)) << 32) | static_cast<uint32_t>(behind);
    }
    const Pair* find(int ahead, int behind) const {
        const uint64_t k = key(ahead, behind);
        auto it = std::lower_bound(m_pairs.begin(), m_pairs.end(), k,
                                   [](const Pair& p, uint64_t v) { return p.key < v; });
        return (it != m_pairs.end() && it->key == k) ? &*it : nullptr;
    }

    std::vector<Pair> m_pairs;   // sorted by key
    std::vector<Pair> m_next;    // scratch, swapped with m_pairs
    std::vector<int> m_slots;    // official indices of the racing riders
    std::vector<int> m_order;    // racing riders' live order
};

}  // namespace StandingsLiveOrder
