// ============================================================================
// core/test_gl_render_probe.h
// Test-only: drive the in-context GL backend against the current context and
// read one pixel back. Split out of core/test_hooks.cpp, which is a registry of
// thin exports and was at its size budget - the budget's rule is that an
// oversized file wants a split rather than a bigger number.
//
// The EXPORT still lives in test_hooks.cpp (check_test_hook_placement.sh
// requires it, and that placement is what keeps test hooks out of a shipping
// DLL); only the body is here. Both fences still apply: the whole file is gated
// on MXBMRP3_TEST_BUILD, and mxbmrp3/CMakeLists.txt removes it from every
// shipping target's source list alongside test_hooks.cpp.
// ============================================================================
#pragma once

#if defined(MXBMRP3_TEST_BUILD)

namespace mxbtest {

// Render a synthetic frame at client size w,h through hudgl::Renderer, then
// read the pixel at (px, py) in TOP-DOWN coordinates - the way the HUD names
// positions - and return it as 0xRRGGBBAA. -1 if the backend could not render.
//
// scenario: 0 = one opaque red quad over the left half (position and colour);
//           1 = red then blue, both full-screen (z-order: blue must win);
//           2 = a string (the GL_ALPHA/MODULATE text path);
//           3 = a NESTED PACK sprite (path resolution for themes/gamepads/
//               pitboards/gauges, which resolve relative to the asset root
//               rather than under /textures/ or /icons/).
int glRenderProbe(int w, int h, int px, int py, int scenario);

// The asset name the in-context GL frame carries at `index`, as the RENDERER
// sees it - written to `out`, returning its length, or -1.
//
// This is the one instrument for a class of bug no pixel assertion can reach.
// The frame's tables must hold RENDER names (resolved against the backend's own
// asset root), not the full paths the game's DrawInit is handed. Hand over the
// wrong shape and every lookup misses, the batcher drops the primitive, and
// NOTHING raises an error - the HUD loses all text and every textured sprite
// while untextured fills keep drawing perfectly. Read through the same builder
// renderInContextGl uses, so a rewire is caught here rather than by a human
// noticing a missing glyph.
//
// kind: 0 = fonts, 1 = sprites.
int glFrameAssetName(int kind, int index, char* out, int cap);
int glFrameAssetCount(int kind);

}  // namespace mxbtest

#endif
