// ============================================================================
// core/plausible_g.h
// The one limit on a believable chassis g reading, shared by everything that
// takes a PEAK of it: the lifetime hardest-hit stat (ExplorationStats::onGForce)
// and the impact vignette's per-frame peak (PluginData::updateAcceleration).
//
// A landing reads a few g; anything past MAX is the physics engine losing its
// footing (a spawn, a wall, a reset). A peak is exactly the number one such
// sample ruins: a lifetime stat permanently, the vignette as a black flash on
// every respawn. Pinned by tests/unit/test_impact_vignette.cpp.
// ============================================================================
#pragma once

#include <cmath>

namespace PlausibleG {

constexpr float MAX = 50.0f;

inline bool ok(float g) { return std::isfinite(g) && g > 0.0f && g <= MAX; }

}  // namespace PlausibleG
