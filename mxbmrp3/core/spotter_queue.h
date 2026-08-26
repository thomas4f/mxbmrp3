// ============================================================================
// core/spotter_queue.h
// The spotter's pending-cue queue: a plain bounded FIFO, pure logic so the
// policy is unit-testable without Windows audio (test_spotter_queue.cpp).
//
// OVERFLOW POLICY: drop-OLDEST. Every spotter cue loses value with age — it
// is information about a race that has moved on — so if the audio pipeline
// backs up (a long phrase still speaking while events pile in), the newest
// cue is the one worth keeping. When the queue is full the oldest pending cue
// is discarded to make room, never the newest.
//
// EXPIRY POLICY: cues flagged `perishable` lose value fast enough that late
// is the same as wrong, and are dropped at pop rather than spoken. See the
// flag's comment below for which cues qualify and why.
//
// Thread safety: none here by design. SpotterManager owns the instance and
// guards it with its mutex (MXB_GUARDED_BY); keeping this class lock-free is
// what lets the unit test exercise the policy directly.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// One queued audio cue. `payload` is UTF-8 text to synthesize for Speech, a
// file path (relative to the game working directory) for WavFile, and for
// MixSpec the TTS fallback text — spoken if any chunk fails to load — with the
// recipe itself in `mixDir` + `mixChunks`. The recipe exists so the 480fps
// game thread never touches files; all loading happens on the audio worker.
//
// The three parts used to be newline-joined into `payload` and re-split there,
// which cost an encode, a decode and a "did we get at least three lines" guard
// for a struct that is copied into a queue either way.
struct SpotterCue {
    enum class Kind : uint8_t {
        Speech,   // synthesize via TTS
        WavFile,  // play a .wav from disk
        MixSpec   // stitch chunk wavs and play the built buffer
    };

    Kind kind = Kind::Speech;
    std::string payload;
    // MixSpec only: the pack folder the chunk names resolve against, and the
    // chunk wav filenames in play order.
    std::string mixDir;
    std::vector<std::string> mixChunks;

    // PERISHABLE cues describe where a rider is RIGHT NOW — alongside you,
    // on your tail, clear again. Spoken late they are not merely useless but
    // misleading: "rider left" two seconds after they have gone is a call to
    // leave room that is no longer there. So these expire instead of waiting
    // their turn, and the queue drops them rather than delivering them stale.
    //
    // Everything else is worth hearing late: a lap time, a penalty, a flag —
    // those are facts about something that happened, and they stay true.
    bool perishable = false;
    uint64_t enqueuedMs = 0;   // steady tick at push; 0 when not perishable
};

class SpotterCueQueue {
public:
    // Deliberately small: at typical speech pace ~8 cues is already 15-20s of
    // backlog, which is past the point where any of it is still worth hearing.
    static constexpr size_t kMaxPending = 8;

    // Enqueue a cue; when full, discards the oldest pending cue first (see
    // the overflow policy above). Returns true if an old cue was dropped.
    bool push(SpotterCue cue) {
        bool dropped = false;
        if (m_cues.size() >= kMaxPending) {
            m_cues.pop_front();
            dropped = true;
        }
        m_cues.push_back(std::move(cue));
        return dropped;
    }

    // How long a perishable cue stays worth saying. Speech itself takes about
    // a second, so anything that has already waited longer than this describes
    // a situation that has moved on — and the detectors have hysteresis, so a
    // dropped call is not re-fired the next frame. Deliberately short.
    static constexpr uint64_t kPerishMs = 1500;

    // Dequeue the oldest cue still worth speaking. `nowMs` is a steady tick
    // from the caller (the queue keeps no clock so the policy stays testable).
    // Perishable cues that have aged past kPerishMs are discarded here rather
    // than at push: whether one is stale depends on how long the WORKER took,
    // which is not knowable when it is queued.
    bool pop(SpotterCue& out, uint64_t nowMs) {
        while (!m_cues.empty()) {
            SpotterCue& front = m_cues.front();
            const bool stale = front.perishable &&
                               nowMs >= front.enqueuedMs &&
                               (nowMs - front.enqueuedMs) > kPerishMs;
            if (!stale) {
                out = std::move(front);
                m_cues.pop_front();
                return true;
            }
            m_cues.pop_front();
        }
        return false;
    }

    void clear() { m_cues.clear(); }
    bool empty() const { return m_cues.empty(); }
    size_t size() const { return m_cues.size(); }

private:
    std::deque<SpotterCue> m_cues;
};
