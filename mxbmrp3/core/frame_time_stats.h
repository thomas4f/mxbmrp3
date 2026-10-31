// ============================================================================
// core/frame_time_stats.h
// Whole-stint frame-time statistics for the Benchmark profiler: a log-bucket
// histogram (percentiles over EVERY frame, in fixed memory) plus the N worst
// frames, each stamped with when it happened and what the plugin spent in it.
//
// WHY THIS EXISTS. The profiler used to keep the last 16,384 raw frame times in a
// ring and sort them on export. That is ~34 s at 480 fps and ~4.5 min at 60 fps,
// so a player capturing a stutter that came ten minutes into a race exported a
// report whose frame figures no longer contained it. A histogram keeps the whole
// stint at a fixed size, and the worst-frames list answers the question a stutter
// report actually asks -- "was the plugin slow in THAT frame?" -- with a time
// that can be lined up against a video clip. It deliberately gives no verdict:
// every timer here is wall time, so a Windows pause inside a plugin callback
// (a busy or few-core PC) reads as plugin time. The numbers are stated; the
// reading is left to whoever has the whole report.
//
// ACCURACY. Buckets grow by 1% from MIN_US, so a percentile read back is within
// ~0.5% of the true sample (the bucket's geometric midpoint, clamped to the exact
// min/max, which are tracked separately). Min, max and mean are exact.
//
// COST. add() is one log() and a few compares; the worst list is only rescanned
// when a frame displaces its smallest entry. The histogram (~5.6 KB) is allocated
// on the first sample, not at construction, so a player who never opens the
// profiler pays one empty vector.
//
// Pinned by tests/unit/test_frame_time_stats.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

class FrameTimeStats {
public:
    static constexpr int WORST_COUNT = 20;

    // Bucket range: 10 us .. ~10 s, 1% apart. Frames outside land in the end
    // buckets; min/max stay exact regardless.
    static constexpr double MIN_US = 10.0;
    static constexpr double RATIO = 1.01;
    static constexpr int BUCKET_COUNT = 1389;   // ceil(ln(1e6) / ln(1.01))
    // 1 / ln(RATIO); a literal because std::log is not constexpr on MSVC. Pinned
    // against the real value by the unit test.
    static constexpr double INV_LOG_RATIO = 100.49917081;

    // An interval longer than this is a GAP, not a frame: the game sends no Draw in
    // menus or while loading, so a stint that crosses a session change would
    // otherwise record one "frame" tens of seconds long -- the max, the FPS min, a
    // hitch, and the top of the worst list, read by anyone as a giant freeze. Gaps
    // are counted and totalled instead. ONLY while the plugin took under
    // GAP_PLUGIN_SHARE of it: the plugin gets no callbacks in menus either, so a
    // long interval the plugin spent its time in is a plugin HANG, and stays a
    // frame -- the worst one there is. A share, not an absolute floor: the
    // callbacks around a session change (leave-track save, centerline, the entry
    // list, the first Draw back that rebuilds every HUD) can pass 10 ms, and they
    // are not a hang.
    static constexpr double GAP_US = 5.0e6;
    static constexpr double GAP_PLUGIN_SHARE = 0.25;

    // A frame over this is a HITCH: under 20 fps for that one frame, what a player
    // calls a freeze.
    static constexpr double HITCH_US = 50000.0;

    struct Spike {
        double frameUs = 0.0;     // frame interval
        double atSec = 0.0;       // seconds since the capture started
        long long pluginUs = 0;   // plugin callback time inside that interval (upper bound)
        int riders = 0;           // race entries at the time
        // The longest single plugin callback inside it, and which (an index into
        // BenchmarkMetrics::callbacks, -1 = none): one long call is a slow
        // callback, many short ones adding up point at a busy PC instead.
        long long maxCallUs = 0;
        int maxCallIdx = -1;
    };

    void reset() {
        if (!m_buckets.empty()) std::fill(m_buckets.begin(), m_buckets.end(), 0u);
        m_count = 0;
        m_sumUs = 0.0;
        m_minUs = 0.0;
        m_maxUs = 0.0;
        m_worstCount = 0;
        m_worstMinIdx = 0;
        m_gapCount = 0;
        m_gapUs = 0.0;
        m_hitchCount = 0;
        m_maxPluginUs = 0;
    }

    // The GAP test above, shared by add() and bookNextInterval().
    static bool isGap(double frameUs, long long pluginUs) {
        return frameUs > GAP_US && static_cast<double>(pluginUs) < GAP_PLUGIN_SHARE * frameUs;
    }

    // Books the interval AFTER a frame onto it (the profiler samples inside Draw,
    // so a frame's own Draw ends in the next interval - see sampleFrameTime). A
    // GAP is not booked: its plugin time is the leave-track save, the report
    // itself, the centerline and the first Draw back, none of it spent in the
    // last frame on track. Booked anyway, one pit stop put tens of ms on an
    // ordinary frame and made it the report's "most ms in any one frame".
    static void bookNextInterval(Spike& frame, double nextUs, long long nextPluginUs,
                                 long long nextMaxCallUs, int nextMaxCallIdx) {
        if (isGap(nextUs, nextPluginUs)) return;
        frame.pluginUs += nextPluginUs;
        if (nextMaxCallUs > frame.maxCallUs) {
            frame.maxCallUs = nextMaxCallUs;
            frame.maxCallIdx = nextMaxCallIdx;
        }
    }

    void add(const Spike& f) {
        const double frameUs = f.frameUs;
        // zero/negative/NaN/inf: clock anomaly, would read as inf FPS (or overflow the bucket cast)
        if (!(frameUs > 0.0) || !std::isfinite(frameUs)) return;
        if (isGap(frameUs, f.pluginUs)) {
            ++m_gapCount;
            m_gapUs += frameUs;
            return;
        }
        if (m_buckets.empty()) m_buckets.assign(BUCKET_COUNT, 0u);
        ++m_buckets[static_cast<size_t>(bucketOf(frameUs))];
        if (m_count == 0 || frameUs < m_minUs) m_minUs = frameUs;
        if (m_count == 0 || frameUs > m_maxUs) m_maxUs = frameUs;
        ++m_count;
        m_sumUs += frameUs;
        if (frameUs > HITCH_US) ++m_hitchCount;   // exact, unlike countAbove()
        if (f.pluginUs > m_maxPluginUs) m_maxPluginUs = f.pluginUs;

        if (m_worstCount < WORST_COUNT) {
            m_worst[static_cast<size_t>(m_worstCount++)] = f;
            if (m_worstCount == WORST_COUNT) rescanWorstMin();
        } else if (frameUs > m_worst[static_cast<size_t>(m_worstMinIdx)].frameUs) {
            m_worst[static_cast<size_t>(m_worstMinIdx)] = f;
            rescanWorstMin();
        }
    }

    long long count() const { return m_count; }
    int gapCount() const { return m_gapCount; }
    double gapSec() const { return m_gapUs / 1.0e6; }
    double minUs() const { return m_minUs; }
    double maxUs() const { return m_maxUs; }
    double meanUs() const { return (m_count > 0) ? m_sumUs / static_cast<double>(m_count) : 0.0; }

    // Frame time at percentile p (0..1), nearest-rank on (count-1) like a sorted
    // array's s[p * (n-1)]. 0 when empty.
    double percentileUs(double p) const {
        if (m_count <= 0) return 0.0;
        if (p <= 0.0) return m_minUs;
        if (p >= 1.0) return m_maxUs;
        const long long rank = static_cast<long long>(p * static_cast<double>(m_count - 1));
        long long seen = 0;
        for (int i = 0; i < BUCKET_COUNT; ++i) {
            seen += m_buckets[static_cast<size_t>(i)];
            if (seen > rank) {
                const double mid = MIN_US * std::pow(RATIO, i + 0.5);
                return std::clamp(mid, m_minUs, m_maxUs);
            }
        }
        return m_maxUs;
    }

    // Frames longer than thresholdUs, to bucket resolution (a frame within 1% of
    // the threshold may land either side).
    long long countAbove(double thresholdUs) const {
        if (m_count <= 0) return 0;
        long long n = 0;
        for (int i = bucketOf(thresholdUs) + 1; i < BUCKET_COUNT; ++i)
            n += m_buckets[static_cast<size_t>(i)];
        return n;
    }

    // The worst frames, longest first. Returns how many were written.
    int worst(Spike* out, int cap) const {
        const int n = std::min(cap, m_worstCount);
        std::array<Spike, WORST_COUNT> sorted = m_worst;
        std::sort(sorted.begin(), sorted.begin() + m_worstCount,
                  [](const Spike& a, const Spike& b) { return a.frameUs > b.frameUs; });
        for (int i = 0; i < n; ++i) out[i] = sorted[static_cast<size_t>(i)];
        return n;
    }

    long long hitchCount() const { return m_hitchCount; }
    // The most plugin time in any one frame of the stint (an upper bound per frame).
    long long maxPluginUs() const { return m_maxPluginUs; }

private:
    static int bucketOf(double us) {
        if (us <= MIN_US) return 0;
        const int i = static_cast<int>(std::log(us / MIN_US) * INV_LOG_RATIO);
        return std::clamp(i, 0, BUCKET_COUNT - 1);
    }

    void rescanWorstMin() {
        m_worstMinIdx = 0;
        for (int i = 1; i < m_worstCount; ++i)
            if (m_worst[static_cast<size_t>(i)].frameUs < m_worst[static_cast<size_t>(m_worstMinIdx)].frameUs)
                m_worstMinIdx = i;
    }

    std::vector<uint32_t> m_buckets;   // sized once on first add(), never grown
    long long m_count = 0;
    double m_sumUs = 0.0;
    double m_minUs = 0.0;
    double m_maxUs = 0.0;
    std::array<Spike, WORST_COUNT> m_worst{};
    int m_worstCount = 0;
    int m_worstMinIdx = 0;
    int m_gapCount = 0;
    double m_gapUs = 0.0;
    long long m_hitchCount = 0;
    long long m_maxPluginUs = 0;
};
