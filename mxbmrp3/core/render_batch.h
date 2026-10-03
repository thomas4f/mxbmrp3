// ============================================================================
// core/render_batch.h
// The API-AGNOSTIC half of turning a hudsw::Frame into a triangle stream:
// normalized HUD coords -> NDC through the UiViewport rect, corner order, UV
// basis, sprite/font index resolution, text layout and justify, and coalescing
// consecutive primitives into runs by texture. Everything a GPU backend does
// BEFORE it touches an API.
//
// WHY IT EXISTS. Extracted from hud_gpu_renderer.cpp (D3D11) so the GL backend
// consumes the same batches rather than growing a second copy of the same
// arithmetic. This repo has made exactly this move before, for exactly this
// reason: render_asset_decode was pulled out of hud_sw_renderer so both
// backends share the decoders and the text-layout math -- parity by
// construction rather than by vigilance. Two batchers would have to be kept in
// step by review, forever, across every future change to either backend.
//
// It also makes the batcher TESTABLE, which it was not on either side. The D3D
// backend's header states its own coverage gap out loud: its batching "needs a
// real device AND a display, and headless CI has neither". That is true of the
// D3D-specific parts; it was never true of the geometry, and the geometry is
// most of it. tests/unit/test_render_batch.cpp now pins it with fake handles.
//
// WHAT MAKES IT SHAREABLE, and it is worth stating because it looks like it
// should not be: BOTH the NDC mapping and the UVs are used VERBATIM by both
// APIs, with no per-backend branch anywhere in this file.
//
//  - NDC: D3D and GL disagree about the depth range, but both put clip space
//    y = +1 at the TOP of the viewport, so `1 - py*2/h` is correct for each.
//  - UVs: the famous "GL textures are upside down" does NOT apply. Both APIs
//    map the FIRST ROW of pixel data to v=0 and merely disagree about what to
//    call that edge (GL says lower-left, D3D upper-left). The v values below
//    come from atlas ROW INDICES counted from the first row, so identical UVs
//    address identical texels under either API and NO backend should flip on
//    upload.
//
// That second point is stated this firmly because an earlier version of this
// comment said the opposite - that a GL backend "uploads flipped" - and the GL
// backend duly did. The flip was invisible for sprites, whose UVs span the
// whole texture so a flip and a convention swap cancel, and it silently
// corrupted every glyph, whose UVs are sub-rectangles that a flip relocates.
// Pinned by the text case in tests/integration/tests/gl_render_test.cpp; the
// quad cases could not catch it, because they draw through the 1x1 white
// texture, which is its own mirror image.
//
// Handles are opaque `const void*` rather than a template parameter: the only
// genuinely API-specific things in the loop are "which texture" and "which
// shader", and an untyped handle keeps this header free of both D3D and GL
// types without paying for a template instantiation per backend.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hud_sw_renderer.h"     // hudsw::Frame -- the shared frame input
#include "render_asset_decode.h" // hudassets::FntFont, layoutFnt, measureFnt

namespace hudbatch {

// Matches the D3D input layout element-for-element (POS float2, UV float2,
// COL R8G8B8A8_UNORM); rgba carries the game's ABGR little-endian packing
// straight through, exactly as m_ulColor arrives.
struct Vertex {
    float x, y;      // NDC
    float u, v;
    uint32_t rgba;
};

// Which pixel program a run needs. Sprite = texel x color (the game's texture
// stage); Text = the A8 glyph coverage path.
enum class Shader { Sprite, Text };

struct Run {
    const void* tex;      // opaque backend texture handle
    Shader shader;
    uint32_t start, count;
};

// Resolves names to backend textures. Returning nullptr SKIPS the primitive,
// which is the existing behaviour for a decode miss - a frame with one bad
// asset still draws everything else.
struct Resolver {
    virtual ~Resolver() = default;
    virtual const void* texture(const std::string& base, bool icon,
                                const std::string& root) = 0;
    // Handle plus the decoded metrics. nullptr handle => skip the string.
    virtual const void* font(const std::string& base, const std::string& root,
                             const hudassets::FntFont** outFont) = 0;
    // The 1x1 white texture that lets untextured quads go through the same
    // texel x color shader as everything else.
    virtual const void* white() = 0;
};

// Per-SPRITE-INDEX cache of resolved texture handles, owned by a backend and
// passed to build(). Without it every textured quad pays Resolver::texture -
// a virtual call plus a std::map<std::string> find in both GPU backends, which
// for the GL backend is on the GAME thread every frame (~75-100ns per quad
// measured with a realistic ~400-name table: 11-40us per frame for 150-400
// textured quads, against ~2ns for the slot read).
//
// WHAT IS NEVER CACHED: a nullptr answer. A null is either a permanent decode
// miss (the backend's own map records that, and answers it cheaply) or the GL
// backend's per-frame decode budget DEFERRING the sprite to a later frame - and
// caching the deferral would blank that sprite for the session. So only a
// resolved handle fills a slot; a null is asked again next frame.
//
// INVALIDATION. Slots are keyed implicitly on the frame's name table, so the
// cache re-keys itself (all slots empty) whenever the table OBJECT, its size,
// firstIcon or assetRoot differs from the last build - each of which changes
// what an index resolves to. What it cannot see is the backend's own texture
// cache being dropped (an art reload, a lost GL context), so a backend MUST
// call invalidate() at every site that clears or deletes its textures, or a
// slot serves a dead handle. It also assumes a table is not rewritten in place
// at the same size, which holds for both producers: HudManager rebuilds
// m_spriteBases only in setupDefaultResources()/clear() (the latter destroys
// the GL renderer with it), and CompanionWindow's tables are append-only.
// Pinned by tests/unit/test_render_batch.cpp ("texture cache: ...").
class TextureCache {
public:
    void invalidate() { m_names = nullptr; }

private:
    friend void build(const hudsw::Frame&, int, int, float, float, float, float,
                      Resolver&, std::vector<Vertex>&, std::vector<Run>&, TextureCache*);
    // Re-key to `frame` if it is not the table the slots describe. Allocates
    // only on a re-key, never in the steady state.
    void sync(const hudsw::Frame& frame);

    const std::vector<std::string>* m_names = nullptr;
    size_t m_size = 0;
    int m_firstIcon = 0;
    std::string m_root;
    std::vector<const void*> m_slots;   // index = sprite - 1; nullptr = unresolved
};

// Build `frame` into `verts`/`runs` (both cleared first). w,h are the client
// size in pixels; vx,vy,vw,vh are the UiViewport::compute rect the normalized
// coordinates map through. `cache` is optional; null resolves every quad.
void build(const hudsw::Frame& frame, int w, int h,
           float vx, float vy, float vw, float vh,
           Resolver& res, std::vector<Vertex>& verts, std::vector<Run>& runs,
           TextureCache* cache = nullptr);

}  // namespace hudbatch
