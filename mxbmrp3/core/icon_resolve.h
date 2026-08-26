// ============================================================================
// core/icon_resolve.h
// ICON SPRITE RESOLUTION, both directions. Pure arithmetic plus two map lookups:
// no AssetManager, no Win32, no disk.
//
// THE MODEL. The base icon set is `count` icons occupying the contiguous sprite
// block [first, first + count), and a 1-based SHAPE INDEX is a position in it --
// the unit settings persist (as a name) and the shape pickers step through. A theme
// may override the ART for a name, and its override sprite is registered PAST that
// block, alongside its slices.
//
// WHY IT IS A HEADER OF ITS OWN. Both directions have to stay each other's inverse
// once overrides exist, and the backward one had been open-coded as
// `sprite - first + 1` at ten call sites. That is correct exactly while every icon
// sprite is in the base block; an override sprite makes it return a number off the
// end of the vocabulary, so markers stop rotating (shouldRotate() reads the filename
// at that index) and a saved marker resolves to nothing. One place to get right,
// and -- since AssetManager itself only builds on Windows -- one place a unit test
// can reach without staging a single asset.
//
// Pinned by tests/unit/test_icon_resolve.cpp.
// ============================================================================
#pragma once

#include <map>
#include <string>

namespace IconResolve {

// name -> sprite: the override if the active theme has one, else the base sprite the
// caller already resolved. `overrides` is null when no theme is active.
inline int spriteForName(const std::string& name, int baseSprite,
                         const std::map<std::string, int>* overrides) {
    if (overrides && !overrides->empty()) {
        auto it = overrides->find(name);
        if (it != overrides->end()) return it->second;
    }
    return baseSprite;
}

// shape index -> sprite to DRAW. `nameAtShape` is the base icon's filename at that
// position, which is what an override is keyed by; empty means the shape is out of
// range and the caller has nothing to draw.
inline int spriteForShape(int shape, int first, int count, const std::string& nameAtShape,
                          const std::map<std::string, int>* overrides) {
    if (shape <= 0 || shape > count || nameAtShape.empty()) return 0;
    return spriteForName(nameAtShape, first + shape - 1, overrides);
}

// sprite -> shape index, the inverse of the above for BOTH kinds of sprite. 0 for a
// sprite that is not an icon at all -- a theme's slice sprites live past the icon
// block too, and they are not markers.
inline int shapeForSprite(int sprite, int first, int count,
                          const std::map<int, int>* overrideShape) {
    const int arrayIndex = sprite - first;
    if (arrayIndex >= 0 && arrayIndex < count) return arrayIndex + 1;
    if (overrideShape && !overrideShape->empty()) {
        auto it = overrideShape->find(sprite);
        if (it != overrideShape->end()) return it->second;
    }
    return 0;
}

}  // namespace IconResolve
