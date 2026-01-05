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
    float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    [[maybe_unused]] float rowWidth = ctx.panelWidth - (ctx.labelX - ctx.contentAreaStartX);

    ctx.addSectionHeader("Settings");

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

    ctx.addSpacing(1.0f);

    // Check Now button - centered (fixed width like Records HUD Compare button)
    {
        UpdateChecker& checkerForButton = UpdateChecker::getInstance();
        bool isChecking = checkerForButton.isChecking();
        bool isOnCooldown = checkerForButton.isOnCooldown() && !isChecking;
        bool isDisabled = isChecking || isOnCooldown;  // Disabled while checking or on cooldown
        const char* buttonText = isChecking ? "[   ...   ]" : "[Check Now]";  // Both 11 chars
        float buttonWidth = cw * 11;  // Fixed width for consistent centering
        float buttonHeight = ctx.lineHeightNormal;

        // Center the button like Copy/Reset in General tab
        float buttonCenterX = ctx.contentAreaStartX + (ctx.panelWidth - ctx.paddingH - ctx.paddingH) / 2.0f;
        float buttonX = buttonCenterX - buttonWidth / 2.0f;

        size_t regionIndex = ctx.parent->m_clickRegions.size();
        if (!isDisabled) {
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                buttonX, ctx.currentY, buttonWidth, buttonHeight,
                SettingsHud::ClickRegion::UPDATE_CHECK_NOW, nullptr
            ));
        }

        bool isHovered = !isDisabled && ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex);

        // Background: gray when disabled, purple when active
        SPluginQuad_t bgQuad;
        float bgX = buttonX, bgY = ctx.currentY;
        ctx.parent->applyOffset(bgX, bgY);
        ctx.parent->setQuadPositions(bgQuad, bgX, bgY, buttonWidth, buttonHeight);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = isDisabled ? PluginUtils::applyOpacity(colorConfig.getMuted(), 0.3f)
            : isHovered ? colorConfig.getAccent()
            : PluginUtils::applyOpacity(colorConfig.getAccent(), 0.5f);
        ctx.parent->m_quads.push_back(bgQuad);

        // Text: gray when disabled, accent on accent when active
        unsigned long textColor = isDisabled ? colorConfig.getMuted()
            : isHovered ? colorConfig.getPrimary()
            : colorConfig.getAccent();
        ctx.parent->addString(buttonText, buttonCenterX, ctx.currentY, Justify::CENTER,
            Fonts::getNormal(), textColor, ctx.fontSize);

        ctx.currentY += ctx.lineHeightNormal;
    }

    ctx.addSpacing(1.0f);
    ctx.addSectionHeader("Status");

    // Current version (aligned with "Available:" label width)
    // "Available: " is 11 chars, "Current:   " padded to match
    float labelWidth = cw * 11;  // Width for label column
    ctx.parent->addString("Current:", ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
    char currentVersionStr[32];
    snprintf(currentVersionStr, sizeof(currentVersionStr), "v%s", PLUGIN_VERSION);
    ctx.parent->addString(currentVersionStr, ctx.labelX + labelWidth, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
    ctx.currentY += ctx.lineHeightNormal;

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
        // Show available version (aligned with "Current:" label width)
        std::string latestVersion = checker.getLatestVersion();
        bool debugMode = checker.isDebugMode();
        float availLabelWidth = cw * 11;  // Match "Current:" alignment

        ctx.parent->addString("Available:", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);

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
        ctx.parent->addString(versionPart, ctx.labelX + availLabelWidth, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), versionColor, ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

        // Show test directory warning in debug mode
        if (debugMode) {
            ctx.parent->addString("Will extract to mxbmrp3_update_test/", ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getWarning(), ctx.fontSize);
            ctx.currentY += ctx.lineHeightNormal;
        }
    }

    if (isDownloading) {
        // Show step-by-step progress
        ctx.addSpacing(1.0f);
        ctx.addSectionHeader("Progress");
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

            // Render step label
            unsigned long labelColor = (step.status == UpdateDownloader::StepStatus::IN_PROGRESS)
                ? colorConfig.getAccent()
                : colorConfig.getSecondary();
            ctx.parent->addString(step.label, ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), labelColor, ctx.fontSize);

            // Render status indicator (after label)
            if (indicator[0] != '\0') {
                ctx.parent->addString(indicator, ctx.labelX + cw * 10, ctx.currentY, Justify::LEFT,
                    Fonts::getNormal(), indicatorColor, ctx.fontSize);
            }

            ctx.currentY += ctx.lineHeightNormal;
            stepIndex++;
        }

    } else if (isReady) {
        // Show completed steps
        ctx.addSpacing(1.0f);
        ctx.addSectionHeader("Progress");
        auto steps = downloader.getSteps();
        for (const auto& step : steps) {
            const char* indicator = "OK";
            unsigned long indicatorColor = colorConfig.getPositive();

            if (step.status == UpdateDownloader::StepStatus::SKIPPED) {
                indicator = "skip";
                indicatorColor = colorConfig.getMuted();
            }

            ctx.parent->addString(step.label, ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
            ctx.parent->addString(indicator, ctx.labelX + cw * 10, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), indicatorColor, ctx.fontSize);
            ctx.currentY += ctx.lineHeightNormal;
        }

        ctx.addSpacing(0.5f);

        // Update installed message
        ctx.parent->addString("Update installed!", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getPositive(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

        ctx.parent->addString("Restart MX Bikes to apply.", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

    } else if (downloaderState == UpdateDownloader::State::FAILED) {
        // Download failed
        std::string errorText = "Error: " + downloader.getErrorMessage();
        ctx.parent->addString(errorText.c_str(), ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getNegative(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

        // Retry button - centered
        {
            float buttonWidth = cw * 7;  // Fits "[Retry]"
            float buttonHeight = ctx.lineHeightNormal;

            // Center the button like Copy/Reset in General tab
            float buttonCenterX = ctx.contentAreaStartX + (ctx.panelWidth - ctx.paddingH - ctx.paddingH) / 2.0f;
            float buttonX = buttonCenterX - buttonWidth / 2.0f;

            size_t regionIndex = ctx.parent->m_clickRegions.size();
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                buttonX, ctx.currentY, buttonWidth, buttonHeight,
                SettingsHud::ClickRegion::UPDATE_SKIP_VERSION, nullptr
            ));

            bool isHovered = ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex);

            // Background: purple on purple
            SPluginQuad_t bgQuad;
            float bgX = buttonX, bgY = ctx.currentY;
            ctx.parent->applyOffset(bgX, bgY);
            ctx.parent->setQuadPositions(bgQuad, bgX, bgY, buttonWidth, buttonHeight);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = isHovered ? colorConfig.getAccent()
                : PluginUtils::applyOpacity(colorConfig.getAccent(), 0.5f);
            ctx.parent->m_quads.push_back(bgQuad);

            // Text: accent on accent when not hovered
            unsigned long textColor = isHovered ? colorConfig.getPrimary() : colorConfig.getAccent();
            ctx.parent->addString("[Retry]", buttonCenterX, ctx.currentY, Justify::CENTER,
                Fonts::getNormal(), textColor, ctx.fontSize);
            ctx.currentY += ctx.lineHeightNormal;
        }

    } else if (isUpdateAvailable) {
        // Update available - show release notes and install button
        // (Available version row already shown above)

        ctx.addSpacing(0.5f);

        // Release notes (if available)
        std::string releaseNotes = checker.getReleaseNotes();
        if (!releaseNotes.empty()) {
            ctx.addSectionHeader("Release Notes");

            // Simple line-by-line display (first few lines)
            std::istringstream stream(releaseNotes);
            std::string line;
            int lineCount = 0;
            constexpr int MAX_LINES = 9;
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
                    line = line.substr(0, 42) + "...";
                }

                ctx.parent->addString(line.c_str(), ctx.labelX, ctx.currentY, Justify::LEFT,
                    Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
                ctx.currentY += ctx.lineHeightNormal;
                lineCount++;
            }

            // Show truncation note if there are more lines
            if (hasMoreLines) {
                ctx.addSpacing(1.0f);
                ctx.parent->addString("See GitHub for full release notes.", ctx.labelX, ctx.currentY, Justify::LEFT,
                    Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
                ctx.currentY += ctx.lineHeightNormal;
            }

            ctx.addSpacing(0.5f);
        }

        // Install button - centered
        {
            float buttonWidth = cw * 16;  // Fits "[Install Update]"
            float buttonHeight = ctx.lineHeightNormal;

            // Center the button like Copy/Reset in General tab
            float buttonCenterX = ctx.contentAreaStartX + (ctx.panelWidth - ctx.paddingH - ctx.paddingH) / 2.0f;
            float buttonX = buttonCenterX - buttonWidth / 2.0f;

            size_t regionIndex = ctx.parent->m_clickRegions.size();
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                buttonX, ctx.currentY, buttonWidth, buttonHeight,
                SettingsHud::ClickRegion::UPDATE_INSTALL, nullptr
            ));

            bool isHovered = ctx.parent->m_hoveredRegionIndex == static_cast<int>(regionIndex);

            // Background: purple on purple
            SPluginQuad_t bgQuad;
            float bgX = buttonX, bgY = ctx.currentY;
            ctx.parent->applyOffset(bgX, bgY);
            ctx.parent->setQuadPositions(bgQuad, bgX, bgY, buttonWidth, buttonHeight);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = isHovered ? colorConfig.getAccent()
                : PluginUtils::applyOpacity(colorConfig.getAccent(), 0.5f);
            ctx.parent->m_quads.push_back(bgQuad);

            // Text: accent on accent when not hovered
            unsigned long textColor = isHovered ? colorConfig.getPrimary() : colorConfig.getAccent();
            ctx.parent->addString("[Install Update]", buttonCenterX, ctx.currentY, Justify::CENTER,
                Fonts::getNormal(), textColor, ctx.fontSize);
            ctx.currentY += ctx.lineHeightNormal;
        }

    } else if (checkerStatus == UpdateChecker::Status::CHECKING) {
        ctx.parent->addString("Checking for updates...", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

    } else if (checkerStatus == UpdateChecker::Status::UP_TO_DATE) {
        ctx.parent->addString("You have the latest version.", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getSecondary(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

    } else if (checkerStatus == UpdateChecker::Status::CHECK_FAILED) {
        ctx.parent->addString("Could not check for updates.", ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
        ctx.currentY += ctx.lineHeightNormal;

    } else {
        // IDLE - not checked yet
        ctx.addSpacing(0.5f);
        if (!checker.isEnabled()) {
            ctx.parent->addString("Enable auto-check or click Check Now.", ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
        } else {
            ctx.parent->addString("Update check pending...", ctx.labelX, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colorConfig.getMuted(), ctx.fontSize);
        }
        ctx.currentY += ctx.lineHeightNormal;
    }

    return nullptr;  // No specific HUD for this tab
}
