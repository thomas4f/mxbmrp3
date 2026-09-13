// ============================================================================
// core/update_checker.h
// Checks GitHub for plugin updates
// ============================================================================
#pragma once

#include <string>
#include <thread>
#include <mutex>
#include "thread_safety.h"
#include <atomic>
#include <functional>
#include <vector>
#include <cctype>
#include <algorithm>
#include "plugin_utils.h"

class UpdateChecker {
public:
    enum class Status {
        IDLE,             // Not checked yet
        CHECKING,         // Currently checking
        UP_TO_DATE,       // Current version is latest
        UPDATE_AVAILABLE, // Newer version available
        CHECK_FAILED      // Network/parse error
    };

    enum class UpdateMode {
        OFF,      // Don't check for updates
        NOTIFY    // Check and notify user (manual install via Install button)
    };

    enum class UpdateChannel {
        STABLE,      // Only stable releases (prerelease=false)
        PRERELEASE   // All releases including prereleases
    };

    // Singleton access
    static UpdateChecker& getInstance();

    // Trigger an update check (runs asynchronously)
    void checkForUpdates();

    // Update mode setting (persisted)
    UpdateMode getMode() const { return m_mode; }
    void setMode(UpdateMode mode) { m_mode = mode; }

    // Legacy compatibility - maps to mode != OFF
    bool isEnabled() const { return m_mode != UpdateMode::OFF; }
    void setEnabled(bool enabled) { m_mode = enabled ? UpdateMode::NOTIFY : UpdateMode::OFF; }

    // Update channel setting (persisted) - determines whether prereleases are shown
    UpdateChannel getChannel() const { return m_channel.load(); }
    void setChannel(UpdateChannel channel);  // Clears dismissed version on change
    bool isPrereleaseChannel() const { return m_channel.load() == UpdateChannel::PRERELEASE; }

    // Check if the latest available version is a prerelease
    bool isLatestPrerelease() const;

    // Get current status
    Status getStatus() const { return m_status; }

    // Get latest version string (only valid when UPDATE_AVAILABLE)
    std::string getLatestVersion() const;

    // Get release notes (markdown, only valid when UPDATE_AVAILABLE)
    std::string getReleaseNotes() const;

    // Get download URL for the release asset (only valid when UPDATE_AVAILABLE)
    std::string getDownloadUrl() const;

    // Get expected download size in bytes (only valid when UPDATE_AVAILABLE)
    size_t getDownloadSize() const;

    // Get asset filename (only valid when UPDATE_AVAILABLE)
    std::string getAssetName() const;

    // Get SHA256 checksum hash (only valid when UPDATE_AVAILABLE, may be empty)
    std::string getChecksumHash() const;

    // Dismissed version tracking (user chose to skip this version)
    void setDismissedVersion(const std::string& version);
    std::string getDismissedVersion() const;

    // Check if update notification should be shown (update available AND not dismissed)
    bool shouldShowUpdateNotification() const;

    // THE SIDEBAR'S "Update" TAG on the Updates tab row -- live while an update is
    // available and this version's tag has not been seen, cleared by opening the
    // tab, and re-armed on its own when a NEWER version turns up (the seen state is
    // a version string, not a flag).
    //
    // DELIBERATELY NOT the dismissed-version state above, though the shape is
    // identical. That one means "skip this release" -- an explicit button, which
    // also silences the startup notification. Opening a tab to read about an update
    // is not a decision to skip it, and wiring the tag to that state would have made
    // a glance cost the notification.
    bool shouldShowUpdateTag() const;
    void markUpdateTagSeen();
    void setUpdateTagSeenVersion(const std::string& version);
    std::string getUpdateTagSeenVersion() const;

    // Check if currently checking
    bool isChecking() const { return m_status == Status::CHECKING; }

    // Check if on cooldown (prevent spam)
    bool isOnCooldown() const;

    // Set callback for when check completes (called from worker thread!)
    void setCompletionCallback(std::function<void()> callback);

    // Cleanup (call before shutdown)
    void shutdown();

    // Debug mode: forces update to appear available (for testing)
    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool isDebugMode() const { return m_debugMode; }

#if defined(MXBMRP3_TEST_BUILD)
    // Publish an UPDATE_AVAILABLE result without a network round trip. setDebugMode()
    // is NOT this: it only biases the comparison inside a real check, so headless
    // (no network, no GitHub) it never fires and the update-available UI -- the
    // settings footer's chip and the Version widget's whole notification panel --
    // could not be rendered or asserted at all. Test build only; the whole export
    // surface that reaches it lives in test_hooks.cpp.
    // An EMPTY string clears back to IDLE (the plain "vX.Y.Z" state), so one test can
    // measure both states in a row; anything else publishes it as the latest version.
    void testSetUpdateAvailable(const std::string& latest) {
        {
            MutexLock lock(m_mutex);
            m_latestVersion = latest;
            m_dismissedVersion.clear();   // else shouldShowUpdateNotification() hides it
        }
        m_status = latest.empty() ? Status::IDLE : Status::UPDATE_AVAILABLE;
    }
#endif

    // Compare two version strings, returns: -1 (a < b), 0 (a == b), 1 (a > b).
    // Strips leading "v"/"V", tolerates 3-vs-4 components and "-suffix".
    // NOTE: returns 0 when EITHER string fails to parse, so a caller that must
    // tell "equal" from "unparseable" has to parse it itself. isSameRelease()
    // below is the one that needed to, and does.
    static int compareVersions(const std::string& a, const std::string& b);

    // True if two version strings name the same RELEASE, i.e. they agree on
    // major.minor.patch and both parse. The 4th component is deliberately NOT
    // compared: a release tag is vX.Y.Z (release.yml derives it from resource.h
    // and says so at its tag step), so it parses as build 0, while a running
    // build's PLUGIN_VERSION carries VER_BUILD = the git commit count and is
    // never 0. compareVersions() == 0 therefore NEVER holds between a tag and a
    // running build - it is an ordering test, and equality is a stricter demand
    // than it can meet. Pinned by test_update_version_match.cpp.
    static bool isSameRelease(const std::string& a, const std::string& b) {
        int aMajor, aMinor, aPatch, aBuild;
        int bMajor, bMinor, bPatch, bBuild;
        if (!parseVersion(a, aMajor, aMinor, aPatch, aBuild)) return false;
        if (!parseVersion(b, bMajor, bMinor, bPatch, bBuild)) return false;
        return aMajor == bMajor && aMinor == bMinor && aPatch == bPatch;
    }

    // Choose which release asset to download, given the asset filenames (in the
    // order GitHub returns them). Returns the index of the plugin archive, or -1
    // if none is suitable. Prefers the canonical "mxbmrp3.zip"; otherwise the
    // first .zip that is NOT the debug-symbols bundle (a "*symbols*.zip" contains
    // only .pdb/.map and no .dlo, so installing it aborts with "Release not for
    // this game"). Inline + static so it is unit-testable without pulling in the
    // Windows/WinHTTP-heavy update_checker.cpp.
    static int selectAssetIndex(const std::vector<std::string>& assetNames) {
        auto endsWithZip = [](const std::string& n) {
            return n.size() >= 4 && n.compare(n.size() - 4, 4, ".zip") == 0;
        };
        auto isSymbolsBundle = [](const std::string& n) {
            // Case-insensitive substring match on "symbols" (the asset is
            // "mxbmrp3-symbols-vX.Y.Z.B.zip"); that bundle holds only .pdb/.map.
            std::string lower = n;
            PluginUtils::toLowerAscii(lower);
            return lower.find("symbols") != std::string::npos;
        };

        // Prefer the canonical release archive by exact name.
        for (size_t i = 0; i < assetNames.size(); ++i) {
            if (assetNames[i] == "mxbmrp3.zip") return static_cast<int>(i);
        }
        // Otherwise the first .zip that isn't the debug-symbols bundle. GitHub
        // can return the symbols zip BEFORE the release zip (it sorts earlier),
        // so a naive "first .zip" picks the wrong asset — the regression this
        // guards.
        for (size_t i = 0; i < assetNames.size(); ++i) {
            if (endsWithZip(assetNames[i]) && !isSymbolsBundle(assetNames[i])) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    UpdateChecker();
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    // Worker thread function
    void workerThread();

    // Parse version string to comparable integers (e.g., "1.6.6.0" -> {1,6,6,0}).
    // Defined here rather than in the .cpp so the inline isSameRelease() above
    // can reach it, which is also what makes that check unit-testable.
    //
    // Hand-walked rather than through an istringstream: this header is included
    // by a dozen TUs, and <sstream> is one of the heavier standard headers to
    // drag into all of them for four integers off a dotted string.
    static bool parseVersion(const std::string& version, int& major, int& minor, int& patch, int& build) {
        major = minor = patch = build = 0;

        size_t i = 0;
        // Versions arrive with or without a 'v' prefix.
        if (i < version.size() && (version[i] == 'v' || version[i] == 'V')) ++i;

        // Anything from a hyphen on is a pre-release suffix ("1.11.0.0-beta1").
        const size_t end = (std::min)(version.find('-', i), version.size());

        int* const fields[4] = { &major, &minor, &patch, &build };
        for (int f = 0; f < 4; ++f) {
            const size_t digitsFrom = i;
            long long value = 0;
            while (i < end && version[i] >= '0' && version[i] <= '9') {
                value = value * 10 + (version[i] - '0');
                if (value > 1000000000LL) return false;   // not a version number
                ++i;
            }
            if (i == digitsFrom) return false;            // a field with no digits
            *fields[f] = static_cast<int>(value);
            if (i >= end || version[i] != '.') return true;   // fewer than four is fine
            ++i;                                              // step over the dot
        }
        return true;
    }

    // HTTP fetch (blocking)
    bool fetchLatestRelease(std::string& outVersion, std::string& outError);

    // Safely close all active HTTP handles (called from both shutdown and worker thread).
    // The mutex ensures exactly one caller closes the handles; the other gets nulls.
    void closeHttpHandles();

    std::atomic<Status> m_status;
    std::atomic<UpdateMode> m_mode;  // Persisted setting: Off or Notify
    std::atomic<UpdateChannel> m_channel;  // Persisted setting: Stable or Prerelease
    std::string m_latestVersion MXB_GUARDED_BY(m_mutex);
    std::string m_releaseNotes MXB_GUARDED_BY(m_mutex);   // GitHub release body (markdown)
    std::string m_downloadUrl MXB_GUARDED_BY(m_mutex);    // Asset browser_download_url
    std::string m_assetName MXB_GUARDED_BY(m_mutex);      // Asset filename (e.g., "mxbmrp3-v1.10.3.0.zip")
    size_t m_downloadSize MXB_GUARDED_BY(m_mutex);        // Asset size in bytes
    std::string m_checksumHash MXB_GUARDED_BY(m_mutex);   // SHA256 hash from GitHub digest field
    bool m_latestIsPrerelease MXB_GUARDED_BY(m_mutex);    // Whether the latest available version is a prerelease
    std::string m_dismissedVersion MXB_GUARDED_BY(m_mutex);  // Version user chose to skip (persisted)
    std::string m_tagSeenVersion MXB_GUARDED_BY(m_mutex);    // Version whose sidebar tag was seen (persisted)
    mutable Mutex m_mutex;
    // joined-by: shutdown() (PluginManager::shutdown) + the re-arm join in
    // checkForUpdates() before spawning a replacement.
    std::thread m_workerThread;
    std::atomic<bool> m_shutdownRequested;
    // Copy under the lock, invoke OUTSIDE it (see the worker in update_checker.cpp) —
    // exactly the shape the analysis should be pinning, so it carries the annotation.
    std::function<void()> m_completionCallback MXB_GUARDED_BY(m_mutex);

    // HTTP handle tracking for cross-thread cancellation via WinHttpCloseHandle.
    // All three handles are stored so shutdown can close them explicitly rather than
    // relying on ambiguous cascade-close behavior when only the session is closed.
    Mutex m_httpHandleMutex;
    void* m_hHttpSession MXB_GUARDED_BY(m_httpHandleMutex) {nullptr};
    void* m_hHttpConnect MXB_GUARDED_BY(m_httpHandleMutex) {nullptr};
    void* m_hHttpRequest MXB_GUARDED_BY(m_httpHandleMutex) {nullptr};
    std::atomic<bool> m_debugMode;  // Forces update available for testing
    unsigned long m_lastCheckTimestamp;  // When last check started (for cooldown)

    // GitHub API endpoint (host is always api.github.com, path uses constants from plugin_constants.h)
    static constexpr const char* GITHUB_API_HOST = "api.github.com";
    static constexpr unsigned long CHECK_COOLDOWN_MS = 5000;  // Minimum time between checks (prevent spam)
    // Note: GITHUB_RELEASES_PATH is constructed at runtime using PluginConstants::GITHUB_REPO_OWNER/NAME
};
