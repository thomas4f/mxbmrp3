// ============================================================================
// hud/settings/settings_tab_updates.cpp
// Tab renderer for Updates settings (auto-update, download, install)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/update_checker.h"
#include "../../core/update_downloader.h"
#include "../../core/color_config.h"
#include "../../core/settings_manager.h"
#include "../../game/game_config.h"
#include "../../diagnostics/logger.h"

#include <sstream>

using namespace PluginConstants;

// Member function of SettingsHud - handles click events for Updates tab
bool SettingsHud::handleClickTabUpdates(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::UPDATE_CHECK_TOGGLE:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                bool newState = !checker.isEnabled();
                checker.setEnabled(newState);
                if (newState && !checker.isChecking()) {
                    // Trigger an update check when enabled
                    checker.setCompletionCallback([this]() {
                        setDataDirty();
                    });
                    checker.checkForUpdates();
                }
                setDataDirty();
            }
            return true;

        case ClickRegion::UPDATE_CHECK_NOW:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                if (!checker.isChecking()) {
                    checker.setCompletionCallback([this]() {
                        setDataDirty();  // Refresh UI when check completes
                        // Note: Don't show version widget notification here - user is already
                        // in Settings and can see the update info directly
                    });
                    checker.checkForUpdates();
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::UPDATE_INSTALL:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                UpdateDownloader& downloader = UpdateDownloader::getInstance();

                if (checker.getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE &&
                    downloader.getState() == UpdateDownloader::State::IDLE) {
                    // Start download with optional checksum verification
                    downloader.setStateChangeCallback([this]() {
                        setDataDirty();  // Refresh UI on state changes
                    });
                    downloader.startDownload(checker.getDownloadUrl(),
                                            checker.getDownloadSize(),
                                            checker.getChecksumHash());
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::UPDATE_SKIP_VERSION:
            // Reset the downloader if it failed (acts as retry)
            {
                UpdateDownloader& downloader = UpdateDownloader::getInstance();
                if (downloader.getState() == UpdateDownloader::State::FAILED) {
                    downloader.reset();
                    setDataDirty();
                }
            }
            return true;

        case ClickRegion::UPDATE_DEBUG_MODE:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                UpdateDownloader& downloader = UpdateDownloader::getInstance();
                bool newState = !checker.isDebugMode();
                checker.setDebugMode(newState);
                downloader.setDebugMode(newState);
                DEBUG_INFO_F("Update debug mode: %s", newState ? "enabled" : "disabled");
                setDataDirty();
            }
            return true;

        case ClickRegion::UPDATE_CHANNEL_UP:
        case ClickRegion::UPDATE_CHANNEL_DOWN:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                auto current = checker.getChannel();
                auto newChannel = (current == UpdateChecker::UpdateChannel::STABLE)
                    ? UpdateChecker::UpdateChannel::PRERELEASE
                    : UpdateChecker::UpdateChannel::STABLE;
                checker.setChannel(newChannel);
                DEBUG_INFO_F("Update channel: %s", newChannel == UpdateChecker::UpdateChannel::PRERELEASE ? "prerelease" : "stable");
                // Trigger new check with updated channel
                if (checker.isEnabled() && !checker.isChecking()) {
                    checker.setCompletionCallback([this]() { setDataDirty(); });
                    checker.checkForUpdates();
                }
                setDataDirty();
            }
            return true;

        default:
            return false;
    }
}

// Static function that renders the Updates tab content
BaseHud* SettingsHud::renderTabUpdates(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("updates");

    ColorConfig& colorConfig = ColorConfig::getInstance();
    // Value columns for this tab's two label+value pairs, in characters from
    // labelX: the version rows align on the longer of "Current:"/"Available:",
    // the progress rows on the widest step label.
    constexpr int VERSION_COLUMN = 11;
    constexpr int STEP_COLUMN = 10;

    ctx.addSectionHeading("Settings");

    // Developer mode only settings
    if (SettingsManager::getInstance().isDeveloperMode()) {
        // Debug mode toggle (for testing updates)
        bool isDebugMode = UpdateChecker::getInstance().isDebugMode();
        ctx.addToggleControl("Debug Mode (test)", isDebugMode,
                            SettingsHud::ClickRegion::UPDATE_DEBUG_MODE, nullptr,
                            nullptr, 0, true, "updates.debug_mode");

        // Update channel selector (Stable / Prerelease)
        bool isPrerelease = UpdateChecker::getInstance().isPrereleaseChannel();
        const char* channelText = isPrerelease ? "Prerelease" : "Stable";
        ctx.addCycleControl("Update Channel", channelText, 10,
                            SettingsHud::ClickRegion::UPDATE_CHANNEL_DOWN,
                            SettingsHud::ClickRegion::UPDATE_CHANNEL_UP,
                            nullptr, true, false, "updates.channel");
    }

    // Check for Updates toggle using < > cycle control
    bool updatesEnabled = UpdateChecker::getInstance().isEnabled();
    ctx.addToggleControl("Check for Updates", updatesEnabled,
                        SettingsHud::ClickRegion::UPDATE_CHECK_TOGGLE, nullptr,
                        nullptr, 0, true, "updates.check_enabled");

    ctx.addSpacing();

    // Check Now button - centered (fixed width like Records HUD Compare button)
    {
        UpdateChecker& checkerForButton = UpdateChecker::getInstance();
        bool isChecking = checkerForButton.isChecking();
        bool isOnCooldown = checkerForButton.isOnCooldown() && !isChecking;
        bool isDisabled = isChecking || isOnCooldown;  // Disabled while checking or on cooldown
        const char* buttonText = isChecking ? "   ...   " : "Check Now";   // both 9 chars (button is 11 wide -> 1ch padding)
        // POSITIVE: this is the confirming action of the tab -- fetch and install.
        // Paired with the red on Reset in the General tab.
        ctx.addActionButton(buttonText, 11, SettingsHud::ClickRegion::UPDATE_CHECK_NOW,
                            SettingsLayoutContext::ButtonRole::Positive, !isDisabled);
    }

    ctx.addSectionHeading("Status");

    // Current version. VERSION_COLUMN is "Available: " -- the longer of the two
    // labels -- so the pair of version rows aligns.
    char currentVersionStr[32];
    snprintf(currentVersionStr, sizeof(currentVersionStr), "v%s", PLUGIN_VERSION);
    ctx.addLabelValueRow("Current:", colorConfig.getSecondary(),
                         currentVersionStr, colorConfig.getSecondary(), VERSION_COLUMN);

    // Show status based on UpdateChecker and UpdateDownloader states
    UpdateChecker& checker = UpdateChecker::getInstance();
    UpdateDownloader& downloader = UpdateDownloader::getInstance();

    auto checkerStatus = checker.getStatus();
    auto downloaderState = downloader.getState();

    // Show "Available:" row when update is available (even during download/install)
    bool isUpdateAvailable = (checkerStatus == UpdateChecker::Status::UPDATE_AVAILABLE);
    bool isDownloading = (downloaderState == UpdateDownloader::State::DOWNLOADING ||
                          downloaderState == UpdateDownloader::State::VERIFYING ||
                          downloaderState == UpdateDownloader::State::EXTRACTING);
    bool isReady = (downloaderState == UpdateDownloader::State::READY);

    if (isUpdateAvailable || isDownloading || isReady) {
        std::string latestVersion = checker.getLatestVersion();
        bool debugMode = checker.isDebugMode();

        char versionPart[64];
        bool isPrerelease = checker.isLatestPrerelease();
        if (debugMode) {
            snprintf(versionPart, sizeof(versionPart), "%s (DEBUG)", latestVersion.c_str());
        } else if (isPrerelease) {
            snprintf(versionPart, sizeof(versionPart), "%s (PRE)", latestVersion.c_str());
        } else {
            snprintf(versionPart, sizeof(versionPart), "%s", latestVersion.c_str());
        }
        // Color: warning for debug, accent for prerelease, positive for stable
        unsigned long versionColor = debugMode ? colorConfig.getWarning()
            : isPrerelease ? colorConfig.getAccent()
            : colorConfig.getPositive();
        ctx.addLabelValueRow("Available:", colorConfig.getSecondary(),
                             versionPart, versionColor, VERSION_COLUMN);

        // Show test directory warning in debug mode
        if (debugMode) {
            ctx.addTextRow("Will extract to mxbmrp3_update_test/", colorConfig.getWarning());
        }
    }

    if (isDownloading) {
        // Show step-by-step progress
        ctx.addSectionHeading("Progress");
        auto steps = downloader.getSteps();
        int stepIndex = 0;
        for (const auto& step : steps) {
            // Status indicator
            char indicatorBuf[16] = "";
            const char* indicator = indicatorBuf;
            unsigned long indicatorColor = colorConfig.getMuted();

            switch (step.status) {
                case UpdateDownloader::StepStatus::COMPLETE:
                    indicator = "OK";
                    indicatorColor = colorConfig.getPositive();
                    break;
                case UpdateDownloader::StepStatus::IN_PROGRESS:
                    // Show percentage for download step only
                    if (stepIndex == 0 && downloaderState == UpdateDownloader::State::DOWNLOADING) {
                        float progress = downloader.getProgress();
                        snprintf(indicatorBuf, sizeof(indicatorBuf), "%.0f%%", progress * 100.0f);
                    }
                    // Other in-progress steps: label color indicates activity, no indicator needed
                    indicatorColor = colorConfig.getAccent();
                    break;
                case UpdateDownloader::StepStatus::SKIPPED:
                    indicator = "skip";
                    indicatorColor = colorConfig.getMuted();
                    break;
                case UpdateDownloader::StepStatus::PENDING:
                    // No indicator for pending - just show the label
                    break;
                default:
                    break;
            }

            unsigned long labelColor = (step.status == UpdateDownloader::StepStatus::IN_PROGRESS)
                ? colorConfig.getAccent()
                : colorConfig.getSecondary();
            ctx.addLabelValueRow(step.label, labelColor, indicator, indicatorColor, STEP_COLUMN);
            stepIndex++;
        }

    } else if (isReady) {
        // Show completed steps
        ctx.addSectionHeading("Progress");
        auto steps = downloader.getSteps();
        for (const auto& step : steps) {
            const char* indicator = "OK";
            unsigned long indicatorColor = colorConfig.getPositive();

            if (step.status == UpdateDownloader::StepStatus::SKIPPED) {
                indicator = "skip";
                indicatorColor = colorConfig.getMuted();
            }

            ctx.addLabelValueRow(step.label, colorConfig.getSecondary(),
                                 indicator, indicatorColor, STEP_COLUMN);
        }

        ctx.addSpacing();

        // Update installed message
        ctx.addTextRow("Update installed!", colorConfig.getPositive());
        ctx.addTextRow("Restart " GAME_NAME " to apply.", colorConfig.getSecondary());

    } else if (downloaderState == UpdateDownloader::State::FAILED) {
        // Download failed
        ctx.addSpacing();
        std::string errorText = "Error: " + downloader.getErrorMessage();
        ctx.addTextRow(errorText.c_str(), colorConfig.getNegative());
        ctx.addSpacing();

        // Retry button - centered
        ctx.addActionButton("Retry", 7, SettingsHud::ClickRegion::UPDATE_SKIP_VERSION);

    } else if (isUpdateAvailable) {
        // Update available - show release notes and install button
        // (Available version row already shown above)

        // Release notes (if available)
        std::string releaseNotes = checker.getReleaseNotes();
        if (!releaseNotes.empty()) {
            ctx.addSectionHeading("Release Notes");

            // Simple line-by-line display (first few lines)
            std::istringstream stream(releaseNotes);
            std::string line;
            int lineCount = 0;
            constexpr int MAX_LINES = 13;
            bool hasMoreLines = false;

            while (std::getline(stream, line)) {
                // Skip empty lines and markdown headers
                if (line.empty() || line[0] == '#') continue;

                if (lineCount >= MAX_LINES) {
                    hasMoreLines = true;
                    break;
                }

                // Truncate long lines
                if (line.size() > 45) {
                    line.resize(42);
                    line += "...";
                }

                ctx.addTextRow(line.c_str(), colorConfig.getSecondary());
                lineCount++;
            }

            // Show truncation note if there are more lines
            if (hasMoreLines) {
                ctx.addSpacing();
                ctx.addTextRow("See GitHub for full release notes.", colorConfig.getMuted());
            }

            ctx.addSpacing();
        }

        // Install button - centered
        ctx.addActionButton("Install Update", 16, SettingsHud::ClickRegion::UPDATE_INSTALL);

    } else if (checkerStatus == UpdateChecker::Status::CHECKING) {
        ctx.addTextRow("Checking for updates...", colorConfig.getSecondary());

    } else if (checkerStatus == UpdateChecker::Status::UP_TO_DATE) {
        ctx.addTextRow("You have the latest version.", colorConfig.getSecondary());

    } else if (checkerStatus == UpdateChecker::Status::CHECK_FAILED) {
        ctx.addTextRow("Could not check for updates.", colorConfig.getMuted());

    } else {
        // IDLE - not checked yet. No leading spacing: the status line sits
        // directly under "Current:" to match the CHECKING / UP_TO_DATE /
        // CHECK_FAILED states (they render with no gap too).
        ctx.addTextRow(checker.isEnabled() ? "Update check pending..."
                                           : "Enable auto-check or click Check Now.",
                       colorConfig.getMuted());
    }

    return nullptr;  // No specific HUD for this tab
}
