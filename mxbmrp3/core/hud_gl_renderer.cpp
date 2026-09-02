// ============================================================================
// core/hud_gl_renderer.cpp
// Implementation of the in-context GL backend - see hud_gl_renderer.h for the
// design and, in particular, for why this is GL 1.1 only.
// ============================================================================
#include "hud_gl_renderer.h"

#if defined(_WIN32)

#include "render_batch.h"
#include "render_asset_decode.h"
#include "gl_state_fingerprint.h"   // glprobe::parseVersion
#include "../diagnostics/logger.h"

#include <windows.h>
#include <cstddef>
#include <atomic>
#include <map>
#include <vector>

namespace hudgl {
namespace {

// GL types and constants, spelled out rather than #included, for the same two
// reasons as core/gl_probe.cpp: this must not create an opengl32 import (the
// whole fallback design depends on the loader having nothing to fail on, and
// check_lazy_module_imports.sh enforces it), and <GL/gl.h> differs between the
// MSVC and mingw toolchains this builds under.
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned int GLuint;

constexpr GLenum GL_NO_ERROR_ = 0;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_POLYGON_STIPPLE = 0x0B42;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_FOG = 0x0B60;
constexpr GLenum GL_LIGHTING = 0x0B50;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_VIEWPORT = 0x0BA2;
constexpr GLenum GL_ATTRIB_STACK_DEPTH = 0x0BB0;
constexpr GLenum GL_CLIENT_ATTRIB_STACK_DEPTH = 0x0BB1;
constexpr GLenum GL_ALPHA_TEST = 0x0BC0;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_COLOR_LOGIC_OP = 0x0BF2;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_TEXTURE_GEN_S = 0x0C60;
constexpr GLenum GL_TEXTURE_GEN_T = 0x0C61;
constexpr GLenum GL_TEXTURE_GEN_R = 0x0C62;
constexpr GLenum GL_TEXTURE_GEN_Q = 0x0C63;
constexpr GLenum GL_POLYGON_MODE = 0x0B40;
constexpr GLenum GL_MAX_CLIP_PLANES = 0x0D32;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_CLIP_PLANE0 = 0x3000;
constexpr GLenum GL_FRONT_AND_BACK = 0x0408;
constexpr GLenum GL_FILL = 0x1B02;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_MODELVIEW = 0x1700;
constexpr GLenum GL_PROJECTION = 0x1701;
constexpr GLenum GL_TEXTURE = 0x1702;
constexpr GLenum GL_ALPHA = 0x1906;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_MODULATE = 0x2100;
constexpr GLenum GL_TEXTURE_ENV_MODE = 0x2200;
constexpr GLenum GL_TEXTURE_ENV = 0x2300;
constexpr GLenum GL_CLAMP = 0x2900;                  // GL 1.0
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;          // GL 1.2 - GATED
constexpr GLenum GL_SAMPLE_BUFFERS = 0x80A8;            // GL 1.3 - GATED
constexpr GLenum GL_SAMPLES = 0x80A9;                   // GL 1.3 - GATED
constexpr GLenum GL_MULTISAMPLE = 0x809D;               // GL 1.3 - GATED
constexpr GLenum GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E;  // GL 1.3 - GATED
constexpr GLenum GL_SAMPLE_ALPHA_TO_ONE = 0x809F;      // GL 1.3 - GATED
constexpr GLenum GL_SAMPLE_COVERAGE = 0x80A0;          // GL 1.3 - GATED
constexpr GLenum GL_TEXTURE0 = 0x84C0;                 // GL 1.3 - GATED
constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;           // GL 1.3 - GATED
constexpr GLenum GL_CLIENT_ACTIVE_TEXTURE = 0x84E1;    // GL 1.3 - GATED
constexpr GLenum GL_MAX_TEXTURE_UNITS = 0x84E2;        // GL 1.3 - GATED
constexpr GLenum GL_VERTEX_ARRAY = 0x8074;
constexpr GLenum GL_NORMAL_ARRAY = 0x8075;
constexpr GLenum GL_COLOR_ARRAY = 0x8076;
constexpr GLenum GL_TEXTURE_COORD_ARRAY = 0x8078;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;           // GL 1.5 - protected by DATA
                                                     // FLOW, not a version test:
                                                     // every use sits behind a
                                                     // non-zero prevArray, which
                                                     // only the >=15 gated query
                                                     // can produce. Do not "fix"
                                                     // by moving a use out from
                                                     // behind that check.
constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;   // GL 1.5 - GATED
constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x85B5;   // GL 3.0 - GATED
constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D;        // GL 2.0 - GATED
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLbitfield GL_ALL_ATTRIB_BITS = 0x000FFFFF;
constexpr GLbitfield GL_CLIENT_ALL_ATTRIB_BITS = 0xFFFFFFFFu;

// Every one of these is a GL 1.1 export of opengl32.dll. If that ever stops
// being true for a name added here, it silently becomes a wglGetProcAddress
// dependency with all the per-driver variance the header rejects - so keep the
// list to 1.1 and let init() fail loudly rather than reaching for the extension
// mechanism.
struct Gl {
    HGLRC (WINAPI* wglGetCurrentContext)() = nullptr;
    const GLubyte* (WINAPI* GetString)(GLenum) = nullptr;
    void (WINAPI* GetIntegerv)(GLenum, GLint*) = nullptr;
    GLenum (WINAPI* GetError)() = nullptr;
    void (WINAPI* PushAttrib)(GLbitfield) = nullptr;
    void (WINAPI* PopAttrib)() = nullptr;
    void (WINAPI* PushClientAttrib)(GLbitfield) = nullptr;
    void (WINAPI* PopClientAttrib)() = nullptr;
    void (WINAPI* MatrixMode)(GLenum) = nullptr;
    void (WINAPI* PushMatrix)() = nullptr;
    void (WINAPI* PopMatrix)() = nullptr;
    void (WINAPI* LoadIdentity)() = nullptr;
    void (WINAPI* Enable)(GLenum) = nullptr;
    void (WINAPI* Disable)(GLenum) = nullptr;
    void (WINAPI* DepthMask)(GLboolean) = nullptr;
    void (WINAPI* StencilMask)(GLuint) = nullptr;
    void (WINAPI* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;
    void (WINAPI* BlendFunc)(GLenum, GLenum) = nullptr;
    void (WINAPI* PolygonMode)(GLenum, GLenum) = nullptr;
    void (WINAPI* PixelStorei)(GLenum, GLint) = nullptr;
    void (WINAPI* TexEnvi)(GLenum, GLenum, GLint) = nullptr;
    void (WINAPI* GenTextures)(GLsizei, GLuint*) = nullptr;
    void (WINAPI* DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (WINAPI* BindTexture)(GLenum, GLuint) = nullptr;
    void (WINAPI* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                              GLenum, GLenum, const void*) = nullptr;
    void (WINAPI* TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (WINAPI* EnableClientState)(GLenum) = nullptr;
    void (WINAPI* DisableClientState)(GLenum) = nullptr;
    void (WINAPI* VertexPointer)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* TexCoordPointer)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* ColorPointer)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (WINAPI* Color4f)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
};

// The batcher reads Vertex by field; we hand GL the same memory by BYTE OFFSET
// and stride, exactly as the D3D input layout does. Same shared struct, same
// hazard, so the same compile-time contract: a field added to hudbatch::Vertex
// for one backend's benefit would silently shift the other's pointers.
static_assert(sizeof(hudbatch::Vertex) == 20, "GL array stride");
static_assert(offsetof(hudbatch::Vertex, x) == 0, "position at byte 0");
static_assert(offsetof(hudbatch::Vertex, u) == 8, "texcoord at byte 8");
static_assert(offsetof(hudbatch::Vertex, rgba) == 16, "color at byte 16");

}  // namespace

struct Renderer::Impl {
    HMODULE lib = nullptr;
    Gl gl;
    bool ready = false;
    bool warnedProgram = false;   // the bound-program note is one-shot
    std::string error;
    // GL version x10 (46 = 4.6). Several tokens below are NEWER than the 1.1
    // this backend targets, and querying an unsupported one raises
    // GL_INVALID_ENUM - which the error check at the end of render() would turn
    // into a permanent latch-off, on exactly the old hardware the GL 1.1 choice
    // exists to serve. So every such token is gated on this.
    int version = 0;

    GLuint white = 0;
    struct Tex { GLuint id = 0; bool ok = false; };
    std::map<std::string, Tex> texs;                 // requestArtReload clears
    struct Font { GLuint id = 0; hudassets::FntFont font; };
    std::map<std::string, Font> fonts;               // kept; nobody iterates a .fnt

    std::vector<hudbatch::Vertex> verts;
    std::vector<hudbatch::Run> runs;
    // Set off-thread by requestArtReload, consumed on the game thread in
    // render(). std::atomic because the two really are different threads in
    // pluginThread mode.
    std::atomic<bool> artReload{ false };
    // The context every cached GL name below belongs to. A resolution change
    // (and windowed<->fullscreen) DESTROYS the game's GL context and makes a new
    // one, which silently invalidates every texture we uploaded - see the
    // abandon path in render().
    HGLRC ctx = nullptr;
    bool warnedStaleErrors = false;
    bool loggedIncoming = false;  // the one-shot "what the game handed us" line

    // MULTITEXTURE, the one 1.1-plus dependency this backend cannot decline.
    // Everything else newer than 1.1 in here is something we might WANT; these
    // two are something we must NEUTRALISE, because the game may have left the
    // texture units somewhere other than where our calls assume. Resolved once
    // in init() and only on a context that reports >= 1.3, where they exist by
    // definition - the same "conditional on a state that implies its own
    // availability" argument the buffer bindings below rely on.
    void (WINAPI* activeTexture)(GLenum) = nullptr;
    void (WINAPI* clientActiveTexture)(GLenum) = nullptr;
    int texUnits = 1;     // GL_MAX_TEXTURE_UNITS, clamped; 1 before GL 1.3
    int clipPlanes = 0;   // GL_MAX_CLIP_PLANES, clamped
    // Decodes allowed in ONE frame. Loading every sprite and font at once is a
    // visible hitch on the game thread: the first glInGame frame, every art
    // reload, and - since abandoning the caches is what a context change forces
    // - every RESOLUTION CHANGE. Spreading the work costs a few frames in which
    // a not-yet-loaded sprite is skipped (invisible at 460 fps) and removes the
    // stall entirely. The other backends decode on their own window threads;
    // this one cannot, because a GL context belongs to the thread that has it.
    static constexpr int kMaxDecodesPerFrame = 4;
    int decodesThisFrame = 0;

    GLuint upload(const uint8_t* px, int w, int h, bool alphaOnly);
    GLuint uploadMipped(const std::vector<hudassets::MipLevel>& mips, bool alphaOnly);
    Tex* texture(const std::string& base, bool icon, const std::string& root);
    Font* font(const std::string& base, const std::string& root);
};

// Upload one image, ROWS AS THEY ARE. No vertical flip - and that is worth
// stating, because "GL textures are upside down" is the reflex and it is wrong
// here.
//
// Both APIs map the FIRST ROW OF PIXEL DATA to v=0; they only disagree about
// what to call that edge (GL says lower-left, D3D says upper-left). The shared
// batcher derives its v from atlas ROW INDICES counted from the first row, so
// the same UVs address the same texels under either API and no flip is
// correct for either.
//
// A flip is invisible for sprites (their UVs span the whole texture, so a flip
// and a v-convention swap cancel) and BREAKS GLYPHS, whose UVs are
// sub-rectangles: flipping the atlas moves every glyph's rect to the wrong rows
// and the text path renders nothing recognisable. gl_render_test's text case
// pins it - the quad cases cannot, since they draw through the 1x1 white
// texture, which is its own mirror image.
GLuint Renderer::Impl::upload(const uint8_t* px, int w, int h, bool alphaOnly) {
    GLuint id = 0;
    gl.GenTextures(1, &id);
    if (!id) return 0;
    gl.BindTexture(GL_TEXTURE_2D, id);
    // Row alignment 1: an odd-width GL_ALPHA glyph atlas is not 4-byte aligned,
    // and the default of 4 would shear it. Restored by the client-attrib pop -
    // which is only true because render() does its build, and therefore every
    // call that lands here, INSIDE the save window.
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum fmt = alphaOnly ? GL_ALPHA : GL_RGBA;
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fmt), w, h, 0,
                  fmt, GL_UNSIGNED_BYTE, px);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp, not repeat: a quad sampling slightly past 1.0 must not wrap around
    // and show the opposite edge of an atlas.
    // CLAMP_TO_EDGE is GL 1.2. On anything older, plain CLAMP - which samples
    // the border colour at the very edge rather than the last texel, a
    // difference of at most one texel row and vastly better than the
    // GL_INVALID_ENUM the newer token would raise there.
    const GLenum wrap = (version >= 12) ? GL_CLAMP_TO_EDGE : GL_CLAMP;
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    return id;
}

// Upload a whole mip chain and sample it trilinearly. ONE function for both the
// glyph atlas and icons, which is the point of hudassets::MipLevel existing once
// rather than twice: the two differ only in pixel format, and as two functions
// that difference is 20 duplicated lines kept in step by hand.
//
// WHY EITHER OF THEM IS MIPPED. These are the textures this backend always
// MINIFIES. The shipped .fnt cell is 135px so scaled-up widgets stay crisp, and
// icon source art is likewise far larger than the ~20px it draws at - so one
// bilinear tap reads 4 texels of a ~49-texel footprint and drops exactly the
// partial-coverage texels at every edge. That is not a blur, it is strokes
// thinning and breaking up: text that is not smooth beside the game's own
// renderer drawing the same .fnt. The game mips that atlas, and fontgen's 20px
// inter-glyph padding exists precisely so mipping it cannot bleed neighbours in.
//
// The chain is uploaded COMPLETE, to 1x1. GL_TEXTURE_MAX_LEVEL is GL 1.2, so on
// the 1.1 floor this backend targets there is no way to declare a partial chain,
// and a texture whose chain stops early is INCOMPLETE - it samples as white,
// silently, which is the failure mode this file exists to avoid.
GLuint Renderer::Impl::uploadMipped(const std::vector<hudassets::MipLevel>& mips,
                                    bool alphaOnly) {
    if (mips.empty()) return 0;
    GLuint id = 0;
    gl.GenTextures(1, &id);
    if (!id) return 0;
    gl.BindTexture(GL_TEXTURE_2D, id);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);   // see upload(): odd-width levels
    const GLenum fmt = alphaOnly ? GL_ALPHA : GL_RGBA;
    for (size_t i = 0; i < mips.size(); ++i) {
        const hudassets::MipLevel& m = mips[i];
        gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), static_cast<GLint>(fmt),
                      m.w, m.h, 0, fmt, GL_UNSIGNED_BYTE, m.px.data());
    }
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                     static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    const GLenum wrap = (version >= 12) ? GL_CLAMP_TO_EDGE : GL_CLAMP;
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    return id;
}

Renderer::Impl::Tex* Renderer::Impl::texture(const std::string& base, bool icon,
                                             const std::string& root) {
    auto it = texs.find(base);
    if (it != texs.end()) return it->second.ok ? &it->second : nullptr;
    // Over budget: draw nothing for this sprite THIS frame and try again next.
    // No cache entry is created, because an entry here is a permanent record of
    // a miss - the deferral must not be mistaken for "this file is not loadable".
    if (decodesThisFrame >= kMaxDecodesPerFrame) return nullptr;
    ++decodesThisFrame;
    Tex& t = texs[base];
    // hudassets::spritePath, NOT a hand-rolled join. It is renderName's inverse
    // and it has a case a local join misses: a NESTED pack asset (themes,
    // gamepads, pitboards, gauges) keeps a relative path in its render name -
    // "gamepads/xbox/frame" - and resolves against the root directly, where a
    // flat asset gets /textures/ or /icons/. Re-deriving the flat half alone
    // makes every pack-art sprite silently fail to load while icons and flat
    // textures work, which reads as "some art is missing" rather than as a path
    // bug. One implementation, shared with the software renderer, is why that
    // cannot diverge.
    hudassets::Texture img = hudassets::decodeTga(
        hudsw::readFile(hudassets::spritePath(root, base, icon)));
    if (!img.ok) return nullptr;                     // recorded as a miss
    // ICONS ONLY get a mip chain. An icon is minified roughly uniformly (large
    // source art, ~20px on screen), which is the case a chain answers. Nine-slice
    // panel art is stretched hard in one axis instead, where a chain picks its
    // level from the stretched derivative and blurs the sharp one - see
    // buildTexMips.
    if (icon) {
        hudassets::buildTexMips(img);
        t.id = uploadMipped(img.mips, false);
    } else {
        t.id = upload(img.rgba.data(), img.w, img.h, false);
    }
    t.ok = (t.id != 0);
    return t.ok ? &t : nullptr;
}

Renderer::Impl::Font* Renderer::Impl::font(const std::string& base,
                                           const std::string& root) {
    auto it = fonts.find(base);
    if (it != fonts.end()) return it->second.id ? &it->second : nullptr;
    if (decodesThisFrame >= kMaxDecodesPerFrame) return nullptr;   // see texture()
    ++decodesThisFrame;
    Font& f = fonts[base];
    f.font = hudassets::decodeFnt(hudsw::readFile(hudassets::fntPath(root, base)));
    if (!f.font.ok) return nullptr;
    // GL_ALPHA, so fixed-function MODULATE reproduces the D3D text shader:
    // RGB stays the vertex colour, alpha becomes coverage x colour alpha.
    // The no-chain fallback: without it a font whose chain fails to build
    // renders NO text instead of un-mipped text. See FntFont::atlas for why
    // exactly one of the two is populated.
    f.id = f.font.mips.empty() ? upload(f.font.atlas.data(), f.font.aw, f.font.ah, true)
                               : uploadMipped(f.font.mips, true);
    return f.id ? &f : nullptr;
}

Renderer::Renderer() = default;
Renderer::~Renderer() {
    // Textures are NOT deleted here. This destructor can run at DLL teardown,
    // by which point the game's context may be gone or current on another
    // thread, and glDeleteTextures without a context is undefined. Leaking a
    // few texture names into a process that is exiting anyway is the safer
    // trade - the same reasoning hud_gpu_renderer applies to its DLLs.
    delete m_impl;
}

bool Renderer::ok() const { return m_impl && m_impl->ready; }
const std::string& Renderer::lastError() const {
    static const std::string none;
    return m_impl ? m_impl->error : none;
}

bool Renderer::init() {
    if (m_impl) return m_impl->ready;
    m_impl = new Impl();
    Impl& im = *m_impl;

    // GetModuleHandle, never LoadLibrary: if opengl32 is not already resident
    // the game is not a GL app and there is nothing for this backend to join.
    im.lib = GetModuleHandleA("opengl32.dll");
    if (!im.lib) { im.error = "opengl32.dll is not loaded in this process"; return false; }

    // Resolve EVERYTHING up front and name whatever is missing. A backend that
    // discovers a null pointer mid-frame has already half-drawn into someone
    // else's context.
    std::string missing;
    auto bind = [&](void** slot, const char* name) {
        FARPROC p = GetProcAddress(im.lib, name);
        // Some drivers signal "no such function" from the EXTENSION mechanism
        // with 1/2/3/-1 rather than null. GetProcAddress itself does not, but
        // sanitising costs nothing and keeps the rule in one place if a name
        // ever moves to wglGetProcAddress.
        const uintptr_t v = reinterpret_cast<uintptr_t>(p);
        if (!p || v == 1 || v == 2 || v == 3 || v == static_cast<uintptr_t>(-1)) {
            if (!missing.empty()) missing += ", ";
            missing += name;
            return;
        }
        *slot = reinterpret_cast<void*>(p);
    };
#define B(field, name) bind(reinterpret_cast<void**>(&im.gl.field), name)
    B(wglGetCurrentContext, "wglGetCurrentContext");
    B(GetString, "glGetString");     B(GetIntegerv, "glGetIntegerv");
    B(GetError, "glGetError");       B(PushAttrib, "glPushAttrib");
    B(PopAttrib, "glPopAttrib");     B(PushClientAttrib, "glPushClientAttrib");
    B(PopClientAttrib, "glPopClientAttrib");
    B(MatrixMode, "glMatrixMode");   B(PushMatrix, "glPushMatrix");
    B(PopMatrix, "glPopMatrix");     B(LoadIdentity, "glLoadIdentity");
    B(Enable, "glEnable");           B(Disable, "glDisable");
    B(DepthMask, "glDepthMask");     B(StencilMask, "glStencilMask");
    B(ColorMask, "glColorMask");     B(BlendFunc, "glBlendFunc");
    B(PixelStorei, "glPixelStorei"); B(TexEnvi, "glTexEnvi");
    B(PolygonMode, "glPolygonMode");
    B(GenTextures, "glGenTextures"); B(DeleteTextures, "glDeleteTextures");
    B(BindTexture, "glBindTexture"); B(TexImage2D, "glTexImage2D");
    B(TexParameteri, "glTexParameteri");
    B(EnableClientState, "glEnableClientState");
    B(DisableClientState, "glDisableClientState");
    B(VertexPointer, "glVertexPointer");
    B(TexCoordPointer, "glTexCoordPointer");
    B(ColorPointer, "glColorPointer");
    B(DrawArrays, "glDrawArrays");   B(Color4f, "glColor4f");
#undef B
    if (!missing.empty()) {
        im.error = "missing opengl32 entry points: " + missing;
        return false;
    }
    im.ctx = im.gl.wglGetCurrentContext();
    if (!im.ctx) {
        im.error = "no GL context current on this thread";
        return false;
    }

    // Logged once, and deliberately verbose: a field report from a machine
    // unlike the author's has to be diagnosable from the log alone.
    auto str = [&](GLenum n) {
        const GLubyte* p = im.gl.GetString(n);
        return p ? reinterpret_cast<const char*>(p) : "(null)";
    };
    im.version = glprobe::parseVersion(str(GL_VERSION));
    DEBUG_INFO_F("hudgl: GL 1.1 backend ready - vendor='%s' renderer='%s' version='%s' (parsed %d)",
                 str(GL_VENDOR), str(GL_RENDERER), str(GL_VERSION), im.version);

    // MULTITEXTURE NEUTRALISERS. Read the long comment at the top of render()'s
    // texture-unit block for why these are not optional: without them "unit 0"
    // is an assumption about the game rather than a statement about our draw,
    // and being wrong about it costs no GL error and produces no fallback.
    //
    // wglGetProcAddress, which this file otherwise refuses - and the exception
    // holds for the same reason the glUseProgram one does: it is conditional on
    // a state that implies its own availability. A context reporting >= 1.3 HAS
    // glActiveTexture. If one somehow does not, we fail init loudly here rather
    // than draw through state we cannot control. Below 1.3 there are no units to
    // select, so skipping this is not merely safe, it is exactly correct.
    if (im.version >= 13) {
        auto gpa = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
            reinterpret_cast<void*>(GetProcAddress(im.lib, "wglGetProcAddress")));
        auto ext = [&](const char* a, const char* b) -> void* {
            if (!gpa) return nullptr;
            for (const char* n : { a, b }) {
                PROC p = gpa(n);
                const uintptr_t v = reinterpret_cast<uintptr_t>(p);
                if (p && v != 1 && v != 2 && v != 3 && v != static_cast<uintptr_t>(-1))
                    return reinterpret_cast<void*>(p);
            }
            return nullptr;
        };
        // The ARB spellings are the fallback: the pre-1.3 drivers that shipped
        // multitexture as an extension exported only those, and some ICDs kept
        // that as the only name long after 1.3 was core.
        im.activeTexture = reinterpret_cast<decltype(im.activeTexture)>(
            ext("glActiveTexture", "glActiveTextureARB"));
        im.clientActiveTexture = reinterpret_cast<decltype(im.clientActiveTexture)>(
            ext("glClientActiveTexture", "glClientActiveTextureARB"));
        if (!im.activeTexture || !im.clientActiveTexture) {
            im.error = "GL reports >= 1.3 but glActiveTexture/glClientActiveTexture "
                       "could not be resolved - declining rather than drawing through "
                       "texture units we cannot select";
            return false;
        }
        GLint units = 0;
        im.gl.GetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        // TAKE THE DRIVER'S ANSWER. The upper bound here is an absurdity guard,
        // NOT a claim about hardware. A low clamp on the reasoning that a
        // fixed-function combiner count is "4-8 on real hardware" makes a single
        // sample everyone's ceiling, and the cost it would save is two of the
        // cheapest calls GL has, per unit, against a 2.08 ms budget.
        // Underestimating is the direction that actually hurts: the loop below
        // then cannot reach a unit the game left enabled, and that unit
        // modulates our fragments with its texture.
        //
        // What the value means: GL_MAX_TEXTURE_UNITS counts FIXED-FUNCTION
        // combiners (spec minimum 2; MX Bikes hands us unit 7 on a driver
        // reporting 8). An answer past 64 means the driver replied to a
        // different question - so we walk 64 rather than its number, which still
        // covers every real fixed-function unit, and NOT 1: falling back to one
        // unit would protect nothing at all, which is the same underestimating
        // mistake in a different disguise.
        im.texUnits = units < 1 ? 1 : (units > 64 ? 64 : static_cast<int>(units));
    }
    GLint planes = 0;
    im.gl.GetIntegerv(GL_MAX_CLIP_PLANES, &planes);
    im.clipPlanes = planes < 0 ? 0 : (planes > 8 ? 8 : static_cast<int>(planes));

    // Our own queries are ours to clean up: an error left here would be read by
    // the GAME's next glGetError, and by our own drain as somebody else's.
    for (int i = 0; i < 64 && im.gl.GetError() != GL_NO_ERROR_; ++i) { }

    im.ready = true;
    return true;
}

void Renderer::requestArtReload() {
    if (m_impl) m_impl->artReload.store(true, std::memory_order_relaxed);
}

bool Renderer::render(const hudsw::Frame& frame, int w, int h,
                      float vx, float vy, float vw, float vh) {
    if (!ok() || w <= 0 || h <= 0) return false;
    Impl& im = *m_impl;
    Gl& g = im.gl;

    struct GlResolver : hudbatch::Resolver {
        Impl& im;
        explicit GlResolver(Impl& i) : im(i) {}
        const void* texture(const std::string& b, bool icon, const std::string& r) override {
            Impl::Tex* t = im.texture(b, icon, r);
            return t ? reinterpret_cast<const void*>(static_cast<uintptr_t>(t->id)) : nullptr;
        }
        const void* font(const std::string& b, const std::string& r,
                         const hudassets::FntFont** out) override {
            Impl::Font* f = im.font(b, r);
            if (!f) return nullptr;
            *out = &f->font;
            return reinterpret_cast<const void*>(static_cast<uintptr_t>(f->id));
        }
        const void* white() override {
            if (!im.white) {
                const uint8_t px[4] = { 255, 255, 255, 255 };
                im.white = im.upload(px, 1, 1, false);
            }
            return reinterpret_cast<const void*>(static_cast<uintptr_t>(im.white));
        }
    } resolver(im);

    // Consume a pending art reload HERE: the game thread, with the context
    // current, which is the only place glDeleteTextures is legal. Doing it at
    // the request site would either race the render or have to abandon the
    // names, and abandoning them leaks one set per reload - which an author
    // iterating on a theme does dozens of times in a sitting.
    // CONTEXT LOSS. A resolution change destroys the game's GL context and makes
    // a fresh one; every texture name we hold belonged to the dead one. After a
    // resolution change the HUD comes back as white blocks, because a stale
    // name binds to nothing (or, worse, to something else).
    //
    // The caches are ABANDONED, never deleted. glDeleteTextures here would be
    // actively harmful: those numeric names are meaningless in the new context
    // and may well have been handed out to the GAME's own textures by now, so
    // deleting them would corrupt the game's rendering - the one failure this
    // whole design is built to avoid. The dead context took its objects with it
    // when it died; there is nothing left to free.
    im.decodesThisFrame = 0;   // per-frame decode budget; see kMaxDecodesPerFrame

    // DRAIN SOMEBODY ELSE'S ERRORS BEFORE WE DRAW.
    //
    // GL errors are a QUEUE and glGetError pops one at a time, so an error left
    // behind by the game - or by an injected layer like ReShade, which replaces
    // opengl32.dll and does its own GL work - is still sitting there when we
    // arrive. The check at the end of this function would then read that error,
    // conclude WE corrupted the context, and latch the backend off for the whole
    // session: "render failed (GL error 0x0500)" on the first frame with no
    // effects even enabled, and the feature silently doing nothing thereafter.
    //
    // Draining here makes the check at the bottom mean what it claims: an error
    // raised BY OUR CALLS. It also makes the latch trustworthy enough to keep -
    // it is a harsh response, and it should only fire for our own mistakes.
    if (g.GetError) {
        int stale = 0;
        while (g.GetError() != GL_NO_ERROR_ && stale < 64) ++stale;
        if (stale > 0 && !im.warnedStaleErrors) {
            im.warnedStaleErrors = true;
            // Worth one line: it says the errors are not ours, and a field report
            // showing this is how we learn which layers a player is running.
            DEBUG_INFO_F("hudgl: the GL context already had %d queued error(s) on entry "
                         "(raised before our Draw, not by us) - drained so our own error "
                         "check stays honest", stale);
        }
    }

    const HGLRC nowCtx = g.wglGetCurrentContext ? g.wglGetCurrentContext() : nullptr;
    if (nowCtx != im.ctx) {
        DEBUG_INFO_F("hudgl: GL context changed (%p -> %p) - abandoning %zu cached "
                     "textures and %zu fonts; they died with the old context",
                     static_cast<void*>(im.ctx), static_cast<void*>(nowCtx),
                     im.texs.size(), im.fonts.size());
        im.texs.clear();
        im.fonts.clear();
        im.white = 0;
        im.ctx = nowCtx;
    }

    if (im.artReload.exchange(false, std::memory_order_relaxed)) {
        for (auto& kv : im.texs)
            if (kv.second.id) g.DeleteTextures(1, &kv.second.id);
        im.texs.clear();
        // Fonts are deliberately kept: a .fnt is not something anyone iterates
        // on, and re-decoding an atlas per reload would be pure cost.
    }

    // ---- save -------------------------------------------------------------
    // Verified, not assumed: the attrib stack is only guaranteed 16 deep, and a
    // failed push whose pop still runs would restore state the GAME pushed.
    // That is the one way this code could corrupt the frame rather than merely
    // fail to draw, so it is checked rather than trusted.
    GLint d0 = 0, d1 = 0;
    g.GetIntegerv(GL_ATTRIB_STACK_DEPTH, &d0);
    g.PushAttrib(GL_ALL_ATTRIB_BITS);
    g.GetIntegerv(GL_ATTRIB_STACK_DEPTH, &d1);
    if (d1 <= d0) { im.error = "glPushAttrib did not take"; return false; }
    GLint c0 = 0, c1 = 0;
    g.GetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &c0);
    g.PushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    g.GetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &c1);
    if (c1 <= c0) {
        // Symmetric with the server-side push above, which already refuses to
        // draw when it does not take. Drawing anyway would disable the GAME's
        // vertex, colour and texcoord arrays with no pop to put them back -
        // corrupting the next draw, which is the one outcome this whole design
        // exists to prevent. Declining costs a frame of HUD; the other costs the
        // game's frame.
        g.PopAttrib();
        im.error = "glPushClientAttrib did not take";
        return false;
    }

    // ---- one texture unit, and it is unit 0 -------------------------------
    // THE SECOND FAILURE THIS BACKEND CANNOT FEEL. Every texturing call below
    // is PER-UNIT, and "unit 0" is an assumption about the game unless we make
    // it a statement about our draw. Being wrong about it raises no GL error,
    // so the error check at the bottom never fires and the HUD never falls back
    // - it just comes out wrong.
    //
    // CLIENT_ACTIVE_TEXTURE is the vicious one. glTexCoordPointer feeds the
    // CLIENT-active unit, so with the game leaving it on unit 1 our UVs go to a
    // unit that is not drawing and unit 0 samples every quad at the CURRENT
    // texcoord - ONE CONSTANT TEXEL for the whole frame. That does not read as a
    // blank HUD, which would be obvious; it reads as a HUD in roughly the right
    // colours in which every glyph and icon is a solid block, because a flat
    // sprite survives being reduced to one of its own texels and a glyph atlas
    // does not.
    //
    // Units ABOVE 0 matter too: fixed-function applies every ENABLED unit in
    // turn, so one the game left on both modulates our fragments with its
    // texture and reads its own coord array, which our vertices do not feed.
    //
    // All of it is inside the two pushes above - ACTIVE_TEXTURE belongs to
    // GL_TEXTURE_BIT and CLIENT_ACTIVE_TEXTURE to GL_CLIENT_VERTEX_ARRAY_BIT -
    // so it is ours to set and the pops put it back.
    // Read for the LOG, not to restore from - both pops above are verified to
    // have taken, so putting these back is theirs. What the game leaves here is
    // the single most useful thing a field report can carry about this backend,
    // and it costs two queries a frame.
    GLint prevClientUnit = static_cast<GLint>(GL_TEXTURE0);
    GLint prevUnit = static_cast<GLint>(GL_TEXTURE0);
    int enabledUnitMask = 0;     // diagnostics only; see the one-shot log below
    if (im.activeTexture) {
        g.GetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
        g.GetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &prevClientUnit);
        // Descending, so both loops END with unit 0 selected.
        for (int u = im.texUnits - 1; u >= 0; --u) {
            im.activeTexture(static_cast<GLenum>(GL_TEXTURE0 + u));
            if (u > 0) {
                if (!im.loggedIncoming) {
                    GLint on = 0;
                    g.GetIntegerv(GL_TEXTURE_2D, &on);
                    // Only the low bits are reportable: texUnits can be 64 now,
                    // and shifting an int by >= 31 is undefined - on exactly the
                    // drivers the clamp comment anticipates.
                    if (on && u < 31) enabledUnitMask |= (1 << u);
                }
                g.Disable(GL_TEXTURE_2D);
            }
            // AND ITS COORD ARRAY. Disabling a unit's texturing does not stop
            // glDrawArrays from DEREFERENCING an enabled GL_TEXTURE_COORD_ARRAY
            // on it: enabled client arrays are fetched, and whether a driver
            // skips a unit that is not texturing is an optimisation, not a
            // promise. The game leaves CLIENT_ACTIVE_TEXTURE on unit 7 (measured,
            // see the log line below), so its array is very likely enabled with a
            // pointer into ITS vertex data - which our 36k-vertex draw would then
            // read past the end of. Restored by the client-attrib pop.
            im.clientActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + u));
            if (u > 0) g.DisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }

    // Log ONCE what the game actually handed us, BEFORE the disables below
    // change any of it. None of this state announces itself - no error, no
    // warning, just a wrong picture - so a machine unlike the author's has to be
    // diagnosable from the log alone. Every value here is a "0" on a context
    // that would have rendered correctly without the block above, which makes
    // the line worth reading rather than noise.
    if (!im.loggedIncoming) {
        im.loggedIncoming = true;
        GLint fog = 0, gen = 0, logic = 0, stip = 0, poly[2] = { 0, 0 };
        // Whether the game gave us a MULTISAMPLE framebuffer to draw into, which
        // is the only thing that decides if enabling GL_MULTISAMPLE below can do
        // anything at all. sampleBuffers=0 means the player has AA off in the
        // game and no amount of state-setting on our side will smooth the map
        // ribbon's angled quads - the answer there is to feather the ribbon
        // itself, which works on any renderer. Logged because "I can't see a
        // difference" is otherwise indistinguishable between "it did nothing"
        // and "there was nothing for it to do".
        GLint sampleBufs = 0, samples = 0;
        if (im.version >= 13) {
            g.GetIntegerv(GL_SAMPLE_BUFFERS, &sampleBufs);
            g.GetIntegerv(GL_SAMPLES, &samples);
        }
        // GetIntegerv rather than glIsEnabled: one fewer entry point to bind for
        // a line that runs once, and it is already resolved.
        g.GetIntegerv(GL_FOG, &fog);
        g.GetIntegerv(GL_TEXTURE_GEN_S, &gen);
        g.GetIntegerv(GL_COLOR_LOGIC_OP, &logic);
        g.GetIntegerv(GL_POLYGON_STIPPLE, &stip);
        g.GetIntegerv(GL_POLYGON_MODE, poly);
        DEBUG_INFO_F("hudgl: incoming context state - activeUnit=%d clientActiveUnit=%d "
                     "otherUnitsEnabled=0x%02x (of %d) clipPlanes=%d fog=%d texGenS=%d "
                     "logicOp=%d polyStipple=%d polyMode=0x%04x sampleBuffers=%d samples=%d",
                     static_cast<int>(prevUnit) - static_cast<int>(GL_TEXTURE0),
                     static_cast<int>(prevClientUnit) - static_cast<int>(GL_TEXTURE0),
                     enabledUnitMask, im.texUnits, im.clipPlanes,
                     static_cast<int>(fog), static_cast<int>(gen),
                     static_cast<int>(logic), static_cast<int>(stip),
                     static_cast<unsigned>(poly[0]),
                     static_cast<int>(sampleBufs), static_cast<int>(samples));
    }

    // ---- build, INSIDE the save -------------------------------------------
    // The build is what triggers every decode and every glTexImage2D upload,
    // through the resolver. Run BEFORE the pushes it would quietly make two of
    // our calls permanent in the game's context: upload()'s GL_UNPACK_ALIGNMENT
    // of 1 and whatever texture it left bound. Inside them, the pops restore
    // both.
    hudbatch::build(frame, w, h, vx, vy, vw, vh, resolver, im.verts, im.runs);
    if (im.verts.empty()) {              // nothing to draw is not a failure
        g.PopClientAttrib();
        g.PopAttrib();
        return true;
    }

    g.MatrixMode(GL_TEXTURE);    g.PushMatrix(); g.LoadIdentity();
    g.MatrixMode(GL_PROJECTION); g.PushMatrix(); g.LoadIdentity();
    g.MatrixMode(GL_MODELVIEW);  g.PushMatrix(); g.LoadIdentity();
    // No glOrtho: the batcher already emits NDC, and identity matrices are what
    // make that true for GL as well as D3D (both put clip-space y=+1 at the top).

    // ---- state for our draw ----------------------------------------------
    // DepthMask and StencilMask matter more than the tests they pair with: with
    // depth TEST off, depth WRITES still happen unless masked, which would
    // punch a hole in a scene rendered after the callback.
    g.Disable(GL_DEPTH_TEST);   g.DepthMask(0);
    g.Disable(GL_STENCIL_TEST); g.StencilMask(0);
    g.Disable(GL_CULL_FACE);
    g.Disable(GL_LIGHTING);
    g.Disable(GL_ALPHA_TEST);
    g.Disable(GL_SCISSOR_TEST);
    // Same class as the texture units: each of these alters a textured, blended
    // triangle without raising a single GL error, so leaving it at "whatever the
    // game had" is a silent-wrong, not a fallback. Fixed-function state we
    // depend on is state we set - the list is the fragment pipeline, not the
    // ones that have bitten us.
    g.Disable(GL_FOG);              // tints every fragment toward the fog colour
    g.Disable(GL_COLOR_LOGIC_OP);   // replaces blending outright
    g.Disable(GL_POLYGON_STIPPLE);  // punches a pattern of holes through the HUD
    g.Disable(GL_TEXTURE_GEN_S);    // texgen REPLACES the UVs we just supplied
    g.Disable(GL_TEXTURE_GEN_T);
    g.Disable(GL_TEXTURE_GEN_R);
    g.Disable(GL_TEXTURE_GEN_Q);
    for (int i = 0; i < im.clipPlanes; ++i)
        g.Disable(static_cast<GLenum>(GL_CLIP_PLANE0 + i));   // clips our quads away
    if (im.version >= 13) {         // GATED: the sample caps are GL 1.3
        // ENABLED, where everything around it is disabled: this is the one
        // piece of inherited state we would WANT rather than remove, and
        // leaving it inherited means taking whatever the game had left set.
        //
        // MEASURED TO DO NOTHING IN MX BIKES, and that is worth knowing before
        // anyone tries to smooth the map ribbon this way. The diagnostic line
        // above reports GL_SAMPLE_BUFFERS/GL_SAMPLES, and the field answer is
        // sampleBuffers=0 samples=0 - in a WINDOW, in FULLSCREEN, and with the
        // game's own antialiasing set to 16x. The game resolves its multisample
        // target before the Draw callback, so the framebuffer we are handed has
        // no samples to work with. Which also means the ENGINE path cannot
        // antialias our quads either: it draws into the same buffer.
        //
        // Kept anyway, for three cheap reasons: it is one call against a
        // frame budget it cannot measurably dent, it is the correct answer to
        // an otherwise inherited cap (the class of bug the texture-unit block
        // above exists for), and the other PiBoSo titles - or a
        // later MX Bikes - may hand us a multisampled buffer. Do not read its
        // presence as evidence the map ribbon is antialiased; it is not.
        // Smoothing those quads needs geometry we emit ourselves (a full-alpha
        // core plus fading edge strips), which is costed in
        // plans/gl_in_context_renderer.md.
        g.Enable(GL_MULTISAMPLE);
        g.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE);   // dithers glyph coverage into holes
        g.Disable(GL_SAMPLE_ALPHA_TO_ONE);
        g.Disable(GL_SAMPLE_COVERAGE);
    }
    g.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);    // a wireframe HUD is still silent
    g.ColorMask(1, 1, 1, 1);
    g.Enable(GL_BLEND);
    g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   // straight alpha over an opaque frame
    g.Enable(GL_TEXTURE_2D);
    g.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // The vertex colour array supplies per-vertex colour, but glColor is the
    // fallback if a driver ignores the array; white keeps MODULATE neutral.
    g.Color4f(1.0f, 1.0f, 1.0f, 1.0f);

    // Client arrays need no bound VBO and no bound VAO. Neither is covered by
    // glPushClientAttrib, and with a VBO bound glVertexPointer reads its pointer
    // as an OFFSET into that buffer - which draws nothing, silently, and a HUD
    // that draws nothing benchmarks as very fast. Handled here by construction.
    // A BOUND SHADER PROGRAM IS THE ONE FAILURE THIS BACKEND CANNOT FEEL.
    // Everything else that can go wrong here raises a GL error, and render()
    // turns any GL error into a clean fallback to engine drawing. A bound
    // program raises nothing: fixed-function processing is simply replaced, our
    // vertices go through the GAME's shader, and the HUD comes out as garbage
    // or not at all. That degrades to WRONG where the rest of the design
    // degrades to SAFE, so it is handled explicitly.
    //
    // Unbind-and-restore rather than decline-to-draw, because declining would
    // mean the backend silently never works on a game that binds one - the user
    // sets the key and nothing happens, forever. glUseProgram comes through
    // wglGetProcAddress, which this file otherwise refuses; the exception is
    // sound because it is CONDITIONAL ON A STATE THAT IMPLIES ITS OWN
    // AVAILABILITY - a program can only be bound on a GL 2.0+ context, where
    // glUseProgram necessarily exists. Exactly the argument the buffer bindings
    // below already rely on. If it somehow cannot be resolved, we decline.
    GLint prevProgram = 0;
    void (WINAPI* useProgram)(GLuint) = nullptr;
    if (im.version >= 20) {
        g.GetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        if (prevProgram != 0) {
            auto gpa = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
                reinterpret_cast<void*>(GetProcAddress(im.lib, "wglGetProcAddress")));
            if (gpa) useProgram = reinterpret_cast<decltype(useProgram)>(
                reinterpret_cast<void*>(gpa("glUseProgram")));
            if (!useProgram) {
                if (!im.warnedProgram) {
                    im.warnedProgram = true;
                    DEBUG_WARN("hudgl: the game has a shader program bound and glUseProgram "
                               "could not be resolved - declining to draw so the engine "
                               "keeps the HUD rather than rendering it through their shader");
                }
                // The three matrix pushes above are ours too; left on the
                // stack here they leak into the game's matrices, which only a
                // game binding a program would ever see.
                g.MatrixMode(GL_MODELVIEW);  g.PopMatrix();
                g.MatrixMode(GL_PROJECTION); g.PopMatrix();
                g.MatrixMode(GL_TEXTURE);    g.PopMatrix();
                g.PopClientAttrib();
                g.PopAttrib();
                return false;
            }
            useProgram(0);
            if (!im.warnedProgram) {
                im.warnedProgram = true;
                DEBUG_INFO_F("hudgl: the game had shader program %d bound at Draw time; "
                             "unbinding for our draw and restoring after", prevProgram);
            }
        }
    }

    // Both bindings postdate GL 1.1, so both queries are GATED. A context that
    // predates them cannot have anything bound to ask about, so skipping the
    // query is not merely safe - it is exactly correct.
    GLint prevArray = 0, prevVao = 0;
    if (im.version >= 15) g.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
    if (im.version >= 30) g.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    void (WINAPI* bindBuffer)(GLenum, GLuint) = nullptr;
    void (WINAPI* bindVao)(GLuint) = nullptr;
    if (prevArray || prevVao) {
        // Only reached when the game actually left one bound, so the extension
        // lookup is not on the common path and its absence is not fatal - we
        // simply cannot draw this frame, and say so.
        auto gpa = reinterpret_cast<PROC (WINAPI*)(LPCSTR)>(
            reinterpret_cast<void*>(GetProcAddress(im.lib, "wglGetProcAddress")));
        if (gpa) {
            bindBuffer = reinterpret_cast<decltype(bindBuffer)>(
                reinterpret_cast<void*>(gpa("glBindBuffer")));
            bindVao = reinterpret_cast<decltype(bindVao)>(
                reinterpret_cast<void*>(gpa("glBindVertexArray")));
        }
        if (prevArray && bindBuffer) bindBuffer(GL_ARRAY_BUFFER, 0);
        if (prevVao && bindVao) bindVao(0);
    }

    // ---- draw -------------------------------------------------------------
    const hudbatch::Vertex* v = im.verts.data();
    g.EnableClientState(GL_VERTEX_ARRAY);
    g.EnableClientState(GL_TEXTURE_COORD_ARRAY);
    g.EnableClientState(GL_COLOR_ARRAY);
    g.DisableClientState(GL_NORMAL_ARRAY);
    g.VertexPointer(2, GL_FLOAT, sizeof(hudbatch::Vertex), &v->x);
    g.TexCoordPointer(2, GL_FLOAT, sizeof(hudbatch::Vertex), &v->u);
    g.ColorPointer(4, GL_UNSIGNED_BYTE, sizeof(hudbatch::Vertex), &v->rgba);

    for (const hudbatch::Run& r : im.runs) {
        if (!r.count) continue;
        g.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(
            reinterpret_cast<uintptr_t>(r.tex)));
        g.DrawArrays(GL_TRIANGLES, static_cast<GLint>(r.start),
                     static_cast<GLsizei>(r.count));
    }

    g.DisableClientState(GL_COLOR_ARRAY);
    g.DisableClientState(GL_TEXTURE_COORD_ARRAY);
    g.DisableClientState(GL_VERTEX_ARRAY);

    // ---- restore ----------------------------------------------------------
    if (prevVao && bindVao) bindVao(static_cast<GLuint>(prevVao));
    if (prevArray && bindBuffer) bindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArray));
    if (prevProgram && useProgram) useProgram(static_cast<GLuint>(prevProgram));
    g.MatrixMode(GL_MODELVIEW);  g.PopMatrix();
    g.MatrixMode(GL_PROJECTION); g.PopMatrix();
    g.MatrixMode(GL_TEXTURE);    g.PopMatrix();
    g.PopClientAttrib();
    g.PopAttrib();

    // A GL error means we may have left the context in a state we do not
    // understand, so it latches the backend off rather than being retried.
    GLenum err = g.GetError();
    if (err != GL_NO_ERROR_) {
        // Bounded, for the same reason the drain on entry is: a context that
        // never returns GL_NO_ERROR would otherwise spin the Draw thread
        // forever, hanging the game rather than losing the HUD.
        for (int i = 0; i < 64 && g.GetError() != GL_NO_ERROR_; ++i) { }
        char buf[96];
        snprintf(buf, sizeof(buf), "GL error 0x%04x during render", err);
        im.error = buf;
        return false;
    }
    return true;
}

}  // namespace hudgl

#else   // !_WIN32

namespace hudgl {
Renderer::Renderer() = default;
Renderer::~Renderer() = default;
bool Renderer::init() { return false; }
bool Renderer::ok() const { return false; }
const std::string& Renderer::lastError() const { static const std::string s; return s; }
bool Renderer::render(const hudsw::Frame&, int, int, float, float, float, float) { return false; }
void Renderer::requestArtReload() {}
}  // namespace hudgl

#endif
