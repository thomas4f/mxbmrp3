// ============================================================================
// tests/unit/test_thread_detach_grace.cpp
// The spin-then-detach teardown POLICY (core/thread_detach_grace.h).
//
// WHAT THIS CAN AND CANNOT PIN. The fault the grace exists for is a scheduler
// race — a worker preempted inside its lambda epilogue while FreeLibrary unmaps
// the code underneath it. That is probabilistic and Windows-specific; the only
// evidence for the 150ms is a measurement (5/24 -> 0/24 faults in
// teardown_test under CPU saturation), and no headless test can turn it into an
// assert. See the header.
//
// What IS a decision rather than a measurement, and therefore testable, is the
// POLICY around it:
//   - the grace is paid only when the finished flag actually LANDED (if the OS
//     terminated the thread the flag never arrives, there is no epilogue left
//     to protect, and paying anyway would just lengthen every hard teardown)
//   - the spin is BOUNDED, so a flag that never lands cannot hang process exit
//   - the thread is always detached, never joined, on every path
//
// Those three were previously implicit in three hand-copied bodies. They are
// what a future edit is most likely to get wrong, and they hold in
// milliseconds, so the durations are parameters (GracePolicy) rather than
// literals — production takes the defaults and no call site passes one.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/core/thread_detach_grace.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::milliseconds;

int elapsedMs(Clock::time_point start) {
    return static_cast<int>(std::chrono::duration_cast<Ms>(Clock::now() - start).count());
}

}  // namespace

TEST_CASE("teardown grace: a worker that finishes gets the grace, not the full spin") {
    std::atomic<bool> finished{false};
    std::atomic<bool> stop{false};
    std::thread t([&] {
        while (!stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(Ms(1));
        }
        finished.store(true, std::memory_order_release);
    });

    ThreadTeardown::GracePolicy policy;
    policy.spinLimit = Ms(2000);
    policy.grace = Ms(60);
    policy.poll = Ms(1);

    stop.store(true, std::memory_order_release);
    const auto begin = Clock::now();
    ThreadTeardown::spinThenDetach(t, finished, policy);
    const int took = elapsedMs(begin);

    // Detached, never joined — joining here is what deadlocks on the loader lock.
    CHECK_FALSE(t.joinable());
    // Paid the grace...
    CHECK(took >= 55);
    // ...but nothing like the spin limit: it left as soon as the flag landed.
    CHECK(took < 1000);
}

TEST_CASE("teardown grace: a flag that never lands is bounded, and skips the grace") {
    // Models the ExitProcess-without-Shutdown() teardown: the OS already
    // terminated the worker, so the finished flag will never be stored. An
    // unbounded spin here would hang process exit forever.
    std::atomic<bool> neverFinishes{false};
    std::thread t([] { std::this_thread::sleep_for(Ms(400)); });

    ThreadTeardown::GracePolicy policy;
    policy.spinLimit = Ms(80);
    policy.grace = Ms(500);   // deliberately huge: must NOT be paid
    policy.poll = Ms(1);

    const auto begin = Clock::now();
    ThreadTeardown::spinThenDetach(t, neverFinishes, policy);
    const int took = elapsedMs(begin);

    CHECK_FALSE(t.joinable());
    CHECK(took >= 75);            // waited the bound
    CHECK(took < 400);            // and stopped there — the 500ms grace was skipped
}

TEST_CASE("teardown grace: an already-finished worker costs only the grace") {
    std::atomic<bool> finished{true};
    std::thread t([] {});
    // Let it actually exit so the detach below has nothing left to run.
    std::this_thread::sleep_for(Ms(20));

    ThreadTeardown::GracePolicy policy;
    policy.spinLimit = Ms(2000);
    policy.grace = Ms(40);
    policy.poll = Ms(1);

    const auto begin = Clock::now();
    ThreadTeardown::spinThenDetach(t, finished, policy);
    const int took = elapsedMs(begin);

    CHECK_FALSE(t.joinable());
    CHECK(took >= 35);
    CHECK(took < 1000);
}

TEST_CASE("teardown grace: a non-joinable thread is a no-op") {
    // The normal path: Shutdown() already joined the worker, so the destructor
    // must cost nothing at all rather than sleeping through spin + grace.
    std::thread t;
    ThreadTeardown::GracePolicy policy;
    policy.spinLimit = Ms(2000);
    policy.grace = Ms(2000);
    std::atomic<bool> finished{false};

    const auto begin = Clock::now();
    ThreadTeardown::spinThenDetach(t, finished, policy);
    CHECK(elapsedMs(begin) < 50);
}
