#pragma once

#include <string>

// Shared atomic file write for every persistence subsystem (settings, stats, tracked
// riders, rumble profiles). It owns the one temp-file + MoveFileExA replace routine that
// used to be copy-pasted into each manager, so a crash mid-write can never leave a
// half-written (corrupt) file — the old file stays until the new one is complete.
//
// Writes are SYNCHRONOUS: the subsystems that use it persist on discrete, infrequent,
// OFF-TRACK events — leaving the track for the pits, session end, launch, shutdown — where a
// couple of milliseconds of file I/O on the calling thread is invisible. None writes while the
// player is actively riding (settings/stats defer to the leave-track flush), so there is
// deliberately no off-thread queue.
class AtomicFileWriter {
public:
    // Synchronous atomic write on the calling thread (temp file + MoveFileExA replace).
    // Returns false on failure.
    static bool writeFileAtomic(const std::string& path, const std::string& bytes);

private:
    AtomicFileWriter() = delete;
};
