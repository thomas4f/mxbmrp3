// ============================================================================
// recorder_plugin/performance_timer.h
// High-resolution performance timing utilities for standalone recorder
// ============================================================================
#pragma once

#include <windows.h>
#include <cstdint>

namespace PerformanceTimer {

// Initializes and returns the performance counter frequency
// Returns 1MHz (1000000) as fallback if QueryPerformanceFrequency fails
inline long long initializeFrequency() {
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
inline uint64_t counterToMicroseconds(long long frequency, long long counter) {
    if (frequency == 0) {
        return 0;
    }
    // Split calculation to avoid overflow: (counter / freq) * 1M + (counter % freq) * 1M / freq
    return (counter / frequency) * 1000000LL +
           ((counter % frequency) * 1000000LL) / frequency;
}

// Gets current time in microseconds
// frequency: Result from initializeFrequency()
inline uint64_t getCurrentTimeMicroseconds(long long frequency) {
    LARGE_INTEGER currentTime;
    if (!QueryPerformanceCounter(&currentTime)) {
        return 0;
    }
    return counterToMicroseconds(frequency, currentTime.QuadPart);
}

} // namespace PerformanceTimer
