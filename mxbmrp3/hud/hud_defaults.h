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
            "Default (Session PB)", "Default (All-time PB)", "Default (Last lap)", "Default (Server best)" };
        const int i = UiConfig::getInstance().getDefaultReference();
        return kNames[i >= 0 && i < PbGapTracker::REF_COUNT ? i : 0];
    }
    static_assert(UiConfig::REFERENCE_COUNT == PbGapTracker::REF_COUNT, "General's reference range");
    // A hold in ms: its own, or General's for FreezeDuration::FOLLOW_DEFAULT.
    inline int freezeMs(int own) {
        return own < FreezeDuration::MIN_MS ? UiConfig::getInstance().getDefaultFreezeMs() : own;
    }
    // The stored "reference" value for a HUD following General: one past the
    // references there were when it was chosen, so an INI keeps one key per
    // setting. A FIXED number: Server best came after it and is stored past it,
    // so a saved "Default" kept its meaning. Read and write a reference key only
    // through these two (pinned by test_hud_defaults.cpp).
    constexpr int REFERENCE_FOLLOW = 3;
    inline int storeReference(bool followDefault, PbGapTracker::Ref ref) {
        const int v = static_cast<int>(ref);
        return followDefault ? REFERENCE_FOLLOW : (v >= REFERENCE_FOLLOW ? v + 1 : v);
    }
    // False (both outputs untouched) for a value that is no reference.
    inline bool loadReference(int stored, bool* followDefault, PbGapTracker::Ref* ref) {
        if (stored == REFERENCE_FOLLOW) {
            *followDefault = true;
            return true;
        }
        const int v = stored > REFERENCE_FOLLOW ? stored - 1 : stored;
        if (v < 0 || v >= PbGapTracker::REF_COUNT) return false;
        *ref = static_cast<PbGapTracker::Ref>(v);
        *followDefault = false;
        return true;
    }
}
