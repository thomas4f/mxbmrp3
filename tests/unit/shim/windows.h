// ============================================================================
// tests/unit/shim/windows.h
// Minimal <windows.h> stand-in so the plugin's SETTINGS headers compile with a
// native Linux g++ in the 1-second unit layer.
//
// WHY THIS EXISTS. settings_serde.h is ~1,500 lines of pure enum<->string and
// bitmask serde — exactly the shape the unit layer is for — but it was
// untestable there, because it reaches HUD class definitions for their nested
// enums (StandingsHud::GapMode and friends) and that chain ends at
// input_manager.h's `#include <windows.h>` for one typedef. Rather than push a
// pure-logic test down into the mingw+Wine integration layer (minutes instead of
// seconds, for arithmetic that touches no Windows API), this supplies the handful
// of typedefs the chain actually needs.
//
// SCOPE, deliberately: types only, no functions. Nothing here is called — these
// headers only need the names to exist so declarations parse. If a test ever
// needs a real Win32 CALL, that is the signal it belongs in the integration
// layer, not that this file should grow an implementation.
//
// It is test-only: force-found via -I on the unit build's command line, never on
// any shipped translation unit's include path.
// ============================================================================
#pragma once

#include <cstdint>

// Opaque handles. The settings/HUD headers store and pass these; they never
// dereference them, so void* is a faithful stand-in.
using HWND      = void*;
using HMODULE   = void*;
using HANDLE    = void*;
using HINSTANCE = void*;
using HDC       = void*;
using HBITMAP   = void*;
using HBRUSH    = void*;

// Win32 scalar spellings. Sized to the LLP64 target the plugin ships on, so a
// declaration that compiles here compiles there.
using DWORD     = unsigned long;
using WORD      = unsigned short;
using BYTE      = unsigned char;
using SHORT     = short;
using USHORT    = unsigned short;
using BOOL      = int;
using UINT      = unsigned int;
using LONG      = long;
using ULONG     = unsigned long;
using LONGLONG  = long long;
using ULONGLONG = unsigned long long;
using COLORREF  = unsigned long;
using LPVOID    = void*;
using LPCSTR    = const char*;
using LPSTR     = char*;
using WPARAM    = uintptr_t;
using LPARAM    = intptr_t;
using LRESULT   = intptr_t;

struct POINT { long x, y; };
struct RECT  { long left, top, right, bottom; };

#define WINAPI
#define CALLBACK
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// ---------------------------------------------------------------------------
// Virtual-key codes. hotkey_config.h switches on these to render key names, so
// the settings include chain needs every one the plugin references.
//
// The VALUES are not invented: they were extracted from mingw-w64's real
// winuser.h (all 75 the plugin uses resolved, none missing) by
// tests/unit/shim/regen_constants.sh, so this agrees with the Win32 ABI the
// shipping build compiles against rather than with someone's recollection of it.
// `regen_constants.sh --check` re-verifies this block against the real header in
// CI, so it cannot drift silently. Re-run the generator if the plugin starts
// using a key not listed here — the build tells you, since an unknown VK_ is a
// hard compile error, not a silent zero.
// ---------------------------------------------------------------------------
#define VK_LBUTTON    0x01
#define VK_RBUTTON    0x02
#define VK_MBUTTON    0x04
#define VK_XBUTTON1   0x05
#define VK_XBUTTON2   0x06
#define VK_BACK       0x08
#define VK_TAB        0x09
#define VK_RETURN     0x0D
#define VK_SHIFT      0x10
#define VK_CONTROL    0x11
#define VK_MENU       0x12
#define VK_PAUSE      0x13
#define VK_CAPITAL    0x14
#define VK_ESCAPE     0x1B
#define VK_SPACE      0x20
#define VK_PRIOR      0x21
#define VK_NEXT       0x22
#define VK_END        0x23
#define VK_HOME       0x24
#define VK_LEFT       0x25
#define VK_UP         0x26
#define VK_RIGHT      0x27
#define VK_DOWN       0x28
#define VK_SNAPSHOT   0x2C
#define VK_INSERT     0x2D
#define VK_DELETE     0x2E
#define VK_LWIN       0x5B
#define VK_RWIN       0x5C
#define VK_APPS       0x5D
#define VK_NUMPAD0    0x60
#define VK_NUMPAD1    0x61
#define VK_NUMPAD2    0x62
#define VK_NUMPAD3    0x63
#define VK_NUMPAD4    0x64
#define VK_NUMPAD5    0x65
#define VK_NUMPAD6    0x66
#define VK_NUMPAD7    0x67
#define VK_NUMPAD8    0x68
#define VK_NUMPAD9    0x69
#define VK_MULTIPLY   0x6A
#define VK_ADD        0x6B
#define VK_SUBTRACT   0x6D
#define VK_DECIMAL    0x6E
#define VK_DIVIDE     0x6F
#define VK_F1         0x70
#define VK_F2         0x71
#define VK_F3         0x72
#define VK_F4         0x73
#define VK_F5         0x74
#define VK_F6         0x75
#define VK_F7         0x76
#define VK_F8         0x77
#define VK_F9         0x78
#define VK_F10        0x79
#define VK_F11        0x7A
#define VK_F12        0x7B
#define VK_NUMLOCK    0x90
#define VK_SCROLL     0x91
#define VK_LSHIFT     0xA0
#define VK_RSHIFT     0xA1
#define VK_LCONTROL   0xA2
#define VK_RCONTROL   0xA3
#define VK_LMENU      0xA4
#define VK_RMENU      0xA5
#define VK_OEM_1      0xBA
#define VK_OEM_PLUS   0xBB
#define VK_OEM_COMMA  0xBC
#define VK_OEM_MINUS  0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2      0xBF
#define VK_OEM_3      0xC0
#define VK_OEM_4      0xDB
#define VK_OEM_5      0xDC
#define VK_OEM_6      0xDD
#define VK_OEM_7      0xDE
