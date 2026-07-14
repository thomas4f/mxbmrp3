// ============================================================================
// tests/unit/test_render_frame_buffer.cpp
// Pure-logic tests for the triple buffer that hands render frames from the plugin
// worker thread to the game thread (core/render_frame_buffer.h). We can't assert
// timing here, but we CAN pin the two safety-critical invariants the plugin thread
// relies on:
//   1. The slot the producer writes is NEVER the slot the consumer is displaying,
//      across any interleaving of publish()/acquire() — so a producer overwrite
//      can't corrupt the frame the game is still reading.
//   2. acquire() returns the most recently published frame, and re-returns the same
//      one if nothing new was published (no stale-swap / no tearing).
// It also runs a real 2-thread producer/consumer loop under ThreadSanitizer-style
// scrutiny to catch a data race in the index bookkeeping.
// ============================================================================
#include "doctest.h"

#include "core/render_frame_buffer.h"

#include <atomic>
#include <set>
#include <thread>

namespace {

// A frame payload with a monotonically-increasing tag so the consumer can tell
// which producer frame it got, plus a canary the producer stamps so we can detect
// the consumer ever reading a slot the producer is concurrently writing.
struct TestFrame {
    int tag = 0;
    int canary = 0;
};

} // namespace

TEST_CASE("RenderFrameBuffer: write slot and display slot are always different") {
    RenderFrameBuffer<TestFrame> buf;
    // Indices start as a permutation of {0,1,2}.
    std::set<int> initial{ buf.writeIndex(), buf.readyIndex(), buf.displayIndex() };
    CHECK(initial == std::set<int>{ 0, 1, 2 });

    // Drive many publish/acquire cycles in varied order; the invariant write!=display
    // must hold after every operation.
    for (int i = 0; i < 100; ++i) {
        buf.writeSlot().tag = i;
        buf.publish();
        CHECK(buf.writeIndex() != buf.displayIndex());

        buf.acquire();
        CHECK(buf.writeIndex() != buf.displayIndex());

        // Indices remain a permutation of {0,1,2} (no slot lost or duplicated).
        std::set<int> perm{ buf.writeIndex(), buf.readyIndex(), buf.displayIndex() };
        CHECK(perm == std::set<int>{ 0, 1, 2 });
    }
}

TEST_CASE("RenderFrameBuffer: acquire returns latest published, holds it otherwise") {
    RenderFrameBuffer<TestFrame> buf;
    CHECK_FALSE(buf.everProduced());

    buf.writeSlot().tag = 1;
    buf.publish();
    CHECK(buf.everProduced());
    CHECK(buf.acquire().tag == 1);

    // No new publish: acquire re-returns the same frame.
    CHECK(buf.acquire().tag == 1);

    // Publish several times before acquiring: consumer jumps to the newest.
    buf.writeSlot().tag = 2; buf.publish();
    buf.writeSlot().tag = 3; buf.publish();
    CHECK(buf.acquire().tag == 3);
}

TEST_CASE("RenderFrameBuffer: producer never writes the slot the consumer reads") {
    // The strong version of invariant #1: keep &writeSlot() and the last acquired
    // address; they must never alias, so filling the write slot can't scribble on the
    // frame the consumer holds.
    RenderFrameBuffer<TestFrame> buf;
    buf.writeSlot().tag = 0; buf.publish();
    const TestFrame* displayed = &buf.acquire();
    for (int i = 1; i < 200; ++i) {
        const TestFrame* writing = &buf.writeSlot();
        CHECK(writing != displayed);      // producer's target != consumer's held frame
        buf.writeSlot().tag = i;
        buf.publish();
        // Consumer may or may not pick up the new frame each round; test both by only
        // acquiring on even iterations.
        if (i % 2 == 0) displayed = &buf.acquire();
        CHECK(&buf.writeSlot() != displayed);
    }
}

TEST_CASE("RenderFrameBuffer: concurrent producer/consumer stays consistent") {
    RenderFrameBuffer<TestFrame> buf;
    std::atomic<bool> stop{ false };
    constexpr int kFrames = 20000;

    std::thread producer([&]() {
        for (int i = 1; i <= kFrames; ++i) {
            TestFrame& w = buf.writeSlot();
            // Stamp tag and a matching canary so the consumer can verify it never saw a
            // half-written frame (tag and canary would disagree mid-write).
            w.tag = i;
            w.canary = i;
            buf.publish();
        }
        stop.store(true);
    });

    int lastTag = 0;
    bool torn = false;
    bool wentBackwards = false;
    std::thread consumer([&]() {
        while (!stop.load()) {
            if (!buf.everProduced()) continue;
            const TestFrame& f = buf.acquire();
            int tag = f.tag;
            int canary = f.canary;
            if (tag != canary) torn = true;          // never a partially-written frame
            if (tag < lastTag) wentBackwards = true; // frames only move forward
            lastTag = tag;
        }
    });

    producer.join();
    consumer.join();

    CHECK_FALSE(torn);
    CHECK_FALSE(wentBackwards);
    // Final acquire sees the last frame the producer published.
    CHECK(buf.acquire().tag == kFrames);
}
