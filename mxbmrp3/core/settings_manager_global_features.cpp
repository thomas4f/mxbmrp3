// ============================================================================
// core/settings_manager_global_features.cpp
// The global INI sections each owned by one subsystem: [Rumble] (XInputReader),
// [HelmetOverlay], [Spotter], [Director], [Achievements], [Recorder] (dev tool)
// and [Hotkeys]. One writer/applier pair per section, registered as one row of
// SettingsManager::globalSectionRegistry() in settings_manager_global.cpp, which
// also holds the dispatchers and the UI/general sections' pairs.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "hud_manager.h"
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "plugin_utils.h"
#include "spotter_manager.h"
#include "director_manager.h"
#include "achievement_manager.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/director_widget.h"
#include "../hud/achievement_widget.h"
#include "../diagnostics/logger.h"
#include "../game/game_config.h"
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ostream>

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

// Write Rumble section (effect configuration)
// Always save global config to INI (per-bike effects go to JSON)
void SettingsManager::writeRumbleSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const RumbleConfig& rumbleConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    out << "[Rumble]\n";
    out << "enabled=" << (rumbleConfig.enabled ? 1 : 0) << "\n";
    out << "additive_blend=" << (rumbleConfig.additiveBlend ? 1 : 0) << "\n";
    out << "rumble_when_crashed=" << (rumbleConfig.rumbleWhenCrashed ? 1 : 0) << "\n";
    out << "use_per_bike_effects=" << (rumbleConfig.usePerBikeEffects ? 1 : 0) << "\n";
    out << "send_interval_ms=" << XInputReader::getInstance().getRumbleSendIntervalMs()
        << " ; Min ms between rumble updates; raise to reduce Bluetooth traffic (4-200, default 10)\n";
    // Suspension effect (with optional front/rear split)
    out << "susp_min_input=" << rumbleConfig.suspensionEffect.minInput << "\n";
    out << "susp_max_input=" << rumbleConfig.suspensionEffect.maxInput << "\n";
    out << "susp_light_strength=" << rumbleConfig.suspensionEffect.lightStrength << "\n";
    out << "susp_heavy_strength=" << rumbleConfig.suspensionEffect.heavyStrength << "\n";
    out << "susp_split=" << (rumbleConfig.suspensionSplit ? 1 : 0) << "\n";
    out << "susp_split_init=" << (rumbleConfig.suspensionSplitInitialized ? 1 : 0) << "\n";
    out << "susp_front_min_input=" << rumbleConfig.suspensionEffectFront.minInput << "\n";
    out << "susp_front_max_input=" << rumbleConfig.suspensionEffectFront.maxInput << "\n";
    out << "susp_front_light_strength=" << rumbleConfig.suspensionEffectFront.lightStrength << "\n";
    out << "susp_front_heavy_strength=" << rumbleConfig.suspensionEffectFront.heavyStrength << "\n";
    out << "susp_rear_min_input=" << rumbleConfig.suspensionEffectRear.minInput << "\n";
    out << "susp_rear_max_input=" << rumbleConfig.suspensionEffectRear.maxInput << "\n";
    out << "susp_rear_light_strength=" << rumbleConfig.suspensionEffectRear.lightStrength << "\n";
    out << "susp_rear_heavy_strength=" << rumbleConfig.suspensionEffectRear.heavyStrength << "\n";
    // Wheelspin effect
    out << "wheel_min_input=" << rumbleConfig.wheelspinEffect.minInput << "\n";
    out << "wheel_max_input=" << rumbleConfig.wheelspinEffect.maxInput << "\n";
    out << "wheel_light_strength=" << rumbleConfig.wheelspinEffect.lightStrength << "\n";
    out << "wheel_heavy_strength=" << rumbleConfig.wheelspinEffect.heavyStrength << "\n";
    // Brake lockup effect (with optional front/rear split)
    out << "lockup_min_input=" << rumbleConfig.brakeLockupEffect.minInput << "\n";
    out << "lockup_max_input=" << rumbleConfig.brakeLockupEffect.maxInput << "\n";
    out << "lockup_light_strength=" << rumbleConfig.brakeLockupEffect.lightStrength << "\n";
    out << "lockup_heavy_strength=" << rumbleConfig.brakeLockupEffect.heavyStrength << "\n";
    out << "lockup_split=" << (rumbleConfig.brakeLockupSplit ? 1 : 0) << "\n";
    out << "lockup_split_init=" << (rumbleConfig.brakeLockupSplitInitialized ? 1 : 0) << "\n";
    out << "lockup_front_min_input=" << rumbleConfig.brakeLockupEffectFront.minInput << "\n";
    out << "lockup_front_max_input=" << rumbleConfig.brakeLockupEffectFront.maxInput << "\n";
    out << "lockup_front_light_strength=" << rumbleConfig.brakeLockupEffectFront.lightStrength << "\n";
    out << "lockup_front_heavy_strength=" << rumbleConfig.brakeLockupEffectFront.heavyStrength << "\n";
    out << "lockup_rear_min_input=" << rumbleConfig.brakeLockupEffectRear.minInput << "\n";
    out << "lockup_rear_max_input=" << rumbleConfig.brakeLockupEffectRear.maxInput << "\n";
    out << "lockup_rear_light_strength=" << rumbleConfig.brakeLockupEffectRear.lightStrength << "\n";
    out << "lockup_rear_heavy_strength=" << rumbleConfig.brakeLockupEffectRear.heavyStrength << "\n";
    // RPM effect
    out << "rpm_min_input=" << rumbleConfig.rpmEffect.minInput << "\n";
    out << "rpm_max_input=" << rumbleConfig.rpmEffect.maxInput << "\n";
    out << "rpm_light_strength=" << rumbleConfig.rpmEffect.lightStrength << "\n";
    out << "rpm_heavy_strength=" << rumbleConfig.rpmEffect.heavyStrength << "\n";
    // Slide effect
    out << "slide_min_input=" << rumbleConfig.slideEffect.minInput << "\n";
    out << "slide_max_input=" << rumbleConfig.slideEffect.maxInput << "\n";
    out << "slide_light_strength=" << rumbleConfig.slideEffect.lightStrength << "\n";
    out << "slide_heavy_strength=" << rumbleConfig.slideEffect.heavyStrength << "\n";
    // Surface effect
    out << "surface_min_input=" << rumbleConfig.surfaceEffect.minInput << "\n";
    out << "surface_max_input=" << rumbleConfig.surfaceEffect.maxInput << "\n";
    out << "surface_light_strength=" << rumbleConfig.surfaceEffect.lightStrength << "\n";
    out << "surface_heavy_strength=" << rumbleConfig.surfaceEffect.heavyStrength << "\n";
    // Steer effect
    out << "steer_min_input=" << rumbleConfig.steerEffect.minInput << "\n";
    out << "steer_max_input=" << rumbleConfig.steerEffect.maxInput << "\n";
    out << "steer_light_strength=" << rumbleConfig.steerEffect.lightStrength << "\n";
    out << "steer_heavy_strength=" << rumbleConfig.steerEffect.heavyStrength << "\n";
    // Wheelie effect
    out << "wheelie_min_input=" << rumbleConfig.wheelieEffect.minInput << "\n";
    out << "wheelie_max_input=" << rumbleConfig.wheelieEffect.maxInput << "\n";
    out << "wheelie_light_strength=" << rumbleConfig.wheelieEffect.lightStrength << "\n";
    out << "wheelie_heavy_strength=" << rumbleConfig.wheelieEffect.heavyStrength << "\n";
    // Rev limiter effect (Min/Max are percent of the bike's limiter RPM)
    out << "revlim_min_input=" << rumbleConfig.revLimiterEffect.minInput << "\n";
    out << "revlim_max_input=" << rumbleConfig.revLimiterEffect.maxInput << "\n";
    out << "revlim_light_strength=" << rumbleConfig.revLimiterEffect.lightStrength << "\n";
    out << "revlim_heavy_strength=" << rumbleConfig.revLimiterEffect.heavyStrength << "\n";
    // Pit limiter effect (binary input)
    out << "pitlim_min_input=" << rumbleConfig.pitLimiterEffect.minInput << "\n";
    out << "pitlim_max_input=" << rumbleConfig.pitLimiterEffect.maxInput << "\n";
    out << "pitlim_light_strength=" << rumbleConfig.pitLimiterEffect.lightStrength << "\n";
    out << "pitlim_heavy_strength=" << rumbleConfig.pitLimiterEffect.heavyStrength << "\n\n";
}

// Handle Rumble section (effect configuration)
// Always load into global config (per-bike profiles loaded from JSON)
void SettingsManager::applyRumbleLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
    RumbleConfig& config = XInputReader::getInstance().getGlobalRumbleConfig();
    try {
        if (key == "enabled") {
            config.enabled = std::stoi(value) != 0;
        } else if (key == "additive_blend") {
            config.additiveBlend = std::stoi(value) != 0;
        } else if (key == "rumble_when_crashed") {
            config.rumbleWhenCrashed = std::stoi(value) != 0;
        } else if (key == "use_per_bike_effects" || key == "use_per_bike_profiles") {
            // Note: use_per_bike_profiles is backward compatible alias
            config.usePerBikeEffects = std::stoi(value) != 0;
        } else if (key == "send_interval_ms") {
            // Global (never per-bike): lives on XInputReader, not RumbleConfig
            XInputReader::getInstance().setRumbleSendIntervalMs(std::stoi(value));
        } else if (key == "disable_on_crash") {
            // Backward compatibility: invert the old setting
            config.rumbleWhenCrashed = std::stoi(value) == 0;
        }
        // Suspension effect
        else if (key == "susp_min_input") {
            config.suspensionEffect.minInput = parseFiniteFloat(value);
        } else if (key == "susp_max_input") {
            config.suspensionEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "susp_light_strength") {
            config.suspensionEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "susp_heavy_strength") {
            config.suspensionEffect.heavyStrength = parseFiniteFloat(value);
        } else if (key == "susp_split") {
            config.suspensionSplit = std::stoi(value) != 0;
        } else if (key == "susp_split_init") {
            config.suspensionSplitInitialized = std::stoi(value) != 0;
        } else if (key == "susp_front_min_input") {
            config.suspensionEffectFront.minInput = parseFiniteFloat(value);
        } else if (key == "susp_front_max_input") {
            config.suspensionEffectFront.maxInput = parseFiniteFloat(value);
        } else if (key == "susp_front_light_strength") {
            config.suspensionEffectFront.lightStrength = parseFiniteFloat(value);
        } else if (key == "susp_front_heavy_strength") {
            config.suspensionEffectFront.heavyStrength = parseFiniteFloat(value);
        } else if (key == "susp_rear_min_input") {
            config.suspensionEffectRear.minInput = parseFiniteFloat(value);
        } else if (key == "susp_rear_max_input") {
            config.suspensionEffectRear.maxInput = parseFiniteFloat(value);
        } else if (key == "susp_rear_light_strength") {
            config.suspensionEffectRear.lightStrength = parseFiniteFloat(value);
        } else if (key == "susp_rear_heavy_strength") {
            config.suspensionEffectRear.heavyStrength = parseFiniteFloat(value);
        }
        // Wheelspin effect
        else if (key == "wheel_min_input") {
            config.wheelspinEffect.minInput = parseFiniteFloat(value);
        } else if (key == "wheel_max_input") {
            config.wheelspinEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "wheel_light_strength") {
            config.wheelspinEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "wheel_heavy_strength") {
            config.wheelspinEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Brake lockup effect
        else if (key == "lockup_min_input") {
            config.brakeLockupEffect.minInput = parseFiniteFloat(value);
        } else if (key == "lockup_max_input") {
            config.brakeLockupEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "lockup_light_strength") {
            config.brakeLockupEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "lockup_heavy_strength") {
            config.brakeLockupEffect.heavyStrength = parseFiniteFloat(value);
        } else if (key == "lockup_split") {
            config.brakeLockupSplit = std::stoi(value) != 0;
        } else if (key == "lockup_split_init") {
            config.brakeLockupSplitInitialized = std::stoi(value) != 0;
        } else if (key == "lockup_front_min_input") {
            config.brakeLockupEffectFront.minInput = parseFiniteFloat(value);
        } else if (key == "lockup_front_max_input") {
            config.brakeLockupEffectFront.maxInput = parseFiniteFloat(value);
        } else if (key == "lockup_front_light_strength") {
            config.brakeLockupEffectFront.lightStrength = parseFiniteFloat(value);
        } else if (key == "lockup_front_heavy_strength") {
            config.brakeLockupEffectFront.heavyStrength = parseFiniteFloat(value);
        } else if (key == "lockup_rear_min_input") {
            config.brakeLockupEffectRear.minInput = parseFiniteFloat(value);
        } else if (key == "lockup_rear_max_input") {
            config.brakeLockupEffectRear.maxInput = parseFiniteFloat(value);
        } else if (key == "lockup_rear_light_strength") {
            config.brakeLockupEffectRear.lightStrength = parseFiniteFloat(value);
        } else if (key == "lockup_rear_heavy_strength") {
            config.brakeLockupEffectRear.heavyStrength = parseFiniteFloat(value);
        }
        // RPM effect
        else if (key == "rpm_min_input") {
            config.rpmEffect.minInput = parseFiniteFloat(value);
        } else if (key == "rpm_max_input") {
            config.rpmEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "rpm_light_strength") {
            config.rpmEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "rpm_heavy_strength") {
            config.rpmEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Slide effect
        else if (key == "slide_min_input") {
            config.slideEffect.minInput = parseFiniteFloat(value);
        } else if (key == "slide_max_input") {
            config.slideEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "slide_light_strength") {
            config.slideEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "slide_heavy_strength") {
            config.slideEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Surface effect
        else if (key == "surface_min_input") {
            config.surfaceEffect.minInput = parseFiniteFloat(value);
        } else if (key == "surface_max_input") {
            config.surfaceEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "surface_light_strength") {
            config.surfaceEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "surface_heavy_strength") {
            config.surfaceEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Steer effect
        else if (key == "steer_min_input") {
            config.steerEffect.minInput = parseFiniteFloat(value);
        } else if (key == "steer_max_input") {
            config.steerEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "steer_light_strength") {
            config.steerEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "steer_heavy_strength") {
            config.steerEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Wheelie effect
        else if (key == "wheelie_min_input") {
            config.wheelieEffect.minInput = parseFiniteFloat(value);
        } else if (key == "wheelie_max_input") {
            config.wheelieEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "wheelie_light_strength") {
            config.wheelieEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "wheelie_heavy_strength") {
            config.wheelieEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Rev limiter effect
        else if (key == "revlim_min_input") {
            config.revLimiterEffect.minInput = parseFiniteFloat(value);
        } else if (key == "revlim_max_input") {
            config.revLimiterEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "revlim_light_strength") {
            config.revLimiterEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "revlim_heavy_strength") {
            config.revLimiterEffect.heavyStrength = parseFiniteFloat(value);
        }
        // Pit limiter effect
        else if (key == "pitlim_min_input") {
            config.pitLimiterEffect.minInput = parseFiniteFloat(value);
        } else if (key == "pitlim_max_input") {
            config.pitLimiterEffect.maxInput = parseFiniteFloat(value);
        } else if (key == "pitlim_light_strength") {
            config.pitLimiterEffect.lightStrength = parseFiniteFloat(value);
        } else if (key == "pitlim_heavy_strength") {
            config.pitLimiterEffect.heavyStrength = parseFiniteFloat(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Rumble: Failed to parse settings: %s", e.what());
    }
}

// Write HelmetOverlay section (global, not per-profile)
void SettingsManager::writeHelmetOverlaySettings(std::ostream& out, const HudManager& hudManager) const {
    const auto& hud = hudManager.getHelmetOverlayHud();
    out << "[HelmetOverlay]\n";
    out << "visible=" << (hud.isVisible() ? 1 : 0) << "\n";
    out << "helmetEnabled=" << (hud.m_helmetEnabled ? 1 : 0) << "\n";
    out << "visorMode=" << hud.m_visorMode << "\n";
    out << "helmetUpperVariant=" << hud.m_helmetUpperVariant << "\n";
    out << "helmetLowerVariant=" << hud.m_helmetLowerVariant << "\n";
    out << "helmetUpperOffsetY=" << hud.m_helmetUpperOffsetY << "\n";
    out << "helmetLowerOffsetY=" << hud.m_helmetLowerOffsetY << "\n";
    out << "helmetTiltStrength=" << hud.m_helmetTiltStrength << "\n";
    out << "helmetVibrationStrength=" << hud.m_helmetVibrationStrength << "\n";
    out << "helmetVibrationSensitivity=" << hud.m_helmetVibrationSensitivity << "\n";
    out << "helmetZoom=" << hud.m_helmetZoom << "\n";
    out << "visorTintColor=" << PluginUtils::formatColorHex(hud.m_visorTintColor) << "\n";
    out << "visorTintOpacity=" << hud.m_visorTintOpacity << "\n\n";
}

// Handle HelmetOverlay section (global, not per-profile)
void SettingsManager::applyHelmetOverlayLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    auto& hud = hudManager.getHelmetOverlayHud();
    try {
        if (key == "visible") {
            hud.setVisible(std::stoi(value) != 0);
        } else if (key == "helmetEnabled") {
            hud.m_helmetEnabled = std::stoi(value) != 0;
        } else if (key == "visorMode") {
            hud.m_visorMode = std::clamp(std::stoi(value), 0, HelmetOverlayHud::VISOR_MODE_COUNT - 1);
        } else if (key == "helmetUpperVariant") {
            hud.m_helmetUpperVariant = std::stoi(value);
        } else if (key == "helmetLowerVariant") {
            hud.m_helmetLowerVariant = std::stoi(value);
        } else if (key == "helmetUpperOffsetY") {
            hud.m_helmetUpperOffsetY = parseFiniteFloat(value);
        } else if (key == "helmetLowerOffsetY") {
            hud.m_helmetLowerOffsetY = parseFiniteFloat(value);
        } else if (key == "helmetTiltStrength") {
            hud.m_helmetTiltStrength = parseFiniteFloat(value);
        } else if (key == "helmetVibrationStrength") {
            hud.m_helmetVibrationStrength = parseFiniteFloat(value);
        } else if (key == "helmetVibrationSensitivity") {
            hud.m_helmetVibrationSensitivity = parseFiniteFloat(value);
        } else if (key == "helmetZoom") {
            hud.m_helmetZoom = parseFiniteFloat(value);
        } else if (key == "visorTintColor") {
            hud.m_visorTintColor = PluginUtils::parseColorHex(value, hud.m_visorTintColor);
        } else if (key == "visorTintOpacity") {
            hud.m_visorTintOpacity = parseFiniteFloat(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("HelmetOverlay: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
}

// Write Spotter section (global, not per-profile)
void SettingsManager::writeSpotterSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const SpotterManager& spotter = SpotterManager::getInstance();
    out << "[Spotter]\n";
    out << "enabled=" << (spotter.isEnabled() ? 1 : 0) << "\n";
    out << "subtitles=" << (spotter.isSubtitlesEnabled() ? 1 : 0) << "\n";
    out << "volume=" << spotter.getVolume() << " ; TTS volume 0-100\n";
    // Two decimals, always: the stepper adds 0.05 repeatedly, so the raw
    // float is 1.4499998 by the tenth click, and the default stream
    // format writes unity as a bare "1" — a float that reads as a
    // boolean, which is exactly what the persistence test's 0/1 flip
    // then "toggles" into a clamp.
    char speedBuf[16];
    snprintf(speedBuf, sizeof(speedBuf), "%.2f", spotter.getSpeed());
    out << "speed=" << speedBuf << " ; playback speed multiplier 0.50..2.00\n";
    // One toggle per cue category (SpotterPhrase::Category order).
    // Pack folder name under mxbmrp3_data/spotters. NO INLINE COMMENT, and it
    // must stay that way: `pack` is one of the keys the loader does not strip
    // a `;` from (Settings::isFolderNameValue -- a semicolon is legal in a
    // folder name and truncating it destroys the stored choice), so a comment
    // added here would be read as part of the name. tts_voice below keeps its
    // comment and keeps stripping, which is why it is not on that list.
    // reloadCuePack reads an empty name as the shipped pack.
    out << Settings::Keys::Global::SPOTTER_PACK << "="
        << spotter.getPackName() << "\n";
    out << "tts_voice=" << spotter.getTtsVoice() << " ; Windows voice for TTS cues (blank = system default)\n";
    out << "cat_general=" << (spotter.isCategoryEnabled(SpotterPhrase::Category::General) ? 1 : 0) << "\n";
    out << "cat_timing=" << (spotter.isCategoryEnabled(SpotterPhrase::Category::Timing) ? 1 : 0) << "\n";
    out << "cat_opponents=" << (spotter.isCategoryEnabled(SpotterPhrase::Category::Opponents) ? 1 : 0) << "\n";
    out << "cat_proximity=" << (spotter.isCategoryEnabled(SpotterPhrase::Category::Proximity) ? 1 : 0) << "\n";
    out << "cat_hazard=" << (spotter.isCategoryEnabled(SpotterPhrase::Category::Hazard) ? 1 : 0) << "\n";
    // Proximity/hazard cue tuning (INI-only, like [Advanced]'s hazard knobs).
    const SpotterHazard::Config& hz = spotter.hazardConfig();
    // ORDER IS LOAD-BEARING, like the cat_opponents/cat_proximity pair above:
    // every *_on_m setter raises its matching release band to stay ahead of it
    // (the hysteresis can't invert), so writing *_clear_m FIRST would have it
    // clamped away by the *_on_m that follows. Same file, same rule, and the
    // symptom is a tuned release distance silently snapping back on reload.
    out << "behind_on_m=" << hz.behindOnMeters << " ; 'rider behind' within this many meters\n";
    out << "behind_clear_m=" << hz.clearMeters << " ; 'clear' once past this (hysteresis)\n";
    out << "alongside_on_m=" << hz.alongsideOnMeters << " ; 'rider left/right' when overlapped within this far BEHIND\n";
    out << "alongside_ahead_m=" << hz.alongsideAheadMeters << " ; ...and this far AHEAD (short: you can see those)\n";
    out << "alongside_clear_m=" << hz.alongsideClearMeters << " ; side held until past this (hysteresis)\n";
    out << "lateral_m=" << hz.lateralMeters << " ; ignore riders further ACROSS the track than this\n";
    out << "behind_repeat_ms=" << hz.behindRepeatMs << "\n";
    out << "behind_clear_min_ms=" << hz.clearMinEpisodeMs << " ; a 'clear' this soon after contact stays silent (0 = always voice)\n";
    out << "blue_cooldown_ms=" << hz.blueFlagCooldownMs << "\n";
    out << "lapping_cooldown_ms=" << hz.lappingCooldownMs << "\n";
    out << "hazard_cooldown_ms=" << hz.hazardCooldownMs << "\n";
    out << "on_pace_margin_ms=" << spotter.getOnPaceMarginMs()
        << " ; 'on for a best' needs you this far up at the last split\n\n";
}

// Handle Spotter section (global, not per-profile)
void SettingsManager::applySpotterLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
    SpotterManager& spotter = SpotterManager::getInstance();
    try {
        if (key == "enabled") {
            spotter.setEnabled(std::stoi(value) != 0);
        } else if (key == "subtitles") {
            spotter.setSubtitlesEnabled(std::stoi(value) != 0);
        } else if (key == "volume") {
            spotter.setVolume(std::stoi(value));  // setter clamps
        } else if (key == "speed") {
            // Fallback 1.0, not the parser's 0: a garbled value should
            // read as "as recorded", where 0 would clamp to the slowest
            // speed in the range and sound like a deliberate choice.
            spotter.setSpeed(parseFiniteFloat(value, 1.0f));  // setter clamps
        } else if (key == "rate") {
            // LEGACY (pre-multiplier): SAPI's integer -10..10, whose scale
            // is ~3x at 10. Converted rather than dropped so an existing
            // INI keeps the pace its owner chose; the next save writes
            // `speed` and this key disappears.
            const int legacy = std::stoi(value);
            spotter.setSpeed(static_cast<float>(
                std::pow(3.0, static_cast<double>(legacy) / 10.0)));
        } else if (key == Settings::Keys::Global::SPOTTER_PACK) {
            spotter.setPackName(value);           // validates + loads
        } else if (key == "tts_voice") {
            spotter.setTtsVoice(value);           // resolved against the live list
        } else if (key == "behind_on_m") {
            spotter.setBehindOnMeters(parseFiniteFloat(value));
        } else if (key == "behind_clear_m") {
            spotter.setClearMeters(parseFiniteFloat(value));
        } else if (key == "alongside_on_m") {
            spotter.setAlongsideOnMeters(parseFiniteFloat(value));
        } else if (key == "alongside_ahead_m") {
            spotter.setAlongsideAheadMeters(parseFiniteFloat(value));
        } else if (key == "alongside_clear_m") {
            spotter.setAlongsideClearMeters(parseFiniteFloat(value));
        } else if (key == "lateral_m") {
            spotter.setLateralMeters(parseFiniteFloat(value));
        } else if (key == "behind_repeat_ms") {
            spotter.setBehindRepeatMs(std::stoi(value));
        } else if (key == "behind_clear_min_ms") {
            spotter.setClearMinEpisodeMs(std::stoi(value));
        } else if (key == "blue_cooldown_ms") {
            spotter.setBlueFlagCooldownMs(std::stoi(value));
        } else if (key == "lapping_cooldown_ms") {
            spotter.setLappingCooldownMs(std::stoi(value));
        } else if (key == "hazard_cooldown_ms") {
            spotter.setHazardCooldownMs(std::stoi(value));
        } else if (key == "on_pace_margin_ms") {
            spotter.setOnPaceMarginMs(std::stoi(value));
        } else if (key == "cat_general") {
            spotter.setCategoryEnabled(SpotterPhrase::Category::General, std::stoi(value) != 0);
        } else if (key == "cat_timing") {
            spotter.setCategoryEnabled(SpotterPhrase::Category::Timing, std::stoi(value) != 0);
        } else if (key == "cat_proximity") {
            spotter.setCategoryEnabled(SpotterPhrase::Category::Proximity, std::stoi(value) != 0);
        } else if (key == "cat_opponents") {
            // A file written before cat_proximity existed has no such line
            // and its cat_opponents answers for both. Mirror it, and let the
            // cat_proximity line — which writeSpotterSettings emits
            // immediately after this one — overwrite that guess whenever
            // the file is new enough to carry one. Without this, somebody
            // who had muted the whole group gets the spotting half back
            // talking after an upgrade.
            //
            // Order-dependent by exactly that much: the mirror is only
            // correct because our own writer puts cat_proximity after
            // cat_opponents. Keep them in that order if either moves.
            spotter.setCategoryEnabled(SpotterPhrase::Category::Proximity, std::stoi(value) != 0);
            spotter.setCategoryEnabled(SpotterPhrase::Category::Opponents, std::stoi(value) != 0);
        } else if (key == "cat_hazard") {
            spotter.setCategoryEnabled(SpotterPhrase::Category::Hazard, std::stoi(value) != 0);
        }
    } catch (...) {
        DEBUG_WARN_F("Settings: Invalid [Spotter] value: %s=%s",
                     key.c_str(), value.c_str());
    }
}

// Write Director section (global, not per-profile)
void SettingsManager::writeDirectorSettings(std::ostream& out, const HudManager& hudManager) const {
    const DirectorManager& director = DirectorManager::getInstance();
    out << "[Director]\n";
    out << "enabled=" << (director.isEnabled() ? 1 : 0) << "\n";
    out << "minShotSec=" << director.getMinShotSec() << "\n";
    out << "maxShotSec=" << director.getMaxShotSec() << "\n";
    out << "battleGapMs=" << director.getBattleGapMs() << "\n";
    out << "battleMaxPos=" << director.getBattleMaxPos() << "\n";
    out << "manualResumeSec=" << director.getManualResumeSec() << "\n";
    out << "gamepadTakeover=" << (director.getGamepadTakeover() ? 1 : 0) << "\n";
    out << "camFront=" << (director.getCamFront() ? 1 : 0) << "\n";
    out << "camRear=" << (director.getCamRear() ? 1 : 0) << "\n";
    out << "camHelmet=" << (director.getCamHelmet() ? 1 : 0) << "\n";
    out << "camHelmet2=" << (director.getCamHelmet2() ? 1 : 0) << "\n";
    out << "camForks=" << (director.getCamForks() ? 1 : 0) << "\n";
    out << "followBattles=" << (director.getFollowBattles() ? 1 : 0) << "\n";
    out << "followIncidents=" << (director.getFollowIncidents() ? 1 : 0) << "\n";
    out << "followFastestLap=" << (director.getFollowFastestLap() ? 1 : 0) << "\n";
    out << "finishLock=" << (director.getFinishLock() ? 1 : 0) << "\n";
    out << "catchOvertakes=" << (director.getCatchOvertakes() ? 1 : 0) << "\n";
    out << "followLappers=" << (director.getFollowLappers() ? 1 : 0) << "\n";
    out << "followDrops=" << (director.getFollowDrops() ? 1 : 0) << "\n";
    out << "followPace=" << (director.getFollowPace() ? 1 : 0) << "\n";
    out << "varietyEvery=" << director.getVarietyEvery() << "\n";
    out << "holdSec=" << director.getHoldSec() << "\n";
    out << "incidentMaxSec=" << director.getIncidentMaxSec()
        << " ; Longest incident the director will hold a camera on, in seconds\n";
    // The status-button HUD is global too (like HelmetOverlay) - persist its base
    // settings here rather than in the per-profile HUD cache.
    if (const DirectorWidget* hud = hudManager.getDirectorWidget()) {
        out << "hudVisible=" << (hud->isVisible() ? 1 : 0) << "\n";
        out << "hudX=" << hud->getOffsetX() << "\n";
        out << "hudY=" << hud->getOffsetY() << "\n";
        out << "hudScale=" << hud->getScale() << "\n";
        out << "hudOpacity=" << hud->getBackgroundOpacity() << "\n";
    }
    out << "\n";
}

// Handle Director section (global, not per-profile)
void SettingsManager::applyDirectorLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    DirectorManager& director = DirectorManager::getInstance();
    try {
        if (key == "enabled") {
            director.setEnabled(std::stoi(value) != 0);
        } else if (key == "minShotSec") {
            director.setMinShotSec(std::stoi(value));
        } else if (key == "maxShotSec") {
            director.setMaxShotSec(std::stoi(value));
        } else if (key == "battleGapMs") {
            director.setBattleGapMs(std::stoi(value));
        } else if (key == "battleMaxPos") {
            director.setBattleMaxPos(std::stoi(value));
        } else if (key == "manualResumeSec") {
            director.setManualResumeSec(std::stoi(value));
        } else if (key == "gamepadTakeover") {
            director.setGamepadTakeover(std::stoi(value) != 0);
        } else if (key == "camFront") {
            director.setCamFront(std::stoi(value) != 0);
        } else if (key == "camRear") {
            director.setCamRear(std::stoi(value) != 0);
        } else if (key == "camHelmet") {
            director.setCamHelmet(std::stoi(value) != 0);
        } else if (key == "camHelmet2") {
            director.setCamHelmet2(std::stoi(value) != 0);
        } else if (key == "camForks") {
            director.setCamForks(std::stoi(value) != 0);
        } else if (key == "followBattles") {
            director.setFollowBattles(std::stoi(value) != 0);
        } else if (key == "followIncidents") {
            director.setFollowIncidents(std::stoi(value) != 0);
        } else if (key == "followFastestLap") {
            director.setFollowFastestLap(std::stoi(value) != 0);
        } else if (key == "finishLock") {
            director.setFinishLock(std::stoi(value) != 0);
        } else if (key == "catchOvertakes") {
            director.setCatchOvertakes(std::stoi(value) != 0);
        } else if (key == "followLappers") {
            director.setFollowLappers(std::stoi(value) != 0);
        } else if (key == "followDrops") {
            director.setFollowDrops(std::stoi(value) != 0);
        } else if (key == "followPace") {
            director.setFollowPace(std::stoi(value) != 0);
        } else if (key == "varietyEvery") {
            director.setVarietyEvery(std::stoi(value));
        } else if (key == "holdSec" || key == "incidentLingerSec") {
            // incidentLingerSec is the legacy key for the (now shared) hold.
            director.setHoldSec(std::stoi(value));
        } else if (key == "incidentMaxSec") {
            director.setIncidentMaxSec(std::stoi(value));
        } else if (key == "hudVisible") {
            if (auto* h = hudManager.getDirectorWidget()) h->setVisible(std::stoi(value) != 0);
        } else if (key == "hudX") {
            // isfinite-guard persisted floats: a garbage/Inf/NaN value in the INI
            // must not reach setPosition/setScale (invariant, like finiteOrZero in
            // stats_manager). If it isn't finite, keep the constructor default.
            // NOTE: use std::stof here, NOT parseFiniteFloat — parseFiniteFloat maps
            // non-finite to 0.0f (which passes the isfinite check below and would set
            // 0.0 instead of keeping the default). This site owns its own guard.
            float v = std::stof(value);
            if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setPosition(v, h->getOffsetY()); }
        } else if (key == "hudY") {
            float v = std::stof(value);
            if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setPosition(h->getOffsetX(), v); }
        } else if (key == "hudScale") {
            float v = std::stof(value);
            if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setScale(v); }
        } else if (key == "hudOpacity") {
            float v = std::stof(value);
            if (std::isfinite(v)) { if (auto* h = hudManager.getDirectorWidget()) h->setBackgroundOpacity(v); }
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Director: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
}

// Write Achievements section (global; the toast widget's geometry lives here
// like the Director button's, so a profile switch never moves it).
void SettingsManager::writeAchievementsSettings(std::ostream& out, const HudManager& hudManager) const {
    const AchievementManager& ach = AchievementManager::getInstance();
    out << "[Achievements]\n";
    // "visible", not "showToasts": the toast master IS this element's
    // visibility, and the harness isolates HUDs by rewriting every visible= key.
    out << "visible=" << (ach.isToastsEnabled() ? 1 : 0) << "\n";
    out << "toastMs=" << ach.getToastDurationMs() << "\n";
    if (const AchievementWidget* hud = hudManager.getAchievementWidget()) {
        out << "hudX=" << hud->getOffsetX() << "\n";
        out << "hudY=" << hud->getOffsetY() << "\n";
        out << "hudScale=" << hud->getScale() << "\n";
        out << "hudOpacity=" << hud->getBackgroundOpacity() << "\n";
        out << "hudTitle=" << (hud->getShowTitle() ? 1 : 0) << "\n";
        // The tab's Texture row edits one or the other (texture variants when the
        // widget has any, else the panel theme), so both persist. The theme is
        // written empty too: empty is "follow the global theme", and the apply
        // needs to see it to clear a pin.
        out << "hudTexture=" << hud->getTextureVariant() << "\n";
        out << "hudTheme=" << hud->getThemeOverride() << "\n";
    }
    out << "devToast=" << (ach.isDevToastEnabled() ? 1 : 0)
        << " ; Dev-only: every config reload queues a test toast\n";
    // Written only while it is on, so a test session's knob cannot linger unseen.
    if (ach.getDevValueScale() != 1.0) {
        out << "devScale=" << ach.getDevValueScale()
            << " ; Dev-only: every achievement number, times this\n";
    }
    out << "\n";
}

// Handle Achievements section (global, not per-profile)
void SettingsManager::applyAchievementsLine(const std::string& key, const std::string& value, HudManager& hudManager) {
    AchievementManager& ach = AchievementManager::getInstance();
    AchievementWidget* hud = hudManager.getAchievementWidget();
    try {
        if (key == "visible") {
            ach.setToastsEnabled(std::stoi(value) != 0);
        } else if (key == "toastMs") {
            ach.setToastDurationMs(std::stoi(value));
        } else if (key == "devToast") {
            ach.setDevToastEnabled(std::stoi(value) != 0);
        } else if (key == "devScale") {
            ach.setDevValueScale(std::stod(value));
        } else if (key == "hudX") {   // isfinite-guarded like the Director's geometry
            float v = std::stof(value);
            if (std::isfinite(v) && hud) hud->setPosition(v, hud->getOffsetY());
        } else if (key == "hudY") {
            float v = std::stof(value);
            if (std::isfinite(v) && hud) hud->setPosition(hud->getOffsetX(), v);
        } else if (key == "hudScale") {
            float v = std::stof(value);
            if (std::isfinite(v) && hud) hud->setScale(v);
        } else if (key == "hudOpacity") {
            float v = std::stof(value);
            if (std::isfinite(v) && hud) hud->setBackgroundOpacity(v);
        } else if (key == "hudTitle") {
            if (hud) hud->setShowTitle(std::stoi(value) != 0);
        } else if (key == "hudTexture") {
            if (hud) hud->setTextureVariant(std::stoi(value));
        } else if (key == "hudTheme") {
            // Stored verbatim; an unknown name falls back at render time.
            if (hud) hud->setThemeOverride(value);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Achievements: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
}

#if GAME_HAS_RECORDER
// Write Recorder section (global; hidden developer tool). Off by default;
// a developer sets enabled=1 by hand-editing the INI to capture a callback
// tape for the test harness. No HUD / no hotkey / no settings-menu control.
void SettingsManager::writeRecorderSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    out << "[Recorder]\n";
    out << "enabled=" << (EventRecorder::getInstance().isRecordingEnabled() ? 1 : 0)
        << " ; Dev-only: capture the raw callback stream to mxbmrp3\\tapes\\ for headless replay\n\n";
}

// Handle Recorder section (hidden dev tool). Only reads the enabled flag;
// the actual session tape is opened at startup if enabled (see plugin_manager).
void SettingsManager::applyRecorderLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
    try {
        if (key == "enabled") {
            EventRecorder::getInstance().setRecordingEnabled(std::stoi(value) != 0);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Recorder: Failed to parse setting '%s': %s", key.c_str(), e.what());
    }
}
#endif

// Write Hotkeys section. Keys are named per action (e.g. standings_key) so
// the file is self-documenting; values are numeric codes: _key = Windows
// virtual-key code, _mod = modifier bitmask (1=Ctrl, 2=Shift, 4=Alt),
// _btn = controller button. 0 means unbound. Actions with no row in the
// settings UI (e.g. rumble/helmet/performance) are still written here and
// can be bound by hand-editing.
void SettingsManager::writeHotkeysSettings(std::ostream& out, const HudManager& /*hudManager*/) const {
    const HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    out << "[Hotkeys]\n";
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        HotkeyAction action = static_cast<HotkeyAction>(i);
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);
        const char* name = getActionConfigName(action);

        out << name << "_key=" << static_cast<int>(binding.keyboard.keyCode) << "\n";
        out << name << "_mod=" << static_cast<int>(binding.keyboard.modifiers) << "\n";
        out << name << "_btn=" << static_cast<int>(binding.controller) << "\n";
    }
    out << "\n";
}

// Handle Hotkeys section
void SettingsManager::applyHotkeysLine(const std::string& key, const std::string& value, HudManager& /*hudManager*/) {
    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    try {
        // Split at the LAST underscore: <name>_<suffix> (suffix = key/mod/btn).
        // Names can contain underscores (e.g. "lap_log", "overlay_last_lap").
        size_t lastUnderscore = key.rfind('_');
        if (lastUnderscore == std::string::npos) return;
        std::string name = key.substr(0, lastUnderscore);
        std::string suffix = key.substr(lastUnderscore + 1);

        // Resolve the action. New keys are name-based ("standings_key"); the
        // old index-based form ("action0_key") is still accepted so existing
        // configs migrate automatically - read here, written back in the new
        // form on the next save.
        HotkeyAction action = HotkeyAction::COUNT;
        if (name.length() > 6 && name.substr(0, 6) == "action") {
            int idx = std::stoi(name.substr(6));
            if (idx >= 0 && idx < static_cast<int>(HotkeyAction::COUNT)) {
                action = static_cast<HotkeyAction>(idx);
            }
        } else {
            for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
                if (name == getActionConfigName(static_cast<HotkeyAction>(i))) {
                    action = static_cast<HotkeyAction>(i);
                    break;
                }
            }
        }

        if (action != HotkeyAction::COUNT) {
            HotkeyBinding binding = hotkeyMgr.getBinding(action);
            if (suffix == "key") {
                binding.keyboard.keyCode = static_cast<uint8_t>(std::stoi(value));
            } else if (suffix == "mod") {
                binding.keyboard.modifiers = static_cast<ModifierFlags>(std::stoi(value));
            } else if (suffix == "btn") {
                binding.controller = static_cast<ControllerButton>(std::stoi(value));
            }
            hotkeyMgr.setBinding(action, binding);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Hotkeys: Failed to parse settings: %s", e.what());
    }
}
