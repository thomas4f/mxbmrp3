// ============================================================================
// hud/hud_defaults.h
// "Default" for a HUD's reference lap or freeze: the General tab's value.
//
// A HUD that compares against a reference lap (Gap Bar, Lap Log, Delta Trace,
// Map lap delta) or holds an official gap (Gap Bar, Lap Log, Timing) keeps its
// own setting, and that setting may be "Default" -- follow the one in General,
// so a player changes them all in one place and overrides only where they want.
// The HUD reads its setting through these, never the raw member, at every use.
// ============================================================================
#pragma once

#include "freeze_duration.h"
#include "../core/pb_gap_tracker.h"
#include "../core/ui_config.h"

namespace HudDefaults {
    // The reference a HUD measures against: its own, or General's when it follows.
    inline PbGapTracker::Ref reference(bool followDefault, PbGapTracker::Ref own) {
        return followDefault
            ? static_cast<PbGapTracker::Ref>(UiConfig::getInstance().getDefaultReference())
            : own;
    }
    // A reference row's "Default", naming the reference it stands for today, so
    // the player need not open General to learn it. Literals, no formatting.
    inline const char* defaultReferenceName() {
        static constexpr const char* kNames[PbGapTracker::REF_COUNT] = {
            "Default (Session PB)", "Default (All-time PB)", "Default (Last lap)" };
        const int i = UiConfig::getInstance().getDefaultReference();
        return kNames[i >= 0 && i < PbGapTracker::REF_COUNT ? i : 0];
    }
    // A hold in ms: its own, or General's for FreezeDuration::FOLLOW_DEFAULT.
    inline int freezeMs(int own) {
        return own < FreezeDuration::MIN_MS ? UiConfig::getInstance().getDefaultFreezeMs() : own;
    }
    // The stored "reference" value for a HUD following General: one past the
    // last real reference, so an INI keeps one key per setting.
    constexpr int REFERENCE_FOLLOW = PbGapTracker::REF_COUNT;
}
