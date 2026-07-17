// ============================================================================
// core/fmx_manager_internal.h
// Shared internal macro for the FmxManager translation units (fmx_manager*.cpp).
// FMX_LOG was file-local in fmx_manager.cpp before that file was split into
// focused TUs; it is unchanged. It expands only inside FmxManager member
// functions — it reads the m_bLoggingEnabled member — so every FmxManager TU
// includes this to keep the (opt-in) trick-detection logging identical.
// ============================================================================
#pragma once

#include "../diagnostics/logger.h"

#define FMX_LOG(...) do { if (m_bLoggingEnabled) DEBUG_INFO_F(__VA_ARGS__); } while(0)
