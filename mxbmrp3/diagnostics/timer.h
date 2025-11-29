// ============================================================================
// diagnostics/timer.h
// Performance timing macros for profiling code execution with threshold-based logging
// ============================================================================
#pragma once

#include "logger.h"
#include <chrono>

#ifdef _DEBUG

// Scoped timer that logs duration when it goes out of scope
class ScopedTimer {
public:
    ScopedTimer(const char* name, int thresholdMicros = 0)
        : m_name(name)
        , m_thresholdMicros(thresholdMicros)
        , m_start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();

        // Only log if duration exceeds threshold (0 = always log)
        if (m_thresholdMicros == 0 || duration >= m_thresholdMicros) {
            DEBUG_INFO_F("[TIMER] %s: %lld microseconds (%.3f ms)",
                m_name, duration, duration / 1000.0);
        }
    }

private:
    const char* m_name;
    int m_thresholdMicros;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

// Macro for easy scoped timing - logs when scope exits
// Usage: SCOPED_TIMER("MyFunction");
#define SCOPED_TIMER(name) ScopedTimer _timer_##__LINE__(name)

// Macro for conditional timing - only logs if duration exceeds threshold
// Usage: SCOPED_TIMER_THRESHOLD("MyFunction", 1000); // Only log if > 1ms
#define SCOPED_TIMER_THRESHOLD(name, micros) ScopedTimer _timer_##__LINE__(name, micros)

#else

// No-op in release builds
#define SCOPED_TIMER(name) ((void)0)
#define SCOPED_TIMER_THRESHOLD(name, micros) ((void)0)

#endif
