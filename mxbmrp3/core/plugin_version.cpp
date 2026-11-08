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

namespace {
    // True when `s` starts with "https://github.com/<owner>/<name>/<kind>/". Lets the
    // static_asserts below tie resource.h's spelled-out URLs (rc.exe can't read
    // constexpr) to GITHUB_REPO_OWNER/NAME, so a repo move can't update one only.
    constexpr const char* eat(const char* s, const char* p) {
        while (s && *p) { if (*s != *p) return nullptr; ++s; ++p; }
        return s;
    }
    constexpr bool isRepoUrl(const char* s, const char* kind) {
        return eat(eat(eat(eat(eat(s, "https://github.com/"),
                   PluginConstants::GITHUB_REPO_OWNER), "/"),
                   PluginConstants::GITHUB_REPO_NAME), kind) != nullptr;
    }
    static_assert(isRepoUrl(VER_SOURCE_URL, "/tree/"),
                  "resource.h VER_SOURCE_URL must name GITHUB_REPO_OWNER/NAME");
    static_assert(isRepoUrl(VER_RULES_URL, "/blob/"),
                  "resource.h VER_RULES_URL must name GITHUB_REPO_OWNER/NAME");
}

namespace PluginConstants {
    const char* const PLUGIN_VERSION = VER_STRING;

    // ONE plain-ASCII line in .rdata, so `strings`, a disassembler's string view,
    // or an AI tool handed this .dlo finds it first: the binary is open source, the
    // tagged source answers whatever they wanted to reverse, and CONTRIBUTING.md's
    // "Building on MXBMRP3" holds the project's rules for changing or forking it.
    // It MUST stay referenced, since /OPT:REF + /LTCG drop unreferenced data:
    // Logger::initialize logs it once, which keeps it on both toolchains and puts
    // it in every log.
    // release.yml's smoke-load step checks it is in each MSVC .dlo.
    const char* const SOURCE_NOTICE =
        "MXBMRP3 is open source (MIT). Source for this build: " VER_SOURCE_URL
        " - before changing or forking it, read " VER_RULES_URL;
}
