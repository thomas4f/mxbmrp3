// ============================================================================
// tests/unit/history_ring_test.cpp
// HistoryRing<T, N> (core/history_ring.h): a fixed-capacity rolling history.
//
// WHY THIS FILE EXISTS. This replaced std::deque<float> in the telemetry and
// rumble histories, which is a storage change with NO visible symptom when it goes
// wrong in the ordinary case: a graph drawn from a ring whose seam is off by one
// still draws a plausible-looking line, just of the wrong data. The thing that has
// to hold is that `[0]` is always the oldest sample and `[size()-1]` the newest,
// across the wrap -- so that is what these cases sit on, at and either side of
// capacity, which is where a hand-rolled ring goes wrong.
//
// The deque it replaced got this right for free. The point of the test is that the
// replacement does too, in the one place the ring is allowed to be clever.
// ============================================================================
#include "doctest.h"

#include "core/history_ring.h"

#include <vector>

namespace {
// The ring's contents oldest-first, read through the public interface only.
template <typename Ring>
std::vector<float> drain(const Ring& r) {
    std::vector<float> out;
    for (std::size_t i = 0; i < r.size(); ++i) out.push_back(r[i]);
    return out;
}
}  // namespace

TEST_CASE("HistoryRing: empty by default") {
    HistoryRing<float, 4> r;
    CHECK(r.size() == 0);
    CHECK(r.empty());
    CHECK(HistoryRing<float, 4>::capacity() == 4);
}

TEST_CASE("HistoryRing: fills in order, oldest first") {
    HistoryRing<float, 4> r;
    r.push(1.0f);
    r.push(2.0f);
    r.push(3.0f);
    CHECK(r.size() == 3);
    CHECK_FALSE(r.empty());
    CHECK(drain(r) == std::vector<float>{ 1.0f, 2.0f, 3.0f });
    CHECK(r[0] == 1.0f);          // oldest
    CHECK(r.back() == 3.0f);      // newest
}

TEST_CASE("HistoryRing: exactly full is still in order") {
    // The boundary either side of which the indexing changes branch.
    HistoryRing<float, 4> r;
    for (float f : { 1.0f, 2.0f, 3.0f, 4.0f }) r.push(f);
    CHECK(r.size() == 4);
    CHECK(drain(r) == std::vector<float>{ 1.0f, 2.0f, 3.0f, 4.0f });
    CHECK(r.back() == 4.0f);
}

TEST_CASE("HistoryRing: one past full drops the oldest, not the newest") {
    // THE CASE THAT MATTERS. A ring that drops the wrong end, or reports the seam
    // as element 0, still draws a graph -- of the wrong samples.
    HistoryRing<float, 4> r;
    for (float f : { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f }) r.push(f);
    CHECK(r.size() == 4);
    CHECK(drain(r) == std::vector<float>{ 2.0f, 3.0f, 4.0f, 5.0f });
    CHECK(r[0] == 2.0f);
    CHECK(r.back() == 5.0f);
}

TEST_CASE("HistoryRing: order survives many wraps") {
    // Several laps of the buffer, so a seam that drifts by one per wrap shows up.
    HistoryRing<float, 4> r;
    for (int i = 1; i <= 23; ++i) r.push(static_cast<float>(i));
    CHECK(r.size() == 4);
    CHECK(drain(r) == std::vector<float>{ 20.0f, 21.0f, 22.0f, 23.0f });
    CHECK(r.back() == 23.0f);

    // ...and at an exact multiple of capacity, where head returns to zero.
    HistoryRing<float, 4> r2;
    for (int i = 1; i <= 24; ++i) r2.push(static_cast<float>(i));
    CHECK(drain(r2) == std::vector<float>{ 21.0f, 22.0f, 23.0f, 24.0f });
}

TEST_CASE("HistoryRing: clear resets the seam, not just the count") {
    // A clear that zeroed size but left head where it was would produce a
    // correctly-sized ring reading from the wrong offset -- invisible until the
    // graph looked subtly wrong.
    HistoryRing<float, 4> r;
    for (int i = 1; i <= 7; ++i) r.push(static_cast<float>(i));   // head is mid-buffer
    r.clear();
    CHECK(r.empty());
    CHECK(r.size() == 0);
    r.push(99.0f);
    r.push(98.0f);
    CHECK(drain(r) == std::vector<float>{ 99.0f, 98.0f });
    CHECK(r[0] == 99.0f);
}

TEST_CASE("HistoryRing: copies are independent") {
    HistoryRing<float, 4> a;
    for (float f : { 1.0f, 2.0f, 3.0f }) a.push(f);
    HistoryRing<float, 4> b = a;
    b.push(4.0f);
    CHECK(a.size() == 3);
    CHECK(b.size() == 4);
    CHECK(a.back() == 3.0f);
    CHECK(b.back() == 4.0f);
}

TEST_CASE("HistoryRing: holds a non-scalar sample") {
    // The stick trails store a pair, not a float.
    struct Pt { float x, y; };
    HistoryRing<Pt, 3> r;
    r.push({ 1.0f, 2.0f });
    r.push({ 3.0f, 4.0f });
    r.push({ 5.0f, 6.0f });
    r.push({ 7.0f, 8.0f });          // wraps
    CHECK(r.size() == 3);
    CHECK(r[0].x == 3.0f);
    CHECK(r[0].y == 4.0f);
    CHECK(r.back().x == 7.0f);
    CHECK(r.back().y == 8.0f);
}

TEST_CASE("HistoryRing: a full-capacity walk matches what a strip chart draws") {
    // The real access pattern: 200 samples, read [i] and [i+1] across the whole
    // buffer. Every consecutive pair must be consecutive in insertion order.
    constexpr std::size_t N = 200;
    HistoryRing<float, N> r;
    for (int i = 0; i < 517; ++i) r.push(static_cast<float>(i));
    REQUIRE(r.size() == N);
    bool consecutive = true;
    for (std::size_t i = 0; i + 1 < r.size(); ++i)
        if (r[i + 1] - r[i] != 1.0f) consecutive = false;
    CHECK(consecutive);
    CHECK(r.back() == 516.0f);
    CHECK(r[0] == 516.0f - static_cast<float>(N - 1));
}
