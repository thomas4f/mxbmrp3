// ============================================================================
// handlers/draw_handler.cpp
// Coordinates all rendering operations and manages frame timing
// ============================================================================
#include "draw_handler.h"
#include "../core/hud_manager.h"
#include "../core/plugin_data.h"
#include "../core/stats_manager.h"
#include "../core/xinput_reader.h"
#include "../game/game_config.h"
#if GAME_HAS_HTTP_SERVER
#include "../core/http_server.h"
#endif
#include "../diagnostics/logger.h"
#include <windows.h>
#include <cstdint>

namespace {
    // Windows performance counter utilities for FPS tracking
    // These are DrawHandler-specific and not used elsewhere in the codebase

    // Initializes and returns the performance counter frequency
    // Returns 1MHz (1000000) as fallback if QueryPerformanceFrequency fails
    long long initializeFrequency() {
        LARGE_INTEGER frequency;
        if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart != 0) {
            return frequency.QuadPart;
        }
        // Fallback to 1MHz if performance counter is unavailable
        return 1000000LL;
    }

    // Converts performance counter value to microseconds
    // frequency: Result from initializeFrequency()
    // counter: Current performance counter value
    uint64_t counterToMicroseconds(long long frequency, long long counter) {
        if (frequency == 0) {
            return 0;
        }
        // Split calculation to avoid overflow: (counter / freq) * 1M + (counter % freq) * 1M / freq
        return (counter / frequency) * 1000000LL +
               ((counter % frequency) * 1000000LL) / frequency;
    }

    // Gets current time in microseconds
    // frequency: Result from initializeFrequency()
    uint64_t getCurrentTimeMicroseconds(long long frequency) {
        static uint64_t lastKnownGoodTime = 0;
        LARGE_INTEGER currentTime;
        if (!QueryPerformanceCounter(&currentTime)) {
            DEBUG_WARN("QueryPerformanceCounter failed, using cached value");
            return lastKnownGoodTime;
        }
        lastKnownGoodTime = counterToMicroseconds(frequency, currentTime.QuadPart);
        return lastKnownGoodTime;
    }
}

DrawHandler::DrawHandler()
    : m_frameIndex(0)
    , m_validFrameCount(0)
    , m_accumulatedFrameTimeUs(0)
    , m_performanceFrequency(0) {

    // Initialize frame timestamps to 0
    m_frameTimestamps.fill(0LL);

    // Initialize performance counter frequency
    m_performanceFrequency = initializeFrequency();
    if (m_performanceFrequency == 1000000LL) {
        DEBUG_WARN("QueryPerformanceFrequency failed, using 1MHz fallback");
    }
}

DrawHandler& DrawHandler::getInstance() {
    static DrawHandler instance;
    return instance;
}

long long DrawHandler::getCurrentTimeUs() {
    DrawHandler& instance = getInstance();
    return getCurrentTimeMicroseconds(instance.m_performanceFrequency);
}

void DrawHandler::accumulateCallbackTime(long long timeUs) {
    // Add this callback's execution time to the frame accumulator
    getInstance().m_accumulatedFrameTimeUs += timeUs;
}

long long DrawHandler::consumeAccumulatedCallbackTime() {
    long long v = getInstance().m_accumulatedFrameTimeUs;
    getInstance().m_accumulatedFrameTimeUs = 0;
    return v;
}

void DrawHandler::updateFrameMetrics(long long totalFrameTimeUs) {
    // Get current time for FPS calculation
    long long currentTimeUs = getCurrentTimeUs();

    // Track if we're filling a new slot or overwriting an old one
    if (m_frameTimestamps[m_frameIndex] == 0 && m_validFrameCount < FRAME_HISTORY_SIZE) {
        m_validFrameCount++;  // Adding first entry to this slot
    }

    // Store frame timestamp
    m_frameTimestamps[m_frameIndex] = currentTimeUs;

    // The exploration signals' once-a-second tick: the time sums, the frame
    // rate flag, the overlay's connection total. One compare per frame here.
    ++m_framesThisSecond;
    if (currentTimeUs - m_lastExplorationTickUs >= 1000000LL) {
        if (m_lastExplorationTickUs != 0) {
            const PluginData& pd = PluginData::getInstance();
            uint32_t overlayTotal = 0;
#if GAME_HAS_HTTP_SERVER
            overlayTotal = HttpServer::getInstance().sseConnectsTotal();
#endif
            StatsManager& sm = StatsManager::getInstance();
            // consumeMoved() CLEARS the flag, so it must be read exactly once
            // per tick and nowhere else - a second reader would silently starve
            // this one and Iron Butt would stop counting.
            const bool moved = sm.consumeMoved();
            sm.exploration().tick(
                pd.getSpectatedRaceNum() >= 0, XInputReader::getInstance().rumbleLive(),
                pd.isPlayerRunning(), moved, m_framesThisSecond, overlayTotal);
        }
        m_lastExplorationTickUs = currentTimeUs;
        m_framesThisSecond = 0;
    }

    // Increment frame index (circular buffer)
    m_frameIndex = (m_frameIndex + 1) % FRAME_HISTORY_SIZE;

    // Calculate FPS from frame history
    // After incrementing, m_frameIndex now points to the oldest entry (next to be overwritten)
    float fps = 0.0f;

    // Need at least 10 frames for reliable FPS calculation
    if (m_validFrameCount >= 10) {
        // Use circular buffer index to get oldest entry (O(1) instead of O(n) scan)
        // After incrementing m_frameIndex, it points to the next slot to overwrite,
        // which contains the oldest timestamp when buffer is full
        long long oldestTime = m_frameTimestamps[m_frameIndex];

        // If buffer isn't full yet, find the actual oldest non-zero entry
        if (m_validFrameCount < FRAME_HISTORY_SIZE) {
            for (int i = 0; i < FRAME_HISTORY_SIZE; ++i) {
                if (m_frameTimestamps[i] > 0 &&
                    (oldestTime == 0 || m_frameTimestamps[i] < oldestTime)) {
                    oldestTime = m_frameTimestamps[i];
                }
            }
        }

        // Safety: Ensure we have a valid oldest timestamp
        if (oldestTime > 0 && currentTimeUs > oldestTime) {
            long long timeSpanUs = currentTimeUs - oldestTime;
            if (timeSpanUs > 0) {
                // FPS = (number of valid frames - 1) / time span in seconds
                fps = ((m_validFrameCount - 1) * 1000000.0f) / timeSpanUs;

                // Safety: Clamp to reasonable range (avoid floating point errors)
                if (fps < MIN_FPS_CLAMP) fps = MIN_FPS_CLAMP;
                if (fps > MAX_FPS_CLAMP) fps = MAX_FPS_CLAMP;
            }
        }
    }

    // Calculate plugin time as percentage of frame budget
    float pluginTimeMs = totalFrameTimeUs / 1000.0f;
    float frameBudgetMs = (fps > 0) ? (1000.0f / fps) : DEFAULT_FRAME_BUDGET_MS;
    float pluginPercent = (frameBudgetMs > 0) ? (pluginTimeMs / frameBudgetMs) * 100.0f : 0.0f;

    // Update every frame for real-time values
    if (fps > 0) {
        PluginData::getInstance().updateDebugMetrics(fps, pluginTimeMs, pluginPercent);
    }
}

void DrawHandler::handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {
    // Safety: Check for null pointers from API
    if (piNumQuads == nullptr || ppQuad == nullptr ||
        piNumString == nullptr || ppString == nullptr) {
        DEBUG_WARN("handleDraw called with NULL pointer(s)");
        return;
    }

    // Note: draw state is set in HudManager::produceFrame() (shared by sync + threaded
    // render paths), not here.

    // Delegate to HUD manager
    HudManager::getInstance().draw(iState, piNumQuads, ppQuad, piNumString, ppString);

    // Total plugin time is the accumulated time from all callbacks this frame
    long long totalFrameTimeUs = m_accumulatedFrameTimeUs;

    // Update performance metrics for the performance HUD
    updateFrameMetrics(totalFrameTimeUs);

    // Record render data counts for benchmark widget
    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    if (bm.active) {
        bm.totalQuads = *piNumQuads;
        bm.totalStrings = *piNumString;
    }

    // Reset accumulator for next frame (must be AFTER updateFrameMetrics!)
    m_accumulatedFrameTimeUs = 0;
}
