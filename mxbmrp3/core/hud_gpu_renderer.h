// ============================================================================
// core/hud_gpu_renderer.h
// GPU (D3D11) backend for the plugin's own render primitives — the same
// hudsw::Frame the software rasterizer consumes, drawn as one batched stream of
// textured triangles on the caller's (window) thread with its OWN device, so it
// costs the game's renderer nothing. What it buys over hud_sw_renderer:
// milliseconds-to-microseconds fill cost (the companion was measured fill-rate
// bound: ~3.2 ms/frame at 1080p for a heavy themed frame, and a theme
// multiplies quads 27x), 4x MSAA edge antialiasing on colored quads (the map
// ribbon, needles), and bilinear sprite/glyph sampling.
//
// WHY BESPOKE (evaluated before writing, per CLAUDE.md "reach for standard
// tooling"): Direct2D cannot modulate a bitmap by a per-draw color without an
// effect graph, and texel × quad-color — including alpha — is exactly the
// game's texture stage that both backends must reproduce (white icons take the
// quad color; quad alpha fades textures). DirectXTK's SpriteBatch has the right
// semantics but is a vendored-library-sized dependency that must also survive
// the mingw cross-build. The whole need is: one shader computing texel×color,
// a dynamic vertex buffer, and run-batching by texture — a few hundred lines.
//
// Everything loads dynamically (LoadLibrary d3d11.dll / d3dcompiler_47.dll), so
// the plugin gains NO import the host could fail to resolve; init() returning
// false (or any later failure) means the caller stays on the software path —
// the backends are interchangeable per frame because they share the Frame
// input, the asset decoders and the text layout (render_asset_decode.h).
//
// ONE present target: a swapchain for the companion window. Render into a 4x
// MSAA target, resolve into the backbuffer, Present (vsync or immediate; the
// caller paces).
//
// Do NOT present a HUD through UpdateLayeredWindow: its win32k locking
// serializes the game thread (plugin game-thread cost 0.09 ms -> 1.6 ms avg
// under a ~400 fps game).
//
// Rendered over a transparent clear with (SRC_ALPHA, INV_SRC_ALPHA /
// ONE, INV_SRC_ALPHA) blending, the target accumulates premultiplied RGBA -
// the same argument as hudsw's blend().
//
// COVERAGE. The shared pieces — the decoders and the text-layout math — are
// pinned through the software renderer's golden-frame tests
// (render_asset_decode.h). The BATCHING lives in core/render_batch.h (shared
// with the GL backend); tests/unit/test_render_batch.cpp pins the run
// coalescing, triangle order, justify offsets and NDC mapping.
//
// Not unit-covered: device creation, the present path, and the
// resolver/input-layout wiring below. Headless coverage IS possible: under
// Xvfb, Wine's d3d11.dll creates a D3D_DRIVER_TYPE_HARDWARE device at feature
// level 11_0 and its d3dcompiler_47 compiles vs_4_0, both returning S_OK (in
// the project's own Wine prefix). No new harness is needed:
// tools/hud_window/companion_demo.sh opens the real companion window headless
// and screenshots it, and the companion is this file's only caller - so its
// picture IS this file's output. The differential recipe for a refactor of
// this file:
//
//   ./tools/hud_window/companion_demo.sh before.png 2   # at the old commit
//   ./tools/hud_window/companion_demo.sh after.png  2   # at the new one
//   compare -metric AE before.png after.png null:       # expect 0
//
// and CONTROL it, because a pixel test that cannot see a difference proves
// nothing - if both runs silently fell back to software, the diff is 0 for the
// wrong reason:
//
//   EXTRA_INI=$'[Advanced]\nhwAccel=0' companion_demo.sh sw.png 2
//   compare -metric AE sw.png after.png null:           # expect a LARGE number
//
// Be precise about what it would buy: this is Wine's D3D11 over llvmpipe, not
// Microsoft's, so exact pixels may differ from Windows and a golden image would
// pin WINE's output rather than validate Windows'. It is weak for absolute
// correctness and exactly right for DIFFERENTIAL testing — same inputs, same
// stack, before versus after a refactor. Until it exists, manual in-game
// testing remains the gate for this code; any failure falls back to the
// software path, which is covered.
// ============================================================================
#pragma once

#include "hud_sw_renderer.h"   // hudsw::Frame (the shared frame input)

namespace hudgpu {

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Create device + swapchain for `hwnd`. False => the caller uses the
    // software path; an init failure latches this instance dead and every
    // further method is a no-op returning false.
    bool init(void* hwnd);
    bool ok() const;

    // Draw `frame` at client size w,h; normalized coords map through the
    // (vx,vy,vw,vh) scale viewport (the same UiViewport::compute rect the
    // software path uses): clear to the opaque bg color, then Present
    // (vsync ? interval 1 : immediate).
    // False on any device error - the caller should fall back to software.
    bool renderSwapchain(const hudsw::Frame& frame, int w, int h,
                         float vx, float vy, float vw, float vh,
                         uint8_t bgR, uint8_t bgG, uint8_t bgB, bool vsync);

    // Same live-reload contract as hudsw::Renderer::dropTextureCache: drop
    // decoded .tga SRVs so the next frame re-reads them; fonts kept.
    void dropTextureCache();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace hudgpu
