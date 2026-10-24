// ============================================================================
// hud/gap_auto_range.h
// The Gap Bar's Auto range: the round step (PluginUtils::niceGapScaleMs, the
// Delta Trace's scale) holding the largest gap of the lap so far, never
// shrinking until the lap is over - so a fill drawn against it does not jump
// back and forth with every swing.
//
// One per bar that draws a fill: the Gap Bar HUD and the Dash's Gap bar page
// each own one, so both scale alike without either reading the other's state.
// Integer work only; follow() runs once a frame.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdlib>

#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"

class GapAutoRange {
public:
    // Once a frame, whether or not the bar is shown. The peak starts over on a
    // new session, another displayed rider, another reference, and when the gap
    // comes back live (the next timed lap after the pits: the out-lap is never
    // committed, so the lap stamp alone would miss it).
    void follow(PbGapTracker::Ref reference) {
        const PluginData& pd = PluginData::getInstance();
        const int generation = pd.getSessionData().sessionGeneration;
        const int raceNum = pd.getDisplayRaceNum();
        const bool live = pd.hasValidLiveGap(reference);
        if (generation != m_generation || raceNum != m_raceNum || reference != m_reference ||
            (live && !m_live)) {
            m_peakMs = 0;
        }
        m_generation = generation;
        m_raceNum = raceNum;
        m_reference = reference;
        m_live = live;
    }

    // The range to draw `gapMs` against, never under `minMs`. A committed lap
    // starts the peak over.
    int rangeMs(int gapMs, int minMs) {
        const unsigned stamp = PluginData::getInstance().getPbGapTracker().lastLapStamp();
        if (stamp != m_lapStamp) {
            m_lapStamp = stamp;
            m_peakMs = 0;
        }
        m_peakMs = std::max(m_peakMs, std::abs(gapMs));
        return std::max(minMs, PluginUtils::niceGapScaleMs(m_peakMs));
    }

    void reset() { m_peakMs = 0; }

private:
    int m_peakMs = 0;            // the largest |gap| this lap
    unsigned m_lapStamp = 0;     // PbGapTracker::lastLapStamp() the peak belongs to
    int m_generation = -1;
    int m_raceNum = -1;
    bool m_live = false;
    PbGapTracker::Ref m_reference = PbGapTracker::Ref::SESSION_PB;
};
