#include "atomic_file_writer.h"

#include "thread_detach_grace.h"
#include "thread_safety.h"
#include "../diagnostics/logger.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>    // std::remove, snprintf
#include <deque>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_set>

namespace {

// Per-module write sequence; its ADDRESS is the module's identity (below).
std::atomic<unsigned> s_sequence{0};

// A temp file this age or older is a writer that never got to its rename: a
// write takes milliseconds, so nothing that old is in flight. Younger temps
// may be another writer's and are left alone. 100 ns units, as FILETIME.
constexpr unsigned long long STALE_TEMP_AGE = 10ULL * 60 * 10'000'000;   // 10 minutes

// A temp name no other writer can be using. Two instances of the plugin in one
// game (a duplicate .dlo in plugins\ - a real support case) both write the same
// files at launch, and a shared temp name let one instance's truncate-and-write
// race the other's rename, so a partly written or empty temp could be moved into
// place. The process id alone does not tell them apart -- one game, one process
// -- so the name also carries the address of a static in THIS module (a second
// copy of the DLL loads at another base) and a per-module sequence (two threads
// of one instance). Another process on the same folder differs in the pid.
std::string tempNameFor(const std::string& path) {
    char suffix[64];
    snprintf(suffix, sizeof(suffix), ".%lu-%llx-%u.tmp",
             static_cast<unsigned long>(GetCurrentProcessId()),
             static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&s_sequence)),
             s_sequence.fetch_add(1, std::memory_order_relaxed));
    return path + suffix;
}

unsigned long long asTicks(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

// Delete the temps a killed writer left next to `path`. Unique names mean a
// leaked temp is never overwritten by the next write, so without this each
// crash mid-write left one more file in Documents for good. Done after a
// successful replace, where the caller has just paid for the write anyway;
// the pattern is served by the file system, so this is one cheap listing.
// The pre-unique-name "<path>.tmp" is swept too, on the same age rule.
void sweepStaleTemps(const std::string& path) {
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    const unsigned long long now = asTicks(nowFt);
    const auto isStale = [now](const FILETIME& written) {
        const unsigned long long w = asTicks(written);
        return now > w && now - w >= STALE_TEMP_AGE;
    };

    const size_t slash = path.find_last_of("\\/");
    const std::string dir = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + ".*.tmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (isStale(fd.ftLastWriteTime)) {
                const std::string stale = dir + fd.cFileName;
                if (std::remove(stale.c_str()) == 0) {
                    DEBUG_INFO_F("[AtomicFileWriter] removed stale temp: %s", stale.c_str());
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    const std::string legacy = path + ".tmp";
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (GetFileAttributesExA(legacy.c_str(), GetFileExInfoStandard, &attr) && isStale(attr.ftLastWriteTime)) {
        if (std::remove(legacy.c_str()) == 0) {
            DEBUG_INFO_F("[AtomicFileWriter] removed stale temp: %s", legacy.c_str());
        }
    }
}

// The paths whose last write failed (needsRetry). A file-scope static rather than a
// Worker member: inline writes record here too, before the worker exists or after it
// stopped. Constructed at DLL load, so it outlives every singleton that asks.
Mutex s_failedMutex;
std::unordered_set<std::string> s_failedPaths MXB_GUARDED_BY(s_failedMutex);

void recordOutcome(const std::string& path, bool ok) {
    MutexLock lock(s_failedMutex);
    if (ok) {
        s_failedPaths.erase(path);
    } else {
        s_failedPaths.insert(path);
    }
}

// ---------------------------------------------------------------------------
// The worker.
// ---------------------------------------------------------------------------

struct Job {
    std::string path;
    std::string bytes;
};

// Outside the Worker object on purpose: the Logger reads s_running on every line, from any
// thread, and must never be the one to construct the Worker (it would then outlive it).
// Trivial atomics have no destructor to race static teardown.
std::atomic<bool> s_running{false};
std::atomic<bool> s_workerEnabled{true};
std::atomic<unsigned> s_failures{0};
// mt-plain: game thread only (start() / stop()). Separate from s_running, which a dead
// worker drops: stop() must still join it and write what it left.
bool s_started = false;

// How long the worker sleeps with nothing queued: the Logger's buffered lines reach the
// file at least this often.
constexpr std::chrono::milliseconds LOG_FLUSH_INTERVAL{1000};

class Worker {
public:
    // Constructed by start() only, which runs after Logger::initialize(), so this is
    // destroyed BEFORE the Logger at static teardown and the backstop below may log.
    static Worker& get() {
        static Worker w;
        return w;
    }

    Mutex mutex;
    std::condition_variable wakeCv;   // the worker: a job, a log wake, or stop
    std::condition_variable idleCv;   // flush(): the queue drained and nothing in flight
    std::deque<Job> queue MXB_GUARDED_BY(mutex);
    bool busy MXB_GUARDED_BY(mutex) = false;
    bool stopRequested MXB_GUARDED_BY(mutex) = false;
    bool logWake MXB_GUARDED_BY(mutex) = false;
    std::atomic<bool> finished{false};
    // joined-by: AtomicFileWriter::stop() (PluginManager::shutdown, after the last save, and
    // the initialize() rollback); the destructor below spins then detaches. Touched only by
    // start()/stop() on the game thread and by the destructor.
    std::thread thread;

    ~Worker() MXB_NO_TSA;   // see the definition

private:
    Worker() = default;
};

// Teardown backstop, reached only when the DLL is unloaded WITHOUT the Shutdown() export
// (thread_detach_grace.h has the why of spin-then-detach). The worker drains what is queued
// before it sets `finished`: a few file writes, no waits on another thread and nothing that
// takes the loader lock, and the spin bounds it regardless. MXB_NO_TSA: the stop flag is set
// under a TRY-lock, because a thread ExitProcess killed may have died holding the mutex and
// a blocking lock here would hang the unload.
Worker::~Worker() {
    s_running.store(false);
    if (!thread.joinable()) return;
    for (int i = 0; i < 100; ++i) {
        if (mutex.try_lock()) {
            stopRequested = true;
            mutex.unlock();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    wakeCv.notify_all();
    ThreadTeardown::spinThenDetach(thread, finished);
}

void runWorker(Worker& w) {
    // Top-level guard (an uncaught throw in a thread is std::terminate). Should the loop
    // ever die, the writer falls back to inline writes, and flush() must not wait on a
    // worker that is gone: it gives up once s_running drops. stop() writes what is left.
    try {
        for (;;) {
            Job job;
            bool haveJob = false;
            bool exiting = false;
            {
                CvLock lk(w.mutex);
                if (w.queue.empty() && !w.stopRequested && !w.logWake) {
                    w.wakeCv.wait_for(lk.native(), LOG_FLUSH_INTERVAL);
                }
                w.logWake = false;
                if (!w.queue.empty()) {
                    job = std::move(w.queue.front());
                    w.queue.pop_front();
                    w.busy = true;
                    haveJob = true;
                } else if (w.stopRequested) {
                    exiting = true;   // stop() is waiting, and everything queued is written
                }
            }
            if (haveJob) {
                bool ok = false;
                try {
                    ok = AtomicFileWriter::writeNow(job.path, job.bytes);
                } catch (...) {
                    ok = false;
                }
                if (!ok) s_failures.fetch_add(1);
                {
                    MutexLock lock(w.mutex);
                    w.busy = false;
                }
                w.idleCv.notify_all();
            }
            // Every pass, so a write's own warning reaches the file with it. Cheap when the
            // Logger has nothing buffered.
            try {
                Logger::getInstance().flushPending();
            } catch (...) {}
            if (exiting) break;
        }
    } catch (...) {
        s_running.store(false);
        w.idleCv.notify_all();
    }
    w.finished.store(true, std::memory_order_release);
}

bool writeAtomic(const std::string& path, const std::string& bytes) {
    const std::string temp = tempNameFor(path);
    {
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            DEBUG_WARN_F("[AtomicFileWriter] cannot open temp for write: %s", temp.c_str());
            return false;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.close();
        if (f.fail()) {
            DEBUG_WARN_F("[AtomicFileWriter] failed writing temp: %s", temp.c_str());
            std::remove(temp.c_str());
            return false;
        }
    }
    // Atomic replace; MOVEFILE_WRITE_THROUGH flushes to disk. The one copy of this
    // pattern for every persisted file.
    if (!MoveFileExA(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DEBUG_WARN_F("[AtomicFileWriter] atomic replace failed (err %lu): %s",
                     static_cast<unsigned long>(GetLastError()), path.c_str());
        std::remove(temp.c_str());
        return false;
    }
    sweepStaleTemps(path);
    return true;
}

}  // namespace

bool AtomicFileWriter::writeNow(const std::string& path, const std::string& bytes) {
    const bool ok = writeAtomic(path, bytes);
    recordOutcome(path, ok);
    return ok;
}

bool AtomicFileWriter::needsRetry(const std::string& path) {
    MutexLock lock(s_failedMutex);
    return s_failedPaths.count(path) != 0;
}

bool AtomicFileWriter::submit(const std::string& path, std::string bytes) {
    if (s_running.load()) {
        Worker& w = Worker::get();
        bool queued = false;
        {
            MutexLock lock(w.mutex);
            if (s_running.load()) {   // stop() clears it under this lock
                // A write for this path still waiting is superseded: the newer state takes
                // its slot, and the older bytes are never written.
                for (Job& j : w.queue) {
                    if (j.path == path) {
                        j.bytes = std::move(bytes);
                        return true;
                    }
                }
                w.queue.push_back(Job{path, std::move(bytes)});
                queued = true;
            }
        }
        if (queued) {
            w.wakeCv.notify_one();
            return true;
        }
    }
    // Reached only when nothing was queued: both moves above are followed by a return.
    return writeNow(path, bytes);  // NOLINT(bugprone-use-after-move): see above
}

void AtomicFileWriter::flush() {
    if (!s_running.load()) return;
    Worker& w = Worker::get();
    CvLock lk(w.mutex);
    while ((!w.queue.empty() || w.busy) && s_running.load()) {
        w.idleCv.wait(lk.native());
    }
}

unsigned AtomicFileWriter::failureCount() {
    return s_failures.load();
}

void AtomicFileWriter::start() {
    if (s_started || !s_workerEnabled.load()) return;
    Worker& w = Worker::get();
    {
        MutexLock lock(w.mutex);
        w.stopRequested = false;
        w.logWake = false;
    }
    w.finished.store(false);
    w.thread = std::thread([&w]() { runWorker(w); });
    s_started = true;
    s_running.store(true);
    DEBUG_INFO("[AtomicFileWriter] disk writer thread started");
}

void AtomicFileWriter::stop() {
    if (!s_started) return;
    s_started = false;
    Worker& w = Worker::get();
    {
        MutexLock lock(w.mutex);
        w.stopRequested = true;
    }
    w.wakeCv.notify_all();
    if (w.thread.joinable()) w.thread.join();

    // The worker left with the queue empty; a submit that raced its exit is written here.
    std::deque<Job> left;
    {
        MutexLock lock(w.mutex);
        s_running.store(false);
        left.swap(w.queue);
        w.stopRequested = false;
    }
    for (const Job& j : left) {
        if (!writeNow(j.path, j.bytes)) s_failures.fetch_add(1);
    }
    DEBUG_INFO("[AtomicFileWriter] disk writer thread stopped");
}

void AtomicFileWriter::wakeForLog() {
    if (!s_running.load()) return;
    Worker& w = Worker::get();
    {
        MutexLock lock(w.mutex);
        w.logWake = true;
    }
    w.wakeCv.notify_one();
}

bool AtomicFileWriter::isRunning() {
    return s_running.load();
}

void AtomicFileWriter::testSetWorkerEnabled(bool enabled) {
    s_workerEnabled.store(enabled);
}
