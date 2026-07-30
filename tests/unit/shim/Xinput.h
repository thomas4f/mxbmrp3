// ============================================================================
// tests/unit/shim/Xinput.h
// Minimal <Xinput.h> stand-in for the native unit build. See windows.h beside it
// for why the settings headers need Win32 names at all.
//
// SCOPE. hotkey_config.h gives its GamepadButton enum the XINPUT_GAMEPAD_* bit
// values directly, so those masks must be real — a wrong one would silently
// remap a user's bound button. They are generated from mingw-w64's own xinput.h
// by regen_constants.sh, not written out by hand.
//
// The STRUCTS are FORWARD-DECLARED ONLY, which is both sufficient and safer.
// Exactly one header mentions one of them — xinput_reader.h's
// `fillFromState(const XINPUT_STATE&, ...)` — and a reference parameter needs no
// definition. Declaring the layout here would mean copying field order and types
// out of the real header with nothing to verify them against; leaving it
// incomplete makes any accidental by-value use or member access a compile error
// instead of a silently wrong struct. If a test ever needs to CONSTRUCT one,
// that is the signal it belongs in the integration layer, which links the real
// mingw header.
//
// (tests/integration/shim/Xinput.h is a different thing: it exists only because
// Linux is case-sensitive and mingw ships the real header lowercase, so it
// forwards to it. Here there is no real header to forward to.)
// ============================================================================
#pragma once

struct XINPUT_STATE;
struct XINPUT_GAMEPAD;
struct XINPUT_VIBRATION;

#define XINPUT_GAMEPAD_DPAD_UP             0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN           0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT           0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT          0x0008
#define XINPUT_GAMEPAD_START               0x0010
#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD   30
#define XINPUT_GAMEPAD_BACK                0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB          0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB         0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER       0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER      0x0200
#define XINPUT_GAMEPAD_A                   0x1000
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849
#define XINPUT_GAMEPAD_B                   0x2000
#define XINPUT_GAMEPAD_X                   0x4000
#define XINPUT_GAMEPAD_Y                   0x8000
