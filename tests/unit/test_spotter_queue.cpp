// ============================================================================
// tests/unit/test_spotter_queue.cpp
// Pins the spotter cue queue's policy (core/spotter_queue.h): FIFO order, the
// drop-OLDEST overflow rule, the perishable-cue expiry at pop, and the turn
// policy (a clip finishes unless an urgent call cuts in).
//
// The overflow rule is worth pinning because a refactor that flips it to
// reject-newest would still "bound the queue" and pass a size assert, but a
// race's final-lap cue would then lose to a stale one from minutes earlier.
//
// Expiry is worth pinning because it is invisible from the outside: a queue
// that never expires anything behaves identically until the audio pipeline
// backs up, and then it starts calling a rider alongside who has long gone.
// ============================================================================
#include "doctest.h"

#include "core/spotter_queue.h"

// Non-perishable cues ignore the clock entirely, so tests about ordering and
// overflow pass a fixed tick and stay about ordering and overflow.
static constexpr uint64_t kT0 = 100000;

TEST_CASE("SpotterCueQueue preserves FIFO order across mixed cue kinds") {
    SpotterCueQueue q;
    CHECK(q.empty());

    CHECK_FALSE(q.push({SpotterCue::Kind::Speech, "first", {}, {}}));
    CHECK_FALSE(q.push({SpotterCue::Kind::WavFile, "second.wav", {}, {}}));
    CHECK_FALSE(q.push({SpotterCue::Kind::Speech, "third", {}, {}}));
    CHECK(q.size() == 3);

    SpotterCue cue;
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.kind == SpotterCue::Kind::Speech);
    CHECK(cue.payload == "first");
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.kind == SpotterCue::Kind::WavFile);
    CHECK(cue.payload == "second.wav");
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.payload == "third");
    CHECK(q.empty());
}

TEST_CASE("SpotterCueQueue pop on empty returns false and leaves out untouched") {
    SpotterCueQueue q;
    SpotterCue cue{SpotterCue::Kind::WavFile, "sentinel", {}, {}};
    CHECK_FALSE(q.pop(cue, kT0));
    CHECK(cue.payload == "sentinel");
}

TEST_CASE("SpotterCueQueue overflow drops the OLDEST cue, not the newest") {
    SpotterCueQueue q;
    for (size_t i = 0; i < SpotterCueQueue::kMaxPending; ++i) {
        CHECK_FALSE(q.push({SpotterCue::Kind::Speech, std::to_string(i), {}, {}}));
    }
    CHECK(q.size() == SpotterCueQueue::kMaxPending);

    // Push one past the cap: reports the drop, size stays capped, and the
    // SURVIVORS are 1..kMaxPending-1 plus the newcomer — cue "0" is gone.
    CHECK(q.push({SpotterCue::Kind::Speech, "newest", {}, {}}));
    CHECK(q.size() == SpotterCueQueue::kMaxPending);

    SpotterCue cue;
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.payload == "1");
    // Drain to the end: the newest cue must have survived.
    while (q.pop(cue, kT0)) {}
    CHECK(cue.payload == "newest");
}

TEST_CASE("SpotterCueQueue clear empties the queue") {
    SpotterCueQueue q;
    q.push({SpotterCue::Kind::Speech, "a", {}, {}});
    q.push({SpotterCue::Kind::Speech, "b", {}, {}});
    q.clear();
    CHECK(q.empty());
    SpotterCue cue;
    CHECK_FALSE(q.pop(cue, kT0));
}

// Helper: a cue that describes where a rider is right now.
static SpotterCue perishable(const char* text, uint64_t atMs) {
    SpotterCue cue{SpotterCue::Kind::Speech, text, {}, {}};
    cue.perishable = true;
    cue.enqueuedMs = atMs;
    return cue;
}

TEST_CASE("SpotterCueQueue drops a perishable cue that waited too long") {
    SpotterCueQueue q;
    q.push(perishable("rider left", kT0));

    // Still fresh at the boundary — expiry is strictly greater-than, so a cue
    // popped exactly on kPerishMs is spoken rather than silently swallowed.
    SpotterCue cue;
    REQUIRE(q.pop(cue, kT0 + SpotterCueQueue::kPerishMs));
    CHECK(cue.payload == "rider left");

    q.push(perishable("rider left", kT0));
    CHECK_FALSE(q.pop(cue, kT0 + SpotterCueQueue::kPerishMs + 1));
    CHECK(q.empty());
}

TEST_CASE("SpotterCueQueue never expires a non-perishable cue") {
    SpotterCueQueue q;
    q.push({SpotterCue::Kind::Speech, "fastest lap", {}, {}});

    // A lap time is a fact about something that happened; it stays true no
    // matter how long the worker took to get to it.
    SpotterCue cue;
    REQUIRE(q.pop(cue, kT0 + SpotterCueQueue::kPerishMs * 1000));
    CHECK(cue.payload == "fastest lap");
}

TEST_CASE("SpotterCueQueue skips past stale cues to reach a fresh one") {
    SpotterCueQueue q;
    q.push(perishable("rider left", kT0));
    q.push(perishable("rider right", kT0));
    q.push({SpotterCue::Kind::Speech, "penalty", {}, {}});

    // The backlog case: the worker was busy, both position calls went stale
    // while it spoke. It must not deliver either, and must not stall on them —
    // the penalty behind them is still worth hearing.
    const uint64_t late = kT0 + SpotterCueQueue::kPerishMs + 500;
    SpotterCue cue;
    REQUIRE(q.pop(cue, late));
    CHECK(cue.payload == "penalty");
    CHECK(q.empty());
}

TEST_CASE("SpotterCueQueue tolerates a clock that appears to go backwards") {
    SpotterCueQueue q;
    q.push(perishable("rider behind", kT0));

    // GetTickCount64 is monotonic, but the queue must not turn an unexpected
    // now < enqueuedMs into a giant unsigned age that swallows a fresh cue.
    SpotterCue cue;
    REQUIRE(q.pop(cue, kT0 - 5000));
    CHECK(cue.payload == "rider behind");
}

// TURN POLICY. A recorded pack talked over itself: winmm has one channel, so
// every clip started cut the one before it off mid-word. These pin what the
// worker now does instead - wait for the clip, unless an urgent call is
// pending - and that the urgent call jumps the line rather than waiting
// behind the cues that made the clip late in the first place.
static SpotterCue urgent(const char* text, uint64_t atMs) {
    SpotterCue cue = perishable(text, atMs);
    cue.cutsIn = true;
    return cue;
}

TEST_CASE("SpotterCueQueue next(): waits out a clip unless a cue cuts in") {
    SpotterCueQueue q;
    using Next = SpotterCueQueue::Next;
    const uint64_t clipEnds = kT0 + 2000;

    CHECK(q.next(kT0, clipEnds) == Next::Idle);
    CHECK(q.next(kT0, 0) == Next::Idle);

    q.push({SpotterCue::Kind::WavFile, "session best.wav", {}, {}});
    CHECK(q.next(kT0, clipEnds) == Next::Wait);       // mid-clip: wait
    CHECK(q.next(clipEnds, clipEnds) == Next::Play);  // clip over: its turn
    CHECK(q.next(kT0, 0) == Next::Play);              // nothing sounding

    q.push(urgent("rider left", kT0));
    CHECK(q.next(kT0, clipEnds) == Next::CutIn);
    // ...but not once it has gone stale: a dead call cuts nothing off.
    CHECK(q.next(kT0 + SpotterCueQueue::kPerishMs + 1, clipEnds) == Next::Wait);
}

TEST_CASE("SpotterCueQueue popCutIn(): the urgent call jumps the line") {
    SpotterCueQueue q;
    q.push({SpotterCue::Kind::WavFile, "fastest lap.wav", {}, {}});
    q.push(perishable("clear", kT0));                    // waits its turn
    q.push(urgent("rider right", kT0));
    q.push({SpotterCue::Kind::WavFile, "fuel low.wav", {}, {}});

    SpotterCue cue;
    REQUIRE(q.popCutIn(cue, kT0));
    CHECK(cue.payload == "rider right");
    // Everything else keeps its order behind it.
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.payload == "fastest lap.wav");
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.payload == "clear");
    REQUIRE(q.pop(cue, kT0));
    CHECK(cue.payload == "fuel low.wav");
    CHECK_FALSE(q.popCutIn(cue, kT0));
}

TEST_CASE("SpotterCueQueue popCutIn(): stale cues it passes are dropped") {
    SpotterCueQueue q;
    q.push(urgent("rider left", kT0));                   // stale by then
    q.push(perishable("clear", kT0));                    // stale by then
    q.push(urgent("rider behind", kT0 + 2000));
    q.push({SpotterCue::Kind::WavFile, "lap.wav", {}, {}});

    SpotterCue cue;
    const uint64_t now = kT0 + 2000;
    REQUIRE(q.popCutIn(cue, now));
    CHECK(cue.payload == "rider behind");
    CHECK(q.size() == 1);
    REQUIRE(q.pop(cue, now));
    CHECK(cue.payload == "lap.wav");
}
