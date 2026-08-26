// ============================================================================
// core/layout_config.cpp
// See layout_config.h. All that is left here is the file loop; the line rules are
// layout_ini.h's and the key handling is AssetManager::readThemeIni's.
// ============================================================================
#include "layout_config.h"

#include <fstream>
#include <string>

bool layoutForEachIniPairRaw(const std::string& path, LayoutIniRawPairFn apply, void* ctx) {
    std::ifstream f(path);
    if (!f) return false;

    // The key handed to `apply` is "section.property", so property names can be short
    // and repeat across sections (`size` means one thing under [frame] and another
    // under [card]) -- which is what makes a theme file scan like CSS instead of like
    // a flat list of self-scoping prefixes.
    std::string line, section, key, raw;
    float value = 0.0f;
    bool numeric = false;
    while (std::getline(f, line)) {
        if (layoutParseIniLineRaw(line, section, key, value, raw, numeric) == LayoutIniLine::Pair) {
            apply(key.c_str(), value, raw.c_str(), numeric, ctx);
        }
    }
    return true;
}
