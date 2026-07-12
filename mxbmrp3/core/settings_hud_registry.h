// ============================================================================
// core/settings_hud_registry.h
// One ordered table of per-HUD serializers driving capture, apply, and serialize.
// See settings_hud_registry.cpp for the rationale (single source of truth for the
// per-HUD section list, replacing three parallel hardcoded lists).
// ============================================================================
#pragma once

#include "settings_manager.h"
#include <vector>

class HudManager;

namespace Settings {

struct HudSectionSerializer {
    const char* name;
    void (*capture)(const HudManager&, SettingsManager::ProfileCache&, const char* name);
    void (*apply)(HudManager&, const SettingsManager::ProfileCache&, const char* name);
};

// Ordered registry (serialize order). Iterated by captureToCache, applyProfile,
// and serializeSettings.
const std::vector<HudSectionSerializer>& hudSectionRegistry();

} // namespace Settings
