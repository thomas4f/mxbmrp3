// ============================================================================
// core/analytics_manager_identity.cpp
// AnalyticsManager's install identity: where the analytics file lives, the
// three-state load (missing / locked / corrupt - the distinction is the whole
// point, see core/analytics_identity.h), and the once-per-launch rewrite.
// Split out of analytics_manager.cpp when that file crossed the file budget;
// the class and its API are unchanged.
// ============================================================================
#include "analytics_manager.h"
#include "analytics_manager_internal.h"
#include "analytics_identity.h"
#include "atomic_file_writer.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <windows.h>
#include <fstream>
#include <iterator>
#include <string>

using namespace AnalyticsInternal;

namespace {

constexpr const char* ANALYTICS_SUBDIRECTORY = "mxbmrp3";
constexpr const char* ANALYTICS_FILENAME = "mxbmrp3_analytics.json";

}  // namespace

void AnalyticsManager::loadAndUpdateIdentity(const char* savePath) {
    m_installId.clear();
    m_prevVersion.clear();
    m_firstSeenUnix = 0;
    m_launchCount = 0;
    m_prevSessionStart = 0;
    m_sessionStartUnix = epochSecondsNow();   // this launch's start time

    // Resolve <savePath>/mxbmrp3/mxbmrp3_analytics.json (mirrors StatsManager).
    std::string dir;
    if (!savePath || savePath[0] == '\0') {
        dir = std::string(".\\") + ANALYTICS_SUBDIRECTORY;
    } else {
        dir = savePath;
        if (dir.back() != '/' && dir.back() != '\\') dir += '\\';
        dir += ANALYTICS_SUBDIRECTORY;
    }
    if (!CreateDirectoryA(dir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("AnalyticsManager: could not create %s (error %lu)", dir.c_str(), err);
        }
    }
    std::string path = dir + "\\" + ANALYTICS_FILENAME;
    // Marker the crash handler writes next to the analytics file on a fault.
    m_pendingCrashPath = dir + "\\pending_crash.json";

    // Three states, told apart on purpose (core/analytics_identity.h has the
    // story): the file is missing (first run), it exists but cannot be OPENED
    // (a lock, e.g. an AV scanner or a sync client - treated as transient: a
    // session-only id and the file left alone, so the real id survives), or it
    // opens but does not PARSE (corrupt - never coming back on its own, so the
    // id is recovered from the text where possible and the file is rewritten).
    // The corrupt case used to be handled like the locked one, and an install
    // that hit it was a new install on every launch for the rest of its life.
    const bool fileExists = (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    bool existingUnreadable = false;
    bool existingCorrupt = false;
    std::string id, raw;
    unsigned long long firstSeen = 0, launches = 0;
    m_identityRepair = IdentityRepair::NONE;

    if (fileExists) {
        std::ifstream in(path, std::ios::binary);
        if (in.is_open()) {
            raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            try {
                const nlohmann::json j = nlohmann::json::parse(raw);
                id = j.value("installId", "");
                m_prevVersion = j.value("lastVersion", "");  // "" if absent (pre-2.1 file)
                firstSeen = j.value("firstSeen", 0ULL);
                launches = j.value("launchCount", 0ULL);
                // Previous launch's start time: lets us recover a crashed
                // session's duration (crashTime - prevStart) next launch.
                m_prevSessionStart = j.value("sessionStart", 0ULL);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("AnalyticsManager: analytics file does not parse (%s); repairing", e.what());
                existingCorrupt = true;
            }
        } else {
            existingUnreadable = true;  // present but not openable (locked?)
        }
    }

    if (existingUnreadable) {
        // Use a session-only id and leave the file intact, so the persisted id
        // (and counters) survive if the read failure was transient.
        DEBUG_WARN("AnalyticsManager: analytics file unreadable; using session-only id");
        m_installId = generateUuidV4();   // may be "" on RNG failure; caller handles it
        m_firstSeenUnix = epochSecondsNow();
        m_launchCount = 0;                 // unknown this run
        return;
    }

    if (existingCorrupt) {
        // Whatever the damaged text still says, whole values only. A UUID
        // survives most truncations (the keys are sorted: it is the second one,
        // right after firstSeen); a counter that does not is simply restarted.
        // Either way the write below replaces the file with a clean one, so
        // this happens once per corruption, not once per launch.
        id = AnalyticsIdentity::recoverInstallId(raw);
        m_prevVersion = AnalyticsIdentity::quotedValue(raw, "lastVersion");
        firstSeen = AnalyticsIdentity::recoverUnsigned(raw, "firstSeen");
        launches = AnalyticsIdentity::recoverUnsigned(raw, "launchCount");
        m_prevSessionStart = AnalyticsIdentity::recoverUnsigned(raw, "sessionStart");
        m_identityRepair = id.empty() ? IdentityRepair::NEW_ID : IdentityRepair::RECOVERED;
        DEBUG_WARN_F("AnalyticsManager: %s", id.empty() ? "no install id recoverable; minting a new one"
                                                        : "install id recovered from the damaged file");
    }

    if (id.empty()) {
        id = generateUuidV4();  // first run, or well-formed file missing the key
        if (id.empty()) return;  // RNG failure — leave m_installId empty
    }
    m_installId = id;
    m_firstSeenUnix = (firstSeen != 0) ? firstSeen : epochSecondsNow();  // set once
    m_launchCount = launches + 1;  // count this launch

    // Persist identity + counters at launch (off-track, once per launch — the file is tiny).
    // Route through the shared atomic writer so a crash mid-write can't corrupt it, consistent
    // with every other persisted file.
    try {
        nlohmann::json j;
        j["installId"] = m_installId;
        j["lastVersion"] = PluginConstants::PLUGIN_VERSION;
        j["firstSeen"] = m_firstSeenUnix;
        j["launchCount"] = m_launchCount;
        j["sessionStart"] = m_sessionStartUnix;   // for next-launch crash-duration recovery
        // The writer reports failure rather than throwing. A corrupt file that
        // could not be replaced (read-only, locked by another instance) is
        // repaired again next launch and reports identity_repair again, so say
        // which file it was, once per launch, instead of letting that pass as
        // a completed repair.
        if (!AtomicFileWriter::submit(path, j.dump(2))) {
            DEBUG_WARN_F("AnalyticsManager: analytics file not written, the %s stays for next launch: %s",
                         existingCorrupt ? "corrupt file" : "previous one", path.c_str());
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: failed to write analytics file: %s", e.what());
    }
}
