// ============================================================================
// core/render_pace.h
// The one render-cadence rule for the plugin's window threads (companion +
// overlay): hz > 0 caps at a fixed rate; 0 means V-Sync (DwmFlush blocks until
// the compositor's next present — tear-free and matched to the monitor),
// falling back to ~60 Hz when the DWM is off (rare on modern Windows; also a
// bare Wine prefix) so the loop still paces instead of spinning. One
// definition so the loops that pace cannot drift on these semantics.
// ============================================================================
#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <dwmapi.h>

namespace renderpace {

inline void pace(int hz) {
    if (hz > 0) Sleep(1000 / hz);
    else if (FAILED(DwmFlush())) Sleep(16);
}

}  // namespace renderpace
#endif
