// ============================================================================
// core/hud_gl_renderer.h
// OpenGL backend for the plugin's render primitives, drawn INSIDE the game's
// own GL context on the Draw callback thread - no window, no swapchain, no
// compositor. It consumes the same hudsw::Frame as the software and D3D11
// backends and batches it through the shared core/render_batch.h, so all three
// agree by construction rather than by vigilance.
//
// WHY IT EXISTS: see plans/gl_in_context_renderer.md. The short version is that
// every problem the window-based overlay renderer fought - DWM promoting the
// game off the composition path, z-order, taskbar, focus, capture hooks - only
// exists because the HUD is in a second window. Drawing in the game's own
// framebuffer has none of them, and it appears in OBS game capture by
// definition. Phase 1 measured the engine at ~0.94 us per quad against this
// path's ~0.007, so a heavy themed frame goes from ~5.6 ms to ~0.04 ms.
//
// ---------------------------------------------------------------------------
// IT IS GL 1.1. DELIBERATELY. This is the most important decision in the file.
// ---------------------------------------------------------------------------
// Every entry point used to DRAW is a real export of opengl32.dll, resolved
// with GetProcAddress: no VBOs, no VAOs, no shaders.
//
// The exceptions all have the same shape, and it is worth stating because it is
// the rule that lets the next one in: a name comes through wglGetProcAddress
// only when it is CONDITIONAL ON A STATE THAT IMPLIES ITS OWN AVAILABILITY.
// glUseProgram and glBindBuffer/glBindVertexArray are reached only after
// finding something bound, which a context lacking them cannot have; and
// glActiveTexture/glClientActiveTexture only on a context that reports >= 1.3,
// where they are core. All three are NEUTRALISERS - state the game may have
// left that our fixed-function draw must undo - never features we wanted.
//
// That is not nostalgia, it is what the measurement licenses. Phase 1 showed
// the win comes from BATCHING - one draw call per texture run instead of
// thousands of engine primitives - not from any modern API feature. Client-side
// vertex arrays already cleared the bar with room to spare. And wglGetProcAddress
// is the genuinely risky dependency in GL: it is per-pixel-format and
// per-driver, so a pointer that resolves on an RX 6900 XT says nothing about
// some old Intel iGPU, and on several drivers it signals failure by returning
// 1, 2, 3 or -1 rather than null. Taking the compatibility instead of the
// fancier path costs us nothing we measured and removes that whole class.
//
// The two pixel programs the D3D backend needs shaders for both fall out of
// fixed-function GL_MODULATE, which is why no shader path is required:
//   - sprite (texel x quad colour):  an RGBA texture. MODULATE gives
//     C = Cf*Ct, A = Af*At - exactly psSprite.
//   - text (glyph coverage as alpha): a GL_ALPHA texture. MODULATE on an ALPHA
//     format leaves RGB as the vertex colour and multiplies only alpha, which
//     is exactly psText's float4(col.rgb, coverage * col.a).
// Untextured quads go through a 1x1 white texture, as they do in the batcher,
// so one texture stage serves all three.
//
// TEXTURES ARE UPLOADED UNFLIPPED, which is worth saying because "GL textures
// are upside down" is the reflex and it is wrong here. Both APIs map the FIRST
// ROW of pixel data to v=0 and merely disagree about what to call that edge;
// the shared batcher derives v from atlas ROW INDICES counted from the first
// row, so identical UVs address identical texels under either API. Flipping
// would be invisible for sprites (whose UVs span the whole texture) and would
// silently corrupt glyphs, whose UVs are sub-rectangles.
//
// STATE, IN BOTH DIRECTIONS - and the second direction is the one that shipped
// a bug. Leaking state OUT corrupts the game's next draw, so every bit this
// touches is saved and restored and the restore is verified, not assumed
// (Phase 0: 51 sampled values identical before and after, zero GL errors).
// But state left by the GAME flows IN just as freely, and fixed-function GL has
// a long list of it that silently changes what our calls mean - which texture
// unit they address, whether our UVs are even used, whether blending happens at
// all. None of it raises a GL error, so none of it reaches the fallback: the
// HUD simply comes out wrong. The rule the .cpp now follows is that fixed-
// function state we DEPEND on is state we SET, enumerated from the fragment
// pipeline rather than from the ones that have bitten us. It shipped without
// that and a tester's glyphs and icons came back as solid blocks.
//
// COVERAGE: unusually good for a GPU backend in this repo, because the harness
// can create a real GL context under Wine+Xvfb and read pixels back - so
// gl_render_test.cpp asserts actual rendered output (colour, position, z-order,
// text, and the state restore). That is coverage the D3D backend has never had.
// What it does NOT cover is other drivers; hence the conservative API choice
// and the resolve-everything-up-front contract below.
// ============================================================================
#pragma once

#include <string>

#include "hud_sw_renderer.h"   // hudsw::Frame - the shared frame input

namespace hudgl {

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Resolve opengl32 and every entry point, once. False means the GL backend
    // is unavailable and the caller must keep using engine rendering; the
    // reason is in lastError() and is logged once, naming what was missing, so
    // a field report from a machine unlike the author's is diagnosable from the
    // log alone. Same contract hud_gpu_renderer honours for d3d11.
    //
    // Requires a current GL context (for the version/vendor query), so call it
    // from the Draw callback thread.
    bool init();
    bool ok() const;
    const std::string& lastError() const;

    // Draw `frame` into the CURRENT context and framebuffer at client size w,h,
    // with normalized coords mapping through the (vx,vy,vw,vh) UiViewport rect -
    // the same rect the other backends use, and the one Phase 0 proved the
    // engine itself uses. Returns false on any GL error, which the caller must
    // treat as latch-off-and-fall-back: a renderer that half-draws into someone
    // else's context is worse than one that does not draw.
    bool render(const hudsw::Frame& frame, int w, int h,
                float vx, float vy, float vw, float vh);

    // Same live-reload contract as the other backends: drop uploaded textures
    // so the next frame re-reads them from disk; fonts are kept, because nobody
    // iterates on a .fnt.
    //
    // REQUEST, not an immediate drop, and for the same reason CompanionWindow
    // uses the same shape: the caller is the RELOAD_CONFIG
    // path, which runs on the plugin worker when [Advanced] pluginThread is on,
    // while the cache is read by render() on the game thread. The flag is
    // consumed at the top of the next render(), which is both race-free and the
    // only place where the GL context is current - and therefore the only place
    // the old texture names can actually be DELETED rather than abandoned.
    void requestArtReload();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace hudgl
