// ============================================================================
// core/asset_path.h
// Reduce a registered resource path to the name the software renderer expects.
//
// Header-only and dependency-free so the unit suite can compile it directly.
// It lived as a file-local helper in companion_window.cpp until it turned out to
// be the seam where every nested asset folder breaks -- see the regression test
// in tests/unit/asset_path_test.cpp for the bug that moved it here.
// ============================================================================
#pragma once

#include <string>

namespace AssetPath {

// The renderer resolves a sprite/font two different ways:
//
//   FLAT folders (fonts/ textures/ icons/) are addressed by BASE NAME -- the
//   renderer picks the folder itself from the icon/texture split, so
//   "...\fonts\RobotoMono-Regular.fnt" must reduce to "RobotoMono-Regular".
//
//   NESTED assets (themes/<name>/corner.tga) cannot be expressed by that split at
//   all, so they keep a RELATIVE PATH ("themes/rounded/corner") which the renderer
//   resolves against the asset root.
//
// Returning the bare base name in the nested case is what broke panel themes
// completely: every theme's sprites collapsed to "corner"/"edge"/"center", failed
// to load, and drawQuad silently drew nothing -- a whole feature invisible with no
// error anywhere. The rule below is structural (how DEEP the path is), not a check
// for "themes" by name, so a future nested folder needs no change here.
inline std::string renderName(const std::string& path) {
    std::string p = path;
    for (char& c : p) if (c == '\\') c = '/';

    // Strip the extension, but only if it belongs to the final component
    // ("a.b/c" has no extension).
    const size_t lastSlash = p.find_last_of('/');
    const size_t dot = p.find_last_of('.');
    if (dot != std::string::npos && (lastSlash == std::string::npos || dot > lastSlash)) {
        p.resize(dot);
    }

    // Drop the resource-root prefix; the renderer's root already points at it.
    const std::string root = "mxbmrp3_data/";
    const size_t rp = p.find(root);
    if (rp != std::string::npos) p = p.substr(rp + root.size());

    // Exactly "<flatdir>/<file>" -> "<file>". Anything deeper keeps its path.
    const size_t first = p.find('/');
    if (first != std::string::npos && p.find('/', first + 1) == std::string::npos) {
        return p.substr(first + 1);
    }
    return p;
}

}  // namespace AssetPath
