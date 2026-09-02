// ============================================================================
// core/thread_safety.h
// Clang Thread Safety Analysis (TSA) wrappers: an annotated Mutex + RAII locks.
//
// THE INVARIANT THIS ENFORCES (CLAUDE.md): a mutex-guarded member is guarded
// at EVERY access site — including private helpers called from already-locked-
// *looking* code. The crash-grade bug was RecordsHud::findPlayerPositionInRecords()
// iterating live m_records unlocked while its caller had carefully copied
// under lock. With members declared MXB_GUARDED_BY(mutex), clang's
// -Wthread-safety turns that class of mistake into a COMPILE ERROR
// (tests/integration/check_thread_safety.sh, CI).
//
// The annotations are attributes only — they expand to nothing outside clang,
// and the wrappers delegate straight to std::mutex — so MSVC (shipping) and
// mingw (test) builds are byte-for-byte unchanged. Clang never builds the
// plugin; it only front-end-parses the TUs for the analysis.
//
// Usage:
//     Mutex m_mutex;                                  // instead of std::mutex
//     std::vector<Rec> m_records MXB_GUARDED_BY(m_mutex);
//     { MutexLock lock(m_mutex);  ...use m_records... }  // instead of lock_guard
//     void helper() MXB_REQUIRES(m_mutex);            // called-under-lock helper
//
// Condition variables: std::condition_variable needs std::unique_lock — use
// CvLock and its native() accessor, and write waits as explicit while-loops
// (TSA can't see through the predicate-lambda overload):
//     CvLock lk(m_mutex);
//     while (!m_ready) m_cv.wait(lk.native());
//
// A deliberate exception (e.g. a destructor that must not block) is annotated
// MXB_NO_TSA with a comment saying why.
// ============================================================================
#pragma once

#include <mutex>

#if defined(__clang__)
#define MXB_TSA(x) __attribute__((x))
#else
#define MXB_TSA(x)
#endif

#define MXB_CAPABILITY(x)   MXB_TSA(capability(x))
#define MXB_SCOPED_CAP      MXB_TSA(scoped_lockable)
#define MXB_GUARDED_BY(x)   MXB_TSA(guarded_by(x))
#define MXB_PT_GUARDED_BY(x) MXB_TSA(pt_guarded_by(x))
#define MXB_REQUIRES(...)   MXB_TSA(requires_capability(__VA_ARGS__))
#define MXB_ACQUIRE(...)    MXB_TSA(acquire_capability(__VA_ARGS__))
#define MXB_RELEASE(...)    MXB_TSA(release_capability(__VA_ARGS__))
#define MXB_EXCLUDES(...)   MXB_TSA(locks_excluded(__VA_ARGS__))
#define MXB_NO_TSA          MXB_TSA(no_thread_safety_analysis)

// std::mutex with the TSA capability attribute (libstdc++'s std::mutex carries
// no annotations, so TSA can't track it directly). Same size/behavior.
class MXB_CAPABILITY("mutex") Mutex {
public:
    void lock() MXB_ACQUIRE() { m_mutex.lock(); }
    void unlock() MXB_RELEASE() { m_mutex.unlock(); }
    // Non-blocking acquire, for a producer that must NEVER wait (the game
    // thread publishing to a render-window thread — skip the publish instead).
    // A function built on this needs MXB_NO_TSA with a stated reason: TSA
    // cannot follow a conditionally-held capability through branches.
    bool try_lock() MXB_TSA(try_acquire_capability(true)) { return m_mutex.try_lock(); }
    // For std::condition_variable via CvLock only — never lock this directly,
    // TSA can't see it.
    std::mutex& native() { return m_mutex; }

private:
    std::mutex m_mutex;
};

// std::lock_guard equivalent over Mutex.
class MXB_SCOPED_CAP MutexLock {
public:
    explicit MutexLock(Mutex& m) MXB_ACQUIRE(m) : m_m(m) { m_m.lock(); }
    ~MutexLock() MXB_RELEASE() { m_m.unlock(); }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

private:
    Mutex& m_m;
};

// std::unique_lock equivalent over Mutex, for condition_variable waits:
// pass native() to cv.wait(). From TSA's static view the capability is held
// for the whole scope (the wait's release/reacquire nets out held-on-return).
class MXB_SCOPED_CAP CvLock {
public:
    explicit CvLock(Mutex& m) MXB_ACQUIRE(m) : m_lk(m.native()) {}
    ~CvLock() MXB_RELEASE() {}
    CvLock(const CvLock&) = delete;
    CvLock& operator=(const CvLock&) = delete;
    std::unique_lock<std::mutex>& native() { return m_lk; }

private:
    std::unique_lock<std::mutex> m_lk;
};
