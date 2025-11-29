// ============================================================================
// core/handler_singleton.h
// Macro to eliminate getInstance() duplication across handler classes
// ============================================================================
#pragma once

#include "../diagnostics/logger.h"

// Macro to define singleton getInstance() for handler classes
// Usage: Place DEFINE_HANDLER_SINGLETON(HandlerClassName) in the .cpp file
#define DEFINE_HANDLER_SINGLETON(ClassName) \
    ClassName& ClassName::getInstance() { \
        static ClassName instance; \
        static bool initialized = false; \
        if (!initialized) { \
            DEBUG_INFO(#ClassName " initialized"); \
            initialized = true; \
        } \
        return instance; \
    }

// Macro for defensive null checks in handler methods
// Usage: HANDLER_NULL_CHECK(psData) at start of handler function
// Automatically logs error and returns if pointer is null
#define HANDLER_NULL_CHECK(ptr) \
    if (!(ptr)) { \
        DEBUG_ERROR("Null pointer in handler: " #ptr); \
        return; \
    }

// Variant for handlers that return a value
#define HANDLER_NULL_CHECK_RET(ptr, retval) \
    if (!(ptr)) { \
        DEBUG_ERROR("Null pointer in handler: " #ptr); \
        return (retval); \
    }
