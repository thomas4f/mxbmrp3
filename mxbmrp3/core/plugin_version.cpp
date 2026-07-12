// ============================================================================
// core/plugin_version.cpp
// The ONE translation unit that pulls in resource.h (and the per-build
// version_build.g.h it includes). It defines PluginConstants::PLUGIN_VERSION from
// VER_STRING, so the runtime version string still carries the auto-stamped build
// number — but bumping that number (every commit) recompiles only THIS file, not
// every TU that includes plugin_constants.h. Keeping resource.h out of that widely
// included header is what stops a one-line change from triggering a full rebuild.
// ============================================================================
#include "plugin_constants.h"
#include "../resource.h"   // VER_STRING (single source of truth for the version)

namespace PluginConstants {
    const char* const PLUGIN_VERSION = VER_STRING;
}
