// ============================================================================
// handlers/draw_handler.h
// Coordinates all rendering operations and manages frame timing
// ============================================================================
#pragma once

#include "../core/plugin_constants.h"
#include <array>

class DrawHandler {
public:
    static DrawHandler& getInstance();

    // Main draw handler - processes frame metrics and delegates rendering to HudManager
    void handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString);

    // Performance tracking utilities (called by ACCUMULATE_CALLBACK_TIME macro)
    static long long getCurrentTimeUs();
    static void accumulateCallbackTime(long long timeUs);

private:
    DrawHandler();
    ~DrawHandler() = default;
    DrawHandler(const DrawHandler&) = delete;
    DrawHandler& operator=(const DrawHandler&) = delete;

    void updateFrameMetrics(long long totalFrameTimeUs);

    // FPS calculation constants
    static constexpr float MIN_FPS_CLAMP = 0.1f;     // Minimum FPS value (avoid division by zero)
    static constexpr float MAX_FPS_CLAMP = 1000.0f;  // Maximum FPS value (avoid floating point errors)
    static constexpr float DEFAULT_FRAME_BUDGET_MS = 16.67f;  // Default frame budget for 60fps (1000ms / 60fps)

    // Frame timing for FPS calculation
    static constexpr int FRAME_HISTORY_SIZE = 60;  // Number of frames to track for FPS calculation
    std::array<long long, FRAME_HISTORY_SIZE> m_frameTimestamps;
    int m_frameIndex;
    int m_validFrameCount;  // Number of valid (non-zero) frames in buffer

    // Accumulate actual plugin execution time for current frame
    long long m_accumulatedFrameTimeUs;  // Total time spent in plugin callbacks this frame

    // Cached performance counter frequency (initialized once at startup)
    long long m_performanceFrequency;
};
