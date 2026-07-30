// ============================================================================
// core/render_frame_buffer.h
// A tiny lock-guarded TRIPLE buffer used to hand render frames from the plugin
// worker thread to the game thread (see core/plugin_thread.{h,cpp}).
//
// Why triple and not double: the game reads the frame it was handed for the WHOLE
// duration between two Draw calls (it renders the quads AFTER Draw returns). A
// double buffer would let the producer start overwriting the very slot the game is
// still reading. With three slots the invariant holds that the producer's write
// slot and the consumer's display slot are ALWAYS different physical buffers, so
// the producer can keep publishing at full rate while the consumer holds its frame.
//
// It's a header-only template on the frame payload so the concurrency-critical
// index bookkeeping can be unit-tested in isolation (tests/unit) with a trivial
// payload, without dragging in HudManager / the game structs.
//
// Ownership rules (single producer, single consumer):
//   * Producer thread: writeSlot() to fill, then publish().  Only touches m_write.
//   * Consumer thread: acquire() to adopt the latest published frame and read it.
//     The reference acquire() returns stays valid until the NEXT acquire().
// All index swaps are done under the mutex; the frame payloads themselves are
// never touched under the lock (only cheap index swaps are), so a slow copy on the
// producer never blocks the consumer and vice-versa.
// ============================================================================
#pragma once

#include <mutex>
#include "thread_safety.h"
#include <utility>

template <class Frame>
class RenderFrameBuffer {
public:
    // Producer: the slot to fill this cycle. Stable until the next publish().
    // Deliberately lock-free: m_write only changes inside publish(), which the
    // SAME producer thread calls — so this read can't race. TSA can't express
    // "single-producer owns this index between publishes", hence the opt-out.
    Frame& writeSlot() MXB_NO_TSA { return m_slots[m_write]; }

    // Producer: make the just-filled write slot the latest "ready" frame and take a
    // recycled slot to write next. Never touches the consumer's display slot.
    void publish() {
        MutexLock lk(m_mutex);
        std::swap(m_write, m_ready);
        m_haveReady = true;
        m_everProduced = true;
    }

    // Consumer: adopt the latest ready frame (if any new one) as the display frame and
    // return a reference to it. If nothing new was published since the last call, the
    // previously displayed frame is returned again (no tearing, no stale-swap).
    const Frame& acquire() {
        MutexLock lk(m_mutex);
        if (m_haveReady) {
            std::swap(m_ready, m_display);
            m_haveReady = false;
        }
        return m_slots[m_display];
    }

    // True once the producer has published at least one frame.
    bool everProduced() const {
        MutexLock lk(m_mutex);
        return m_everProduced;
    }

    // Test/introspection: current physical slot indices. The three are always a
    // permutation of {0,1,2}; in particular write != display always holds.
    int writeIndex() const   { MutexLock lk(m_mutex); return m_write; }
    int readyIndex() const   { MutexLock lk(m_mutex); return m_ready; }
    int displayIndex() const { MutexLock lk(m_mutex); return m_display; }

private:
    Frame m_slots[3]{};
    int m_write MXB_GUARDED_BY(m_mutex) = 0;
    int m_ready MXB_GUARDED_BY(m_mutex) = 1;
    int m_display MXB_GUARDED_BY(m_mutex) = 2;
    bool m_haveReady MXB_GUARDED_BY(m_mutex) = false;
    bool m_everProduced MXB_GUARDED_BY(m_mutex) = false;
    mutable Mutex m_mutex;
};
