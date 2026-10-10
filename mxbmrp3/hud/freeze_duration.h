// ============================================================================
// hud/freeze_duration.h
// The one range and default for every "hold the time after a split or the
// line" setting: the Timing HUD's Freeze, the Gap Bar's Freeze and the Lap
// Log's Gap freeze. Each HUD keeps its own value, but they share these limits
// so the steppers read the same and a reset puts them all on the same hold.
// They drifted apart once (Timing at 5 s, the gap HUDs at 3 s).
// The Pitboard's At Splits Freeze shares the range and step but not the rest:
// no Off (a zero hold would hide the board for good) and its own 10 s default,
// the fixed hold it had before it was a setting.
// All four tabs draw the row with SettingsLayoutContext::addFreezeControl.
// The three gap holds also take FOLLOW_DEFAULT: the General tab's Freeze
// (UiConfig::getDefaultFreezeMs, resolved in hud_defaults.h).
// ============================================================================
#pragma once

namespace FreezeDuration {
    constexpr int MIN_MS = 0;         // 0 = Off
    constexpr int MAX_MS = 10000;     // 10 seconds maximum
    constexpr int DEFAULT_MS = 5000;  // 5 seconds default
    constexpr int STEP_MS = 1000;     // 1 second steps
    // "Default": follow the General tab's Freeze. One step below Off, so the
    // slider runs Default, Off, 1 s ... 10 s and the arrows step through it too.
    constexpr int FOLLOW_DEFAULT = MIN_MS - STEP_MS;

    // The Pitboard's At Splits hold: no Off, and its own default
    constexpr int PITBOARD_MIN_MS = STEP_MS;
    constexpr int PITBOARD_DEFAULT_MS = 10000;
}
