// ============================================================================
// hud/benchmark_widget.cpp
// Developer-only widget showing per-callback and per-HUD timing breakdown
// ============================================================================
#include "benchmark_widget.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <ctime>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include "../core/settings_manager.h"
#include "../handlers/draw_handler.h"

using namespace PluginConstants;

BenchmarkWidget::BenchmarkWidget() {
    DEBUG_INFO("BenchmarkWidget created");
    setDraggable(true);

    // Pre-allocate for typical display (header + ~16 callbacks + separator + ~10 HUDs + footer)
    m_quads.reserve(4);
    m_strings.reserve(60);

    m_callbackSnapshots.fill({});
    m_hudSnapshots.fill({});

    resetToDefaults();
    rebuildRenderData();
}

bool BenchmarkWidget::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::DebugMetrics);
}

void BenchmarkWidget::setVisible(bool visible) {
    bool wasVisible = isVisible();
    BaseHud::setVisible(visible);

    // Toggle benchmark metrics collection
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    bm.active = visible;

    if (!visible && wasVisible) {
        // Export report when hiding the widget
        const std::string& savePath = SettingsManager::getInstance().getSavePath();
        if (!savePath.empty()) {
            exportReport(savePath.c_str());
        }
    }

    if (visible && !wasVisible) {
        // Reset snapshots when becoming visible
        m_snapshotCount = 0;
        m_hudSnapshotCount = 0;
        m_frameCounter = 0;
        m_callbackSnapshots.fill({});
        m_hudSnapshots.fill({});

        // Reset peak values in benchmark metrics
        for (int i = 0; i < bm.callbackCount; ++i) {
            bm.callbacks[i].peakTimeUs = 0;
        }
        for (int i = 0; i < bm.hudCount; ++i) {
            bm.huds[i].rebuildCount = 0;
        }
    }
}

void BenchmarkWidget::update() {
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Take snapshot at interval to keep display readable
    m_frameCounter++;
    if (m_frameCounter >= SNAPSHOT_INTERVAL_FRAMES) {
        takeSnapshot();
        m_frameCounter = 0;
    }

    // Always rebuild (data changes every frame when visible)
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void BenchmarkWidget::takeSnapshot() {
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();

    // Snapshot callback timing (accumulated across entire interval)
    m_snapshotCount = bm.callbackCount;
    for (int i = 0; i < m_snapshotCount && i < MAX_CALLBACKS; ++i) {
        strncpy_s(m_callbackSnapshots[i].name, sizeof(m_callbackSnapshots[i].name),
                  bm.callbacks[i].name, _TRUNCATE);
        m_callbackSnapshots[i].totalTimeUs = static_cast<float>(bm.callbacks[i].totalTimeUs);
        m_callbackSnapshots[i].peakTimeUs = static_cast<float>(bm.callbacks[i].peakTimeUs);
        m_callbackSnapshots[i].callCount = bm.callbacks[i].callCount;
    }

    // Snapshot HUD rebuild timing
    m_hudSnapshotCount = bm.hudCount;
    for (int i = 0; i < m_hudSnapshotCount && i < MAX_HUD_SNAPSHOTS; ++i) {
        strncpy_s(m_hudSnapshots[i].name, sizeof(m_hudSnapshots[i].name),
                  bm.huds[i].name, _TRUNCATE);
        m_hudSnapshots[i].lastRebuildTimeUs = static_cast<float>(bm.huds[i].lastRebuildTimeUs);
        m_hudSnapshots[i].rebuildsInInterval = bm.huds[i].rebuildCount;
    }

    // Snapshot aggregate metrics
    m_totalCallbackTimeUs = 0;
    for (int i = 0; i < m_snapshotCount; ++i) {
        m_totalCallbackTimeUs += m_callbackSnapshots[i].totalTimeUs;
    }
    m_collectRenderTimeUs = static_cast<float>(bm.collectRenderTimeUs);
    m_totalQuadCount = bm.totalQuads;
    m_totalStringCount = bm.totalStrings;

    // Reset all counters for next interval
    for (int i = 0; i < bm.callbackCount; ++i) {
        bm.callbacks[i].totalTimeUs = 0;
        bm.callbacks[i].peakTimeUs = 0;
        bm.callbacks[i].callCount = 0;
    }
    for (int i = 0; i < bm.hudCount; ++i) {
        bm.huds[i].rebuildCount = 0;
    }
}

void BenchmarkWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Calculate layout
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize)
        + dim.paddingH + dim.paddingH;

    // Count rows: title + header + callbacks + separator + HUD header + HUDs with rebuilds + footer
    int rowCount = 1;  // Title
    rowCount += 1;     // Callback section header
    int activeCallbacks = 0;
    for (int i = 0; i < m_snapshotCount; ++i) {
        if (m_callbackSnapshots[i].callCount > 0) {
            activeCallbacks++;
        }
    }
    rowCount += (activeCallbacks > 0) ? activeCallbacks : 1;  // At least "(no data)" row
    rowCount += 1;     // Blank separator
    rowCount += 1;     // HUD rebuilds header
    int activeHuds = 0;
    for (int i = 0; i < m_hudSnapshotCount; ++i) {
        if (m_hudSnapshots[i].rebuildsInInterval > 0 || m_hudSnapshots[i].lastRebuildTimeUs > 0) {
            activeHuds++;
        }
    }
    rowCount += (activeHuds > 0) ? activeHuds : 1;  // At least "(none)" row
    rowCount += 1;     // Blank separator
    rowCount += 3;     // Footer (collect time, quads, strings)

    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float backgroundHeight = dim.paddingV + titleHeight + (rowCount * dim.lineHeightNormal) + dim.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float currentY = START_Y + dim.paddingV;

    // Title
    addTitleString("Benchmark", contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    currentY += titleHeight;

    // === CALLBACK SECTION ===
    addString("CALLBACKS", contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::ACCENT), dim.fontSize);

    // Right-align column headers
    float rightEdge = contentStartX + PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize);
    addString("Total us   Peak us  Calls", rightEdge, currentY, Justify::RIGHT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    if (activeCallbacks == 0) {
        addString("(no data)", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        for (int i = 0; i < m_snapshotCount; ++i) {
            if (m_callbackSnapshots[i].callCount <= 0) continue;

            // Callback name (left-aligned)
            addString(m_callbackSnapshots[i].name, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

            // Values (right-aligned)
            char values[64];
            snprintf(values, sizeof(values), "%7.0f  %7.0f  %5d",
                     m_callbackSnapshots[i].totalTimeUs,
                     m_callbackSnapshots[i].peakTimeUs,
                     m_callbackSnapshots[i].callCount);

            // Color based on peak single-call time vs frame budget (4170us at 240fps)
            // Green < 1000us (< 25%), Yellow < 2500us (< 60%), Red > 2500us
            unsigned long color;
            if (m_callbackSnapshots[i].peakTimeUs < 1000.0f) {
                color = this->getColor(ColorSlot::POSITIVE);
            } else if (m_callbackSnapshots[i].peakTimeUs < 2500.0f) {
                color = this->getColor(ColorSlot::WARNING);
            } else {
                color = this->getColor(ColorSlot::NEGATIVE);
            }

            addString(values, rightEdge, currentY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), color, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Separator
    currentY += dim.lineHeightNormal;

    // === HUD REBUILD SECTION ===
    addString("HUD REBUILDS", contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::ACCENT), dim.fontSize);

    addString("Last us  Count", rightEdge, currentY, Justify::RIGHT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    if (activeHuds == 0) {
        addString("(none)", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        for (int i = 0; i < m_hudSnapshotCount; ++i) {
            if (m_hudSnapshots[i].rebuildsInInterval <= 0 && m_hudSnapshots[i].lastRebuildTimeUs <= 0) continue;

            // HUD name
            addString(m_hudSnapshots[i].name, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

            // Values
            char values[48];
            snprintf(values, sizeof(values), "%7.0f  %5d",
                     m_hudSnapshots[i].lastRebuildTimeUs,
                     m_hudSnapshots[i].rebuildsInInterval);

            // Color based on rebuild time
            unsigned long color;
            if (m_hudSnapshots[i].lastRebuildTimeUs < 100.0f) {
                color = this->getColor(ColorSlot::POSITIVE);
            } else if (m_hudSnapshots[i].lastRebuildTimeUs < 500.0f) {
                color = this->getColor(ColorSlot::WARNING);
            } else {
                color = this->getColor(ColorSlot::NEGATIVE);
            }

            addString(values, rightEdge, currentY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), color, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Separator
    currentY += dim.lineHeightNormal;

    // === FOOTER - Aggregate stats ===
    char footer[64];

    snprintf(footer, sizeof(footer), "Collect render: %.0f us", m_collectRenderTimeUs);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    snprintf(footer, sizeof(footer), "Quads: %d", m_totalQuadCount);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);

    snprintf(footer, sizeof(footer), "Strings: %d", m_totalStringCount);
    addString(footer, rightEdge, currentY, Justify::RIGHT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Total callback time
    snprintf(footer, sizeof(footer), "Total callback: %.0f us (%.2f ms)",
             m_totalCallbackTimeUs, m_totalCallbackTimeUs / 1000.0f);
    addString(footer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
}

bool BenchmarkWidget::exportReport(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') return false;

    // Build directory path: savePath/mxbmrp3/ (matches settings and stats convention)
    std::string dir = std::string(savePath);
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '\\';
    }
    dir += "mxbmrp3";

    // Build file path with timestamp: savePath/mxbmrp3/benchmark_YYYYMMDD_HHMMSS.txt
    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_s(&timeInfo, &now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeInfo);
    char fileTimestamp[20];
    strftime(fileTimestamp, sizeof(fileTimestamp), "%Y%m%d_%H%M%S", &timeInfo);
    std::string filePath = dir + "\\benchmark_" + fileTimestamp + ".txt";

    FILE* f = nullptr;
    fopen_s(&f, filePath.c_str(), "w");
    if (!f) {
        DEBUG_WARN_F("BenchmarkWidget: Failed to open %s for writing", filePath.c_str());
        return false;
    }

    fprintf(f, "MXBMRP3 Benchmark Report\n");
    fprintf(f, "Version: %s\n", PluginConstants::PLUGIN_VERSION);
    fprintf(f, "Date: %s\n", timestamp);
    fprintf(f, "Snapshot interval: %d frames\n", SNAPSHOT_INTERVAL_FRAMES);
    fprintf(f, "\n");

    // Callback section
    fprintf(f, "=== CALLBACKS ===\n");
    fprintf(f, "%-24s %10s %10s %8s\n", "Name", "Total us", "Peak us", "Calls");
    fprintf(f, "%-24s %10s %10s %8s\n", "------------------------", "----------", "----------", "--------");
    for (int i = 0; i < m_snapshotCount; ++i) {
        fprintf(f, "%-24s %10.0f %10.0f %8d\n",
                m_callbackSnapshots[i].name,
                m_callbackSnapshots[i].totalTimeUs,
                m_callbackSnapshots[i].peakTimeUs,
                m_callbackSnapshots[i].callCount);
    }
    fprintf(f, "\n");

    // HUD rebuilds section
    fprintf(f, "=== HUD REBUILDS ===\n");
    fprintf(f, "%-24s %10s %8s\n", "Name", "Last us", "Count");
    fprintf(f, "%-24s %10s %8s\n", "------------------------", "----------", "--------");
    for (int i = 0; i < m_hudSnapshotCount; ++i) {
        fprintf(f, "%-24s %10.0f %8d\n",
                m_hudSnapshots[i].name,
                m_hudSnapshots[i].lastRebuildTimeUs,
                m_hudSnapshots[i].rebuildsInInterval);
    }
    fprintf(f, "\n");

    // Aggregate
    fprintf(f, "=== AGGREGATE ===\n");
    fprintf(f, "Total callback time: %.0f us (%.2f ms)\n", m_totalCallbackTimeUs, m_totalCallbackTimeUs / 1000.0f);
    fprintf(f, "Collect render time: %.0f us\n", m_collectRenderTimeUs);
    fprintf(f, "Total quads: %d\n", m_totalQuadCount);
    fprintf(f, "Total strings: %d\n", m_totalStringCount);

    fclose(f);
    DEBUG_INFO_F("BenchmarkWidget: Report exported to %s", filePath.c_str());
    return true;
}

void BenchmarkWidget::resetToDefaults() {
    m_bVisible = false;    // Hidden by default
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.90f;
    m_fScale = 1.0f;
    setPosition(0.01f, 0.3f);  // Left side of screen

    m_frameCounter = 0;
    m_snapshotCount = 0;
    m_hudSnapshotCount = 0;
    m_totalCallbackTimeUs = 0.0f;
    m_collectRenderTimeUs = 0.0f;
    m_totalQuadCount = 0;
    m_totalStringCount = 0;

    m_callbackSnapshots.fill({});
    m_hudSnapshots.fill({});

    setDataDirty();
}
