// ============================================================================
// core/thread_detach_grace.h
// The spin-then-detach teardown backstop, shared by the singletons that own a
// worker thread (XInputReader, PluginThread, CompanionWindow, SpotterManager).
//
// THE CONTRACT ON THE WORKER: after it observes the stop signal, only
// instructions may remain before the finished-flag store -- no waits, and
// nothing that can need the LOADER LOCK the destructing thread holds.
// SpotterManager is the cautionary case: its normal exit releases a SAPI
// voice (waits on the engine's speech thread) and calls CoUninitialize (can
// unload COM DLLs) -- either blocks on the loader lock, the flag never lands,
// and the spin below detaches a LIVE thread. Its destructor therefore sets an
// abandon flag telling the worker to skip that cleanup on this path
// (m_abandonComCleanup); a worker with a similarly heavy exit needs the same.
//
// WHEN THIS RUNS. Only when the orchestrated Shutdown() export was SKIPPED and
// the DLL is unloaded anyway — the path that cost two shipped crashes (see
// teardown_test.cpp). The normal path joins the thread in Shutdown(), so
// joinable() is false by the time the destructor runs and none of this happens.
//
// WHY NOT JOIN. These destructors run during static teardown, i.e. from
// FreeLibrary -> DllMain(DLL_PROCESS_DETACH) -> static dtors, holding the
// WINDOWS LOADER LOCK. std::thread::join() waits for the thread's OS-level
// exit, which also needs that lock: the game HANGS rather than crashes. So:
// signal stop, spin on an app-level flag (no loader lock involved), then detach
// and let the thread's CRT/OS teardown finish without us blocking on it.
//
// THE COST OF WAITING HERE, stated plainly because it is the real trade-off:
// we are sleeping while holding the loader lock, so for the duration every
// other thread in the HOST process that touches module state — LoadLibrary,
// GetModuleHandle, GetProcAddress — blocks behind us. At process exit that is
// free. On a mid-session unload it is not, and mid-session unload is exactly
// the path this fires on. The bound is what keeps it defensible: at most
// spinLimit + grace per worker, and only when Shutdown() was skipped.
//
// BOUNDED SPIN. On an ExitProcess-without-Shutdown() teardown the OS has
// already TERMINATED the worker, so the finished flag will never be stored and
// an unbounded spin would hang process exit forever. Past the deadline we
// detach regardless: a terminated thread makes that trivially safe, and a
// pathologically still-live one lands in the residual window below. The poll
// sleeps rather than yields — on the terminated-thread path the flag never
// lands, so it runs the full spinLimit, and yield() spun a core hot for that
// whole window across all three singletons in sequence.
//
// THE GRACE, AND WHY IT IS NOT ZERO. The finished flag is the LAST statement in
// the thread body, but the lambda epilogue and the std::thread invoke shim
// after it are still OUR code — so detaching the instant the flag lands can let
// FreeLibrary unmap those instructions out from under a preempted thread. Not
// theoretical: under CPU saturation the unload-without-Shutdown() case of
// teardown_test faulted 5/24 runs with "page fault on execute access", and the
// window widens with load because preemption is what makes it lose the race.
//
// This NARROWS the window (a few instructions -> 150ms of slack); it does NOT
// close it, and nothing gates that it stays — 5/24 -> 0/24 is a measurement,
// not a test. A deterministic test for a probabilistic fault would have to pin
// a scheduler race, so the gap is deliberate: delete the grace and the failure
// returns as an intermittent "page fault on execute access" in teardown_test
// under load, not as a red assert. What IS pinned (tests/unit/
// test_thread_detach_grace.cpp) is the POLICY: grace only when the flag landed,
// spin bounded when it never does.
//
// WHY 150ms. The thing being waited out is a preemption, not work: the epilogue
// is a handful of instructions, so the cost is however long the scheduler takes
// to run them again — a quantum, order 1-15ms under Wine. 150ms is ~10x that,
// and measured 5/24 -> 0/24 under full CPU saturation. A margin over a quantum,
// not a derived bound.
//
// ADDITIVE: the singletons run this in sequence, so a no-Shutdown teardown
// pays up to N x (spinLimit + grace) — four workers today. Keeping the
// constants HERE is what makes that arithmetic true — per-site copies of the
// number is the shape where someone tunes one site and the others silently
// keep the old value while the comment still claims the old total.
//
// TWO ways to actually close the window, both rejected, recorded so the choice
// does not look binary:
//   - Pin the module (GET_MODULE_HANDLE_EX_FLAG_PIN): the code can never be
//     unmapped. But the DLL then never unloads, so teardown_test's
//     unload-WITHOUT-Shutdown case stops running static destructors and
//     silently stops testing the backstop it exists for.
//   - FreeLibraryAndExitThread: the worker takes its own module ref at start
//     and drops it as its FINAL act, so the image outlives its last instruction
//     and is still released afterwards — no lifetime change, unlike pinning.
//     The catch is std::thread: its invoker shim runs AFTER the callable
//     returns, so calling this from inside the lambda strands the thread object
//     and skips the shim. Doing it properly means _beginthreadex/CreateThread
//     for all three workers, which is more churn than a hazard that mainly
//     bites the test harness earns. Revisit if this ever faults in a shipped
//     build — and if you do, the ref must be taken WITHOUT
//     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT. That flag looks like the
//     polite one and is the trap: it suppresses the increment, so the worker
//     would free a reference it never held. Note who that hurts — not the
//     worker: it exits into kernel32 right after the decrement and is already
//     out of our code. The victim is whoever STILL holds the module, i.e. the
//     host game, now calling into a plugin that unmapped itself. Not the
//     page-fault-on-execute this grace narrows arriving sooner — a wider one,
//     pointed at the game instead of at teardown.
//
// The caller does its own signalling first, then hands the thread and its
// finished-flag here. Two shapes today: XInputReader and CompanionWindow poll a
// run flag (CompanionWindow's message pump is PeekMessage, so clearing the flag
// is enough — it never blocks in GetMessage), while PluginThread and
// SpotterManager additionally notify the condition variable their workers
// block on. A worker that parks in
// something a flag store cannot wake needs its own nudge added at the call site,
// not here — this function only waits and detaches.
// ============================================================================
#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace ThreadTeardown {

// Durations are parameters, not literals in the body, so the policy can be
// pinned by a unit test in milliseconds rather than seconds. Production always
// takes the defaults — no call site passes this.
struct GracePolicy {
    std::chrono::milliseconds spinLimit{2000};
    std::chrono::milliseconds grace{150};
    std::chrono::milliseconds poll{1};
};

// Spin until `finished` lands (bounded), give the thread a moment to leave our
// code if it did, then detach. Never joins — see the header.
inline void spinThenDetach(std::thread& thread,
                           const std::atomic<bool>& finished,
                           const GracePolicy& policy = GracePolicy{}) {
    if (!thread.joinable()) return;

    const auto deadline = std::chrono::steady_clock::now() + policy.spinLimit;
    while (!finished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(policy.poll);
    }

    // Only when the flag actually LANDED: if it never did, the OS terminated
    // the thread and there is no epilogue left to protect, so the bounded spin
    // above was already the whole cost.
    if (finished.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(policy.grace);
    }

    thread.detach();
}

}  // namespace ThreadTeardown
