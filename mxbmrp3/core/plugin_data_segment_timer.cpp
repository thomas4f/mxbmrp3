// ============================================================================
// core/plugin_data_segment_timer.cpp
// Segment timer (training tool)
// (extracted verbatim from plugin_data.cpp; no behavior change)
// ============================================================================

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "hud_manager.h"  // Direct include for notification
#if GAME_HAS_DISCORD
#include "discord_manager.h"  // Direct include for Discord presence updates
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"  // Steam friends rich-presence integration
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"  // Direct include for web overlay updates
#endif
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ========================================================================
// Segment Timer (training tool)
// ========================================================================
void PluginData::resetSegmentTimer() {
    m_segment = SegmentTimerData{};
    m_segmentHasPrev = false;
    m_segmentPrevPos = 0.0f;
    m_segmentPrevWall = {};
    // Note: does not touch the segment notice - the hotkey handlers own that.
}

void PluginData::addSegmentPoint() {
    // Once the loop is closed (last point back on the first), it already tiles the
    // lap - block further points so they can't overlap the closing segment. Remove
    // the closing point to reopen the chain.
    if (m_segment.isClosed()) return;

    // Cap the number of points (a sanity bound, not a usage limit).
    if (static_cast<int>(m_segment.points.size()) >= SegmentTimerData::MAX_POINTS) return;

    const TrackPositionData* pos = getPlayerTrackPosition();
    if (!pos) return;  // not on track - nothing to mark yet

    float p = pos->trackPos;

    // Snap to a nearby official split (S/F, split 1, split 2) when enabled (INI-only).
    // Distance is circular on the 0-1 lap so a point just before S/F snaps onto it.
    UiConfig& ui = UiConfig::getInstance();
    if (ui.getSnapSegmentsToSplits() && !m_splitPositions.empty()) {
        float bestDist = ui.getSegmentSnapThreshold();
        for (float sp : m_splitPositions) {
            float dist = std::fabs(p - sp);
            if (dist > 0.5f) dist = 1.0f - dist;  // wrap-around
            if (dist <= bestDist) { bestDist = dist; p = sp; }
        }
    }

    // If this point closes the loop (lands back near the first), snap it exactly
    // onto the first point. The chain then tiles the lap with no untimed sliver, and
    // the coincident-crossing tie-break in updateSegmentTimer keeps timing clean.
    // (Needs >=2 existing points so the closer is at least the 3rd, matching isClosed.)
    if (m_segment.points.size() >= 2) {
        float d = std::fabs(p - m_segment.points.front());
        if (d > 0.5f) d = 1.0f - d;  // circular distance on the 0-1 lap
        if (d <= SegmentTimerData::CLOSE_EPS) p = m_segment.points.front();
    }

    m_segment.points.push_back(p);
    // Open chain: adding a point appends one new segment (between the new last two
    // points); existing segments are unchanged, so keep their bests.
    int segCount = m_segment.segmentCount();
    m_segment.bests.resize(segCount, 0.0f);
    m_segment.hasBest.resize(segCount, 0);

    m_segmentNotice = SegmentNoticeKind::Added;
    m_segmentNoticeTime = std::chrono::steady_clock::now();
}

void PluginData::removeSegmentPoint() {
    if (m_segment.points.empty()) return;

    m_segment.points.pop_back();
    int segCount = m_segment.segmentCount();
    m_segment.bests.resize(segCount, 0.0f);
    m_segment.hasBest.resize(segCount, 0);

    // Removing a point drops the last segment; invalidate any run/freeze that
    // referenced a now-gone segment.
    if (m_segment.runningSeg >= segCount) m_segment.runningSeg = -1;
    if (m_segment.lastSeg >= segCount) m_segment.lastSeg = -1;

    m_segmentNotice = m_segment.points.empty() ? SegmentNoticeKind::Cleared
                                               : SegmentNoticeKind::Removed;
    m_segmentNoticeTime = std::chrono::steady_clock::now();
}

void PluginData::updateSegmentTimer(float trackPos) {
    SegmentTimerData& s = m_segment;
    auto now = std::chrono::steady_clock::now();

    // Snapshot the previous sample, then store the current one for next tick.
    float prevPos = m_segmentPrevPos;
    auto prevWall = m_segmentPrevWall;
    bool hadPrev = m_segmentHasPrev;
    m_segmentPrevPos = trackPos;
    m_segmentPrevWall = now;
    m_segmentHasPrev = true;

    const int segCount = s.segmentCount();
    // Need at least one segment (two points) and a previous sample to compare.
    if (!hadPrev || segCount < 1) {
        return;
    }

    // Forward distance travelled this tick. A *large* negative raw delta is the
    // 0.999 -> 0.0 wrap across start/finish (a small forward step); a *small*
    // negative delta is just backward jitter while stationary/slow and must NOT
    // be treated as a near-full-lap move.
    float raw = trackPos - prevPos;
    float d = (raw < -0.5f) ? (raw + 1.0f) : raw;

    // Forward teleport/reset (a big forward jump in one tick): cancel any in-progress
    // run and skip crossing detection. Backward motion (d <= 0) yields no forward
    // crossing below, so the run keeps running when you slow or stop.
    if (d > 0.5f) {
        s.runningSeg = -1;
        return;
    }

    // Wall-clock duration of this tick, used to interpolate crossing times by the
    // same fraction we compute from the centerline position.
    double dtSec = std::chrono::duration<double>(now - prevWall).count();
    auto interpWall = [&](float frac) -> std::chrono::steady_clock::time_point {
        return prevWall + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(dtSec * frac));
    };

    // Fraction into this tick at which boundary point pointIdx is crossed (forward arc).
    auto crossFrac = [&](int pointIdx, bool& crossed) -> float {
        float db = s.points[pointIdx] - prevPos;
        if (db < 0.0f) db += 1.0f;
        crossed = (d > 1e-6f) && (db > 0.0f) && (db <= d);
        return (d > 1e-6f) ? (db / d) : 0.0f;
    };

    // Complete the running segment as of endWall: record time, delta vs its prior
    // best, update that segment's best, and bump the completion counter.
    auto completeRun = [&](std::chrono::steady_clock::time_point endWall) {
        int i = s.runningSeg;
        if (i < 0 || i >= segCount) { s.runningSeg = -1; return; }
        double result = std::chrono::duration<double>(endWall - s.runStart).count();
        s.runningSeg = -1;
        if (result <= 0.0) return;  // degenerate ordering guard

        float t = static_cast<float>(result);
        s.lastSeg = i;
        s.lastTime = t;
        if (s.hasBest[i]) {
            s.lastDelta = t - s.bests[i];
            s.lastHasDelta = true;
        } else {
            s.lastHasDelta = false;
        }
        if (!s.hasBest[i] || t < s.bests[i]) {
            s.bests[i] = t;
            s.hasBest[i] = 1;
            s.lastIsBest = true;
        } else {
            s.lastIsBest = false;
        }
        s.completionCounter++;
    };

    // Collect every point crossed this tick, in the order they were crossed.
    struct Crossing { int idx; float frac; };
    std::vector<Crossing> crossings;
    const int N = static_cast<int>(s.points.size());
    for (int i = 0; i < N; ++i) {
        bool crossed = false;
        float frac = crossFrac(i, crossed);
        if (crossed) crossings.push_back({ i, frac });
    }
    // Sort by crossing fraction. Tie-break (coincident points, e.g. a closing point
    // dropped on the start/finish line that shares trackPos with the first point) by
    // chain order starting from the running segment's expected end, so the running
    // segment is completed before a coincident point (re)starts a new one.
    const int entryBase = (s.runningSeg >= 0) ? (s.runningSeg + 1) : 0;
    std::sort(crossings.begin(), crossings.end(),
              [&](const Crossing& a, const Crossing& b) {
                  if (a.frac != b.frac) return a.frac < b.frac;
                  return ((a.idx - entryBase + N) % N) < ((b.idx - entryBase + N) % N);
              });

    // Process crossings in order. Open chain: crossing point i ends segment i-1 and
    // starts segment i. Complete the running segment if this point is its end, then
    // start the segment that begins here (if one does - the last point begins none).
    for (const Crossing& c : crossings) {
        auto crossWall = interpWall(c.frac);
        if (s.runningSeg >= 0 && c.idx == s.runningSeg + 1) {
            completeRun(crossWall);  // completes runningSeg, sets it to -1
        }
        if (c.idx < segCount) {  // segment c.idx exists (c.idx in [0, N-2])
            s.runningSeg = c.idx;
            s.runStart = crossWall;
        }
    }
}
