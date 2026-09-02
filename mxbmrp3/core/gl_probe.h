// ============================================================================
// core/gl_probe.h
// PHASE 0 of the GL in-context HUD renderer spike (plans/gl_in_context_renderer.md).
// A read-only-by-default diagnostic that answers, in ONE game launch, the single
// question the whole project hangs on:
//
//   Is an OpenGL context current on the thread that receives the Draw callback,
//   and at a point in the frame where what we draw survives to the present?
//
// It is NOT a renderer and never becomes one. It reports, optionally draws one
// quad, and proves it left the context exactly as it found it.
//
// WHY IT HAS TO BE IN-GAME. Nothing in vendor/piboso/mxb_api.h says where in the
// frame Draw sits — its contract is "fill in these arrays", and that the engine
// renders them after the call returns is all we know. No headless test can
// answer it: there is no GL context, no game, and no screen. So the deliverable
// of Phase 0 is this probe plus instructions, and the answer comes from the
// author launching the game once.
//
// HOW IT IS STAGED. [Advanced] glProbe:
//   0  off (default) — not one GL call is made, not even a module lookup.
//   1  REPORT ONLY — glGetString/glGetIntegerv and nothing else. Cannot perturb
//      the game: no draw, no state change, no error-queue drain.
//   2  REPORT + DRAW one magenta bar at the top-left, with conservative state
//      save/restore, a glReadPixels confirming the draw executed, and a
//      before/after state diff confirming the restore was airtight.
// The risky half is opt-in separately from the safe half on purpose: a user who
// only needs to know "is it GL at all" never has to let the plugin draw.
//
// opengl32.dll is resolved with GetModuleHandle, NEVER LoadLibrary. If the
// module is not already resident the game is not a GL app — which is itself the
// answer — and loading it ourselves would be a side effect on a process we are
// only here to observe. Same no-new-import discipline core/hud_gpu_renderer.h
// follows for d3d11.dll, for the same reason: the plugin must gain no import
// the host could fail to resolve at load.
//
// THREADING: onDraw() must be called from the game's Draw callback thread and
// nowhere else — a GL context is per-thread, so asking the [Advanced]
// pluginThread worker would answer "no context" for reasons that have nothing
// to do with the game. It is called from PluginManager::handleDraw, which is
// that thread in BOTH the synchronous and the threaded render mode.
//
// SIDE EFFECTS, stated plainly because this code runs inside someone else's
// renderer: at glProbe=2 the probe consumes the GL error queue on its first few
// frames (it cannot verify its own errors otherwise), and it writes color to
// the framebuffer. It never writes depth or stencil (both masked off), never
// changes the framebuffer binding, and restores every matrix stack and attrib
// it touches. Whether that is actually true is not asserted here — it is
// MEASURED, per frame, by the fingerprint diff in gl_state_fingerprint.h.
// ============================================================================
#pragma once

namespace GlProbe {

// What the probe learned. Written only on the game thread (from onDraw), read
// by the log line and by the MXBMRP3_Test_GlProbe* hooks.
struct Status {
    bool ran = false;              // onDraw() did something at least once
    bool moduleResident = false;   // opengl32.dll was already loaded in-process
    bool entryPointsOk = false;    // the GL 1.1 entry points resolved
    bool contextCurrent = false;   // wglGetCurrentContext() on the Draw thread
    int  glVersion = 0;            // x10: 46 = GL 4.6; 0 = unknown
    bool compatProfile = false;    // fixed function (glBegin) usable
    int  drawFramebuffer = -1;     // 0 = default framebuffer; -1 = not queried
    int  readFramebuffer = -1;
    int  viewport[4] = { 0, 0, 0, 0 };
    bool drew = false;             // a probe quad was submitted
    bool readbackMatched = false;  // the drawn pixel read back as the probe color
    int  stateDiffs = -1;          // differing state values after restore; -1 = not measured
    int  glErrors = 0;             // GL errors raised across the probe's own draw
    // Did the Phase 1 measurement load actually PAINT? -1 not checked, 0 no,
    // 1 yes. This is the GL side's engagement check: the engine rows have
    // quadsHandedOver, and without an equivalent a load that silently drew
    // nothing measures as zero cost and passes a performance bar perfectly.
    // Only meaningful at glProbeQuads >= ~32, where the alpha-blended overlap
    // saturates enough to read back.
    int  loadPainted = -1;
    int  drawGaps = 0;             // times Draw resumed after a >=250ms gap
    int  lastGapMs = 0;            // length of the most recent such gap
};

// Per-frame entry point. Cheap no-op (one relaxed atomic load) when glProbe=0.
// Call ONLY from the Draw callback thread — see the threading note above.
// `iState` is the Draw callback's own state argument (0 on track, 1 spectate,
// 2 replay), used by the cadence heartbeat.
void onDraw(int iState);

// The engine-drawn reference bar's rect in normalized HUD coords, for
// HudManager::produceFrame. Read from here rather than duplicated there so the
// GL bar and the engine bar cannot drift apart — a drift would look exactly
// like the coordinate-mapping disagreement the pair exists to detect, which is
// the one confusion this test must not be capable of.
void referenceBarRect(float& x0, float& y0, float& x1, float& y1);

// A copy of the current status. Safe to call from the same thread that calls
// onDraw(); that is how the test harness reads it.
Status status();

}  // namespace GlProbe
