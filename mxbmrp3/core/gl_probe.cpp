// ============================================================================
// core/gl_probe.cpp
// Implementation of the Phase 0 GL feasibility probe — see gl_probe.h for what
// it answers and why it must be answered in-game.
// ============================================================================
#include "gl_probe.h"

#if defined(_WIN32)

#include "gl_state_fingerprint.h"
#include "ui_config.h"
#include "input_manager.h"
#include "ui_viewport.h"
#include "../diagnostics/logger.h"

#include <windows.h>
#include <string>
#include <chrono>
#include <vector>

namespace GlProbe {
namespace {

// ---------------------------------------------------------------------------
// GL types and constants, spelled out rather than #included.
//
// <GL/gl.h> ships with both toolchains but declares only GL 1.1, which is short
// of half the tokens below; more importantly, including it invites an
// opengl32 import, and the whole point of GetModuleHandle + GetProcAddress is
// that this plugin gains no import a host could fail to resolve. These are
// stable API constants — they have not changed since the headers that define
// them, and they cannot.
// ---------------------------------------------------------------------------
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
constexpr GLenum GL_QUADS = 0x0007;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_VERTEX_ARRAY = 0x8074;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x85B5;
constexpr GLenum GL_COLOR_ARRAY = 0x8076;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_FRONT = 0x0404;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_LIGHTING = 0x0B50;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_VIEWPORT = 0x0BA2;
constexpr GLenum GL_ATTRIB_STACK_DEPTH = 0x0BB0;
constexpr GLenum GL_CLIENT_ATTRIB_STACK_DEPTH = 0x0BB1;
constexpr GLenum GL_ALPHA_TEST = 0x0BC0;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_MODELVIEW = 0x1700;
constexpr GLenum GL_PROJECTION = 0x1701;
constexpr GLenum GL_TEXTURE = 0x1702;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
constexpr GLenum GL_CURRENT_PROGRAM = 0x8B8D;
constexpr GLenum GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
constexpr GLenum GL_CONTEXT_PROFILE_MASK = 0x9126;
constexpr GLint  GL_CONTEXT_COMPATIBILITY_PROFILE_BIT = 0x0002;
constexpr GLbitfield GL_ALL_ATTRIB_BITS = 0x000FFFFF;
constexpr GLbitfield GL_CLIENT_ALL_ATTRIB_BITS = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Entry points. The GL 1.1 set are real exports of opengl32.dll and resolve
// with GetProcAddress; glUseProgram is an extension and only wglGetProcAddress
// can answer for it (and only with a context current, which is why it is
// resolved lazily on the first frame that has one).
// ---------------------------------------------------------------------------
struct Gl {
    HGLRC (WINAPI* wglGetCurrentContext)() = nullptr;
    HDC   (WINAPI* wglGetCurrentDC)() = nullptr;
    PROC  (WINAPI* wglGetProcAddress)(LPCSTR) = nullptr;

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
    void (WINAPI* Ortho)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) = nullptr;

    void (WINAPI* Enable)(GLenum) = nullptr;
    void (WINAPI* Disable)(GLenum) = nullptr;
    void (WINAPI* DepthMask)(GLboolean) = nullptr;
    void (WINAPI* StencilMask)(GLuint) = nullptr;
    void (WINAPI* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;
    void (WINAPI* PixelStorei)(GLenum, GLint) = nullptr;

    void (WINAPI* Begin)(GLenum) = nullptr;
    void (WINAPI* End)() = nullptr;
    void (WINAPI* Color4f)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (WINAPI* Vertex2f)(GLfloat, GLfloat) = nullptr;
    void (WINAPI* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (WINAPI* BlendFunc)(GLenum, GLenum) = nullptr;
    // GL 1.1 client vertex arrays: the batched measurement path. No VBO, so no
    // extension loading - and a client array is a fair proxy for what a real
    // backend does per frame with a dynamic buffer.
    void (WINAPI* EnableClientState)(GLenum) = nullptr;
    void (WINAPI* DisableClientState)(GLenum) = nullptr;
    void (WINAPI* VertexPointer)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* ColorPointer)(GLint, GLenum, GLsizei, const void*) = nullptr;
    void (WINAPI* DrawArrays)(GLenum, GLint, GLsizei) = nullptr;

    // Extension, nullable: a bound shader program would run instead of the
    // fixed-function pipeline and swallow the probe quad, so unbinding it is
    // the one modern reset the draw genuinely needs.
    void (WINAPI* UseProgram)(GLuint) = nullptr;
    // Also extensions, also nullable, and NOT optional for the batched
    // measurement path: with a VBO bound to GL_ARRAY_BUFFER, glVertexPointer's
    // pointer is reinterpreted as a byte OFFSET INTO THAT BUFFER rather than a
    // client address, and with a non-zero VAO bound client arrays are invalid
    // outright. Either way the draw silently renders nothing - which measures
    // as "in-context GL is free" and passes a performance bar with a perfect
    // score. That is exactly what the first in-game sweep reported.
    void (WINAPI* BindBuffer)(GLenum, GLuint) = nullptr;
    void (WINAPI* BindVertexArray)(GLuint) = nullptr;
};

// State, all game-thread-only: onDraw() is documented as Draw-thread-only and
// status() is read from that same thread by the test hook.
struct ProbeState {
    bool resolved = false;         // the one-time module + entry point lookup ran
    bool extResolved = false;      // the context-dependent lookup ran
    HMODULE lib = nullptr;
    Gl gl;
    Status st;
    bool reported = false;         // the descriptive block has been logged
    // The kill verdict is NOT declared on the first contextless frame. A game may
    // well issue a Draw before its context is current, and "no GL context" is the
    // one line that ends this project - reporting it early, from a frame that was
    // simply too soon, would spend the author's single game launch on a wrong
    // answer. So it waits out a run of frames first, and any frame with a context
    // in that window reports the real thing instead.
    int contextlessFrames = 0;
    int profileMask = 0;           // raw GL_CONTEXT_PROFILE_MASK, reported as-is
    int verifyFramesLeft = 3;      // frames still doing readback + state diff
    int lastLoadN = 0;             // re-arm the checks when the load size changes
    // Draw-cadence heartbeat. "The plugin gets no callbacks in menus" is
    // load-bearing for the overlay's stale-hide and is documented in three
    // places, but it is INHERITED - nothing on this build has ever measured it.
    // A gap in Draw is only observable from the Draw that ends it, so the resume
    // carries the gap length: park in a menu for five seconds and the log says
    // "resumed after 5000 ms", which is the evidence. Draw firing throughout
    // would show no gap at all - equally decisive, in the other direction.
    std::chrono::steady_clock::time_point lastDraw{};
    int lastState = -1;
    int drawsSinceGap = 0;
    bool pushWarned = false;       // the push-failure warnings are one-shot: the
                                   // draw path runs EVERY frame at glProbe=2, and
                                   // a per-frame warning at 400 fps floods the log
};

ProbeState& state() {
    static ProbeState s;
    return s;
}

const char* str(const Gl& gl, GLenum name) {
    const GLubyte* p = gl.GetString(name);
    return p ? reinterpret_cast<const char*>(p) : "(null)";
}

// One-time module + GL 1.1 entry point resolution. Returns false if the game is
// not a GL app, or is one whose opengl32 is missing something we need.
bool resolveCore(ProbeState& s) {
    if (s.resolved) return s.st.entryPointsOk;
    s.resolved = true;

    s.lib = GetModuleHandleA("opengl32.dll");
    s.st.moduleResident = (s.lib != nullptr);
    if (!s.lib) return false;

    Gl& g = s.gl;
    auto get = [&s](const char* n) { return GetProcAddress(s.lib, n); };
#define MXB_GL_BIND(field, name) \
    g.field = reinterpret_cast<decltype(g.field)>(reinterpret_cast<void*>(get(name)))
    MXB_GL_BIND(wglGetCurrentContext, "wglGetCurrentContext");
    MXB_GL_BIND(wglGetCurrentDC,      "wglGetCurrentDC");
    MXB_GL_BIND(wglGetProcAddress,    "wglGetProcAddress");
    MXB_GL_BIND(GetString,            "glGetString");
    MXB_GL_BIND(GetIntegerv,          "glGetIntegerv");
    MXB_GL_BIND(GetError,             "glGetError");
    MXB_GL_BIND(PushAttrib,           "glPushAttrib");
    MXB_GL_BIND(PopAttrib,            "glPopAttrib");
    MXB_GL_BIND(PushClientAttrib,     "glPushClientAttrib");
    MXB_GL_BIND(PopClientAttrib,      "glPopClientAttrib");
    MXB_GL_BIND(MatrixMode,           "glMatrixMode");
    MXB_GL_BIND(PushMatrix,           "glPushMatrix");
    MXB_GL_BIND(PopMatrix,            "glPopMatrix");
    MXB_GL_BIND(LoadIdentity,         "glLoadIdentity");
    MXB_GL_BIND(Ortho,                "glOrtho");
    MXB_GL_BIND(Enable,               "glEnable");
    MXB_GL_BIND(Disable,              "glDisable");
    MXB_GL_BIND(DepthMask,            "glDepthMask");
    MXB_GL_BIND(StencilMask,          "glStencilMask");
    MXB_GL_BIND(ColorMask,            "glColorMask");
    MXB_GL_BIND(PixelStorei,          "glPixelStorei");
    MXB_GL_BIND(Begin,                "glBegin");
    MXB_GL_BIND(End,                  "glEnd");
    MXB_GL_BIND(Color4f,              "glColor4f");
    MXB_GL_BIND(Vertex2f,             "glVertex2f");
    MXB_GL_BIND(ReadPixels,           "glReadPixels");
    MXB_GL_BIND(BlendFunc,            "glBlendFunc");
    MXB_GL_BIND(EnableClientState,    "glEnableClientState");
    MXB_GL_BIND(DisableClientState,   "glDisableClientState");
    MXB_GL_BIND(VertexPointer,        "glVertexPointer");
    MXB_GL_BIND(ColorPointer,         "glColorPointer");
    MXB_GL_BIND(DrawArrays,           "glDrawArrays");
#undef MXB_GL_BIND

    s.st.entryPointsOk =
        g.wglGetCurrentContext && g.wglGetCurrentDC && g.wglGetProcAddress &&
        g.GetString && g.GetIntegerv && g.GetError &&
        g.PushAttrib && g.PopAttrib && g.PushClientAttrib && g.PopClientAttrib &&
        g.MatrixMode && g.PushMatrix && g.PopMatrix && g.LoadIdentity && g.Ortho &&
        g.Enable && g.Disable && g.DepthMask && g.StencilMask && g.ColorMask &&
        g.PixelStorei && g.Begin && g.End && g.Color4f && g.Vertex2f && g.ReadPixels &&
        g.BlendFunc && g.EnableClientState && g.DisableClientState &&
        g.VertexPointer && g.ColorPointer && g.DrawArrays;
    return s.st.entryPointsOk;
}

// Describe the context once. Read-only: this is the whole of glProbe=1.
// How many contextless Draw calls before the kill verdict is believed. The game
// runs at hundreds of frames a second, so this is a fraction of a second of
// patience in exchange for not answering the project's central question from a
// frame that was merely early.
constexpr int kContextlessFramesBeforeVerdict = 120;

void describeOnce(ProbeState& s) {
    if (s.reported) return;
    Gl& g = s.gl;

    if (!s.st.contextCurrent) {
        if (++s.contextlessFrames < kContextlessFramesBeforeVerdict) return;
        s.reported = true;
        // The kill criterion. Say so outright rather than leaving the reader to
        // infer it from an absence - this line is the deliverable.
        DEBUG_WARN_F("GlProbe: NO GL CONTEXT is current on the Draw callback thread "
                     "(thread=%lu, checked over %d consecutive Draw calls). In-context "
                     "rendering is not possible at this hook point - see "
                     "plans/gl_in_context_renderer.md, Phase 0 kill criteria.",
                     GetCurrentThreadId(), s.contextlessFrames);
        return;
    }

    s.reported = true;
    DEBUG_INFO_F("GlProbe: Draw thread=%lu  opengl32 resident=%d  entry points=%d  context=%p"
                 "  (contextless Draw calls before this: %d)",
                 GetCurrentThreadId(), s.st.moduleResident ? 1 : 0,
                 s.st.entryPointsOk ? 1 : 0,
                 static_cast<void*>(g.wglGetCurrentContext()), s.contextlessFrames);

    // WHICH opengl32 answered. The game directory may hold ReShade's proxy, and
    // the plan calls that desirable rather than a problem - our calls then route
    // exactly like the game's own. Either way it is better seen than assumed,
    // and the path is the only thing that distinguishes them.
    {
        char modPath[MAX_PATH] = { 0 };
        if (GetModuleFileNameA(s.lib, modPath, static_cast<DWORD>(sizeof(modPath))) > 0)
            DEBUG_INFO_F("GlProbe: opengl32 module = %s", modPath);
    }

    // The DC the context is bound to, and whether it belongs to the game's own
    // window: a context current on a DC that is NOT the game window would mean
    // we are looking at some other surface entirely.
    {
        HDC dc = g.wglGetCurrentDC();
        HWND dcWnd = dc ? WindowFromDC(dc) : nullptr;
        HWND gameWnd = static_cast<HWND>(InputManager::getInstance().getGameWindowHandle());
        DEBUG_INFO_F("GlProbe: current DC=%p  its window=%p  game window=%p  (%s)",
                     static_cast<void*>(dc), static_cast<void*>(dcWnd),
                     static_cast<void*>(gameWnd),
                     (dcWnd && gameWnd && dcWnd == gameWnd) ? "same window - as expected"
                     : (gameWnd == nullptr) ? "game window not known yet"
                     : "DIFFERENT window - worth reporting");
    }

    DEBUG_INFO_F("GlProbe: vendor='%s' renderer='%s'", str(g, GL_VENDOR), str(g, GL_RENDERER));
    DEBUG_INFO_F("GlProbe: version='%s' glsl='%s' parsed=%d profileMask=0x%X profile=%s",
                 str(g, GL_VERSION), str(g, GL_SHADING_LANGUAGE_VERSION), s.st.glVersion,
                 static_cast<unsigned>(s.profileMask),
                 s.st.compatProfile ? "compatibility (glBegin usable)"
                                    : "core (glBegin UNAVAILABLE - Phase 2 needs a shader path)");
    DEBUG_INFO_F("GlProbe: draw framebuffer=%d read framebuffer=%d (0 = the default framebuffer, "
                 "i.e. the backbuffer that gets swapped)",
                 s.st.drawFramebuffer, s.st.readFramebuffer);

    // The viewport against the game's client size: a viewport that is not the
    // client rect says Draw sits inside some other pass, not the final one.
    RECT rc{};
    HWND hwnd = static_cast<HWND>(InputManager::getInstance().getGameWindowHandle());
    const bool haveRect = (hwnd != nullptr) && GetClientRect(hwnd, &rc);
    DEBUG_INFO_F("GlProbe: viewport=[%d,%d %dx%d]  game client=%s",
                 s.st.viewport[0], s.st.viewport[1], s.st.viewport[2], s.st.viewport[3],
                 haveRect ? (std::to_string(rc.right - rc.left) + "x" +
                             std::to_string(rc.bottom - rc.top)).c_str()
                          : "(unknown)");

    if (!s.st.compatProfile) {
        DEBUG_WARN("GlProbe: core profile - glProbe=2 will not draw. This is NOT a kill: "
                   "it means a GL backend must use a shader pipeline rather than fixed "
                   "function. Report the version line above.");
    }
}

// Read-only context interrogation, run every frame the probe is on so the log
// line above has values to print and the status hook stays current.
void sample(ProbeState& s) {
    Gl& g = s.gl;
    s.st.contextCurrent = (g.wglGetCurrentContext() != nullptr);
    if (!s.st.contextCurrent) return;

    s.st.glVersion = glprobe::parseVersion(str(g, GL_VERSION));

    // Before GL 3.2 there are no profiles and fixed function is always there.
    // From 3.2 the mask is authoritative. A context created by the legacy
    // wglCreateContext is always a compatibility context, which is what an
    // engine of this vintage is overwhelmingly likely to have.
    if (s.st.glVersion >= 32) {
        GLint mask = 0;
        g.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
        s.profileMask = mask;
        s.st.compatProfile = (mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
    } else {
        s.st.compatProfile = (s.st.glVersion > 0);
    }

    if (s.st.glVersion >= 30) {
        GLint fb = 0;
        g.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
        s.st.drawFramebuffer = fb;
        g.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fb);
        s.st.readFramebuffer = fb;
    }
    g.GetIntegerv(GL_VIEWPORT, s.st.viewport);
}

// The probe quad, in the plugin's normalized top-left-origin HUD coordinates:
// a magenta bar near the top-left, far enough in to be unmistakably ours and
// not a HUD element.
//
// THE ENGINE DRAWS A SECOND BAR flush underneath this one
// and the same width (see the glProbe block in HudManager::produceFrame). That
// pairing is the actual test of "correct position through the UiViewport
// mapping": we do not know what mapping the ENGINE applies to normalized
// coordinates, and drawing our own bar alone could only ever answer "something
// appeared". Two bars that line up exactly say the mappings agree; any
// horizontal offset or width difference measures the disagreement directly.
//
// It only discriminates at a NON-16:9 client, where the centered-16:9 mapping
// and a stretch-to-full-viewport mapping diverge - at 16:9 they are identical.
// The manual checklist says so.
// Sizes, relative to the pair's top-left corner (glProbeX/glProbeY). The bars
// stack: ours on top, the engine's flush beneath it.
constexpr float kBarW = 0.16f;
constexpr float kBarH = 0.03f;

// The pair's rects for a given origin. Shared with HudManager::produceFrame
// through glProbeBarRects() so the two sides cannot drift apart - if they did,
// a misalignment would read as "the mappings disagree" when it was only our
// own arithmetic. That failure would be indistinguishable from the real finding.
void barRects(float ox, float oy, float& x0, float& x1,
              float& glY0, float& glY1, float& refY0, float& refY1) {
    x0 = ox; x1 = ox + kBarW;
    glY0 = oy;            glY1 = oy + kBarH;
    refY0 = oy + kBarH;   refY1 = oy + 2.0f * kBarH;
}

// Gaps in the Draw callback, and changes of draw state. Logged only on
// TRANSITIONS: at 400 fps a per-frame line would be tens of thousands of
// entries and would itself perturb what it measures.
constexpr int kDrawGapMs = 250;   // below any real menu visit, above any frame

void heartbeat(ProbeState& s, int iState) {
    const auto now = std::chrono::steady_clock::now();
    if (s.lastDraw.time_since_epoch().count() != 0) {
        const long long gapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - s.lastDraw).count();
        if (gapMs >= kDrawGapMs) {
            DEBUG_INFO_F("GlProbe: Draw RESUMED after a %lld ms gap (state=%d; %d draws before "
                         "it). No Draw means no in-context GL draw either - whatever was on "
                         "screen during that gap, we did not render into it",
                         gapMs, iState, s.drawsSinceGap);
            s.drawsSinceGap = 0;
            ++s.st.drawGaps;
            s.st.lastGapMs = static_cast<int>(gapMs);
        }
    }
    s.lastDraw = now;
    ++s.drawsSinceGap;

    if (iState != s.lastState) {
        // 0 = on track, 1 = spectate, 2 = replay (mxb_api.h).
        const char* name = (iState == 0) ? "on track"
                         : (iState == 1) ? "spectate"
                         : (iState == 2) ? "replay" : "?";
        DEBUG_INFO_F("GlProbe: Draw state -> %d (%s)", iState, name);
        s.lastState = iState;
    }
}

// N quads drawn IN-CONTEXT, the GL half of the Phase 1 measurement. The engine
// half is the existing renderProbeQuads, and the two must stay comparable, so
// the shape and alpha are read from the renderProbe* keys rather than from keys
// of their own - a sweep of one is then directly against a sweep of the other.
//
// Two submission paths, both measured on purpose:
//   batch=0  glBegin/glEnd per quad. The slowest thing GL can do, so it is a
//            FLOOR: whatever this beats, GL beats.
//   batch=1  one glDrawArrays over a client-side vertex array. No VBO (that
//            would need extension loading), but per-frame client arrays are a
//            fair stand-in for a real backend's dynamic buffer.
// Reporting only the floor would understate GL; only the batch would understate
// how much work Phase 2 actually is. Hence both.
template <class MapX, class MapY>
void drawMeasurementLoad(ProbeState& s, MapX mapX, MapY mapY) {
    const int n = UiConfig::getInstance().getGlProbeQuads();
    if (n <= 0) return;
    Gl& g = s.gl;

    const bool fullscreen = UiConfig::getInstance().getRenderProbeFullscreen();
    const int alpha = UiConfig::getInstance().getRenderProbeAlpha();
    const float a = static_cast<float>(alpha) / 255.0f;
    // The engine blends these; so must we, or the comparison is between a
    // blended draw and an opaque one and the numbers mean nothing.
    if (alpha < 255) { g.Enable(GL_BLEND); g.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }

    const float x0 = 0.0f, y0 = 0.0f;
    const float x1 = fullscreen ? 1.0f : 0.01f;
    const float y1 = fullscreen ? 1.0f : 0.01f;
    const float px0 = mapX(x0), px1 = mapX(x1), py0 = mapY(y0), py1 = mapY(y1);

    if (UiConfig::getInstance().getGlProbeBatch() == 0) {
        g.Color4f(1.0f, 1.0f, 1.0f, a);
        g.Begin(GL_QUADS);
        for (int i = 0; i < n; ++i) {
            g.Vertex2f(px0, py0); g.Vertex2f(px0, py1);
            g.Vertex2f(px1, py1); g.Vertex2f(px1, py0);
        }
        g.End();
        return;
    }

    // Batched: six vertices per quad (two triangles), built once into a scratch
    // buffer that is reused across frames. A per-frame allocation here would be
    // measuring the allocator, not the renderer - and the 480fps budget in
    // CLAUDE.md forbids it in a per-frame path regardless.
    struct V { float x, y; unsigned char r, gr, b, al; };
    static std::vector<V> verts;
    const size_t need = static_cast<size_t>(n) * 6;
    if (verts.size() != need) verts.resize(need);
    const unsigned char A = static_cast<unsigned char>(alpha);
    const V a0{ px0, py0, 255, 255, 255, A };
    const V a1{ px0, py1, 255, 255, 255, A };
    const V a2{ px1, py1, 255, 255, 255, A };
    const V a3{ px1, py0, 255, 255, 255, A };
    for (int i = 0; i < n; ++i) {
        V* v = &verts[static_cast<size_t>(i) * 6];
        v[0] = a0; v[1] = a1; v[2] = a2;
        v[3] = a0; v[4] = a2; v[5] = a3;
    }
    // CLIENT ARRAYS REQUIRE NO BOUND VBO AND NO BOUND VAO. The game leaves its
    // own bindings up across the Draw callback, and neither is covered by
    // glPushClientAttrib. Skipping this is not a slow draw, it is a SILENT
    // no-op that reads as "in-context GL costs nothing".
    GLint prevArrayBuf = 0, prevVao = 0;
    if (g.BindBuffer && s.st.glVersion >= 15) {
        g.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
        if (prevArrayBuf != 0) g.BindBuffer(GL_ARRAY_BUFFER, 0);
    }
    if (g.BindVertexArray && s.st.glVersion >= 30) {
        g.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        if (prevVao != 0) g.BindVertexArray(0);
    }

    g.EnableClientState(GL_VERTEX_ARRAY);
    g.EnableClientState(GL_COLOR_ARRAY);
    g.VertexPointer(2, GL_FLOAT, sizeof(V), &verts[0].x);
    g.ColorPointer(4, GL_UNSIGNED_BYTE, sizeof(V), &verts[0].r);
    g.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(need));
    // The client-state enables are covered by glPushClientAttrib, but only when
    // that push actually took - which drawProbeQuad does not guarantee. Undo
    // them explicitly rather than relying on a pop that may not happen.
    g.DisableClientState(GL_COLOR_ARRAY);
    g.DisableClientState(GL_VERTEX_ARRAY);
    if (g.BindVertexArray && prevVao != 0) g.BindVertexArray(static_cast<GLuint>(prevVao));
    if (g.BindBuffer && prevArrayBuf != 0)
        g.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
}

// Draw one quad, save and restore everything, and (on `verify` frames) prove
// both that it landed and that nothing leaked.
void drawProbeQuad(ProbeState& s, bool verify) {
    Gl& g = s.gl;

    // Reading the error queue is destructive, so it happens only on verify
    // frames — where it is the only way to attribute an error to our own draw.
    if (verify) { while (g.GetError() != GL_NO_ERROR_) { /* drain */ } }

    auto reader = [&g](unsigned int token, int count, int* out) {
        (void)count;   // glGetIntegerv writes as many as the token defines
        g.GetIntegerv(static_cast<GLenum>(token), out);
    };
    glprobe::Fingerprint before;
    if (verify) before = glprobe::capture(s.st.glVersion, s.st.compatProfile, reader);

    // --- save -------------------------------------------------------------
    // glPushAttrib covers the fixed-function enables and the write masks;
    // it covers NO modern binding, and it does NOT cover the matrix stacks.
    //
    // The push is VERIFIED, not assumed. The attrib stack is only guaranteed 16
    // deep, and if the game has filled it our glPushAttrib fails with
    // GL_STACK_OVERFLOW while the matching glPopAttrib still pops - restoring
    // state the GAME pushed, which is the one way this diagnostic could actively
    // corrupt the frame rather than merely fail to draw. So the depth is read
    // either side, and a push that did not take means no draw and no pop.
    GLint attribDepth0 = 0, attribDepth1 = 0;
    g.GetIntegerv(GL_ATTRIB_STACK_DEPTH, &attribDepth0);
    g.PushAttrib(GL_ALL_ATTRIB_BITS);
    g.GetIntegerv(GL_ATTRIB_STACK_DEPTH, &attribDepth1);
    if (attribDepth1 <= attribDepth0) {
        if (!s.pushWarned) {
            s.pushWarned = true;
            DEBUG_WARN_F("GlProbe: glPushAttrib did not take (attrib stack depth %d, unchanged) - "
                         "not drawing and not popping, because a pop here would restore state "
                         "the GAME pushed", attribDepth0);
        }
        return;
    }
    GLint clientDepth0 = 0, clientDepth1 = 0;
    g.GetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &clientDepth0);
    g.PushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    g.GetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &clientDepth1);
    const bool clientPushed = (clientDepth1 > clientDepth0);
    if (!clientPushed && !s.pushWarned) {
        s.pushWarned = true;
        DEBUG_WARN_F("GlProbe: glPushClientAttrib did not take (client attrib stack depth %d) - "
                     "drawing without it; the client state this touches is only the pack "
                     "alignment for the readback", clientDepth0);
    }

    // The one modern reset the draw actually needs: with a program bound, the
    // fragment shader runs instead of the fixed-function pipeline and the quad
    // either vanishes or comes out as garbage. Everything else modern (VAO,
    // buffer bindings, samplers) is irrelevant to glBegin/glEnd with texturing
    // off — and the fingerprint diff is what checks that claim rather than
    // trusting it.
    GLint prevProgram = 0;
    if (g.UseProgram && s.st.glVersion >= 20) {
        g.GetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        if (prevProgram != 0) g.UseProgram(0);
    }

    GLint dvp0[4] = { 0, 0, 0, 0 };
    g.GetIntegerv(GL_VIEWPORT, dvp0);
    const double dvpW = dvp0[2] > 0 ? dvp0[2] : 1;
    const double dvpH = dvp0[3] > 0 ? dvp0[3] : 1;

    g.MatrixMode(GL_TEXTURE);    g.PushMatrix(); g.LoadIdentity();
    g.MatrixMode(GL_PROJECTION); g.PushMatrix(); g.LoadIdentity();
    // Viewport pixels, y down: HUD coordinates are mapped into this by the
    // UiViewport rect below, so a coordinate outside [0,1] stays on screen.
    g.Ortho(0.0, static_cast<double>(dvpW), static_cast<double>(dvpH), 0.0, -1.0, 1.0);
    g.MatrixMode(GL_MODELVIEW);  g.PushMatrix(); g.LoadIdentity();

    // --- draw -------------------------------------------------------------
    // DepthMask/StencilMask are the two that would corrupt the game's frame
    // rather than merely its state: with depth TEST off, depth WRITES still
    // happen unless the mask is cleared, and if Draw is called before the
    // scene renders that would punch a hole in it.
    g.Disable(GL_DEPTH_TEST);   g.DepthMask(0);
    g.Disable(GL_STENCIL_TEST); g.StencilMask(0);
    g.Disable(GL_BLEND);
    g.Disable(GL_TEXTURE_2D);
    g.Disable(GL_LIGHTING);
    g.Disable(GL_ALPHA_TEST);
    g.Disable(GL_SCISSOR_TEST);
    g.Disable(GL_CULL_FACE);
    g.ColorMask(1, 1, 1, 1);

    // Normalized HUD coords -> the centered 16:9 rect inside the GL viewport,
    // through the SAME UiViewport::compute the companion paint loop and the
    // cursor hit-test use. Elements outside [0,1] deliberately fall outside
    // that rect rather than being clipped, matching hudsw::Image::setViewport.
    const UiViewport::Rect ui = UiViewport::compute(dvp0[2], dvp0[3]);
    const float uiX = static_cast<float>(ui.x), uiY = static_cast<float>(ui.y);
    const float uiW = static_cast<float>(ui.w), uiH = static_cast<float>(ui.h);
    // Ortho spans the VIEWPORT in pixels (y down), so the UI rect is expressed
    // inside it rather than the projection being the UI rect - that is what
    // lets a coordinate outside [0,1] land outside the 16:9 box, on screen.
    auto mapX = [&](float n) { return uiX + n * uiW; };
    auto mapY = [&](float n) { return uiY + n * uiH; };

    float bx0, bx1, by0, by1, rby0, rby1;
    barRects(UiConfig::getInstance().getGlProbeX(), UiConfig::getInstance().getGlProbeY(),
             bx0, bx1, by0, by1, rby0, rby1);

    g.Color4f(1.0f, 0.0f, 1.0f, 1.0f);
    g.Begin(GL_QUADS);
    g.Vertex2f(mapX(bx0), mapY(by0));
    g.Vertex2f(mapX(bx0), mapY(by1));
    g.Vertex2f(mapX(bx1), mapY(by1));
    g.Vertex2f(mapX(bx1), mapY(by0));
    g.End();
    s.st.drew = true;

    // --- Phase 1 measurement load ----------------------------------------
    // N quads in-context, the GL counterpart of renderProbeQuads. Geometry and
    // alpha come from the renderProbe* keys so a sweep of one is directly
    // comparable to a sweep of the other; taking them from separate keys would
    // let the two sides silently measure different shapes.
    drawMeasurementLoad(s, mapX, mapY);

    // --- did it land? -----------------------------------------------------
    // glReadPixels reads the READ framebuffer. When the game has left read and
    // draw pointing at different targets, a readback would silently sample the
    // wrong surface and report a false negative — so it is skipped and said so
    // rather than guessed at.
    if (verify) {
        const bool sameTarget =
            (s.st.drawFramebuffer == s.st.readFramebuffer) || s.st.glVersion < 30;
        GLint vp[4] = { 0, 0, 0, 0 };
        g.GetIntegerv(GL_VIEWPORT, vp);
        if (sameTarget && vp[2] > 0 && vp[3] > 0) {
            // Same mapping as the draw, or the readback samples the wrong pixel
            // and reports a false negative on a non-16:9 client.
            const UiViewport::Rect rui = UiViewport::compute(vp[2], vp[3]);
            const float cx = static_cast<float>(rui.x) +
                             (bx0 + bx1) * 0.5f * static_cast<float>(rui.w);
            const float cy = static_cast<float>(rui.y) +
                             (by0 + by1) * 0.5f * static_cast<float>(rui.h);
            const GLint px = vp[0] + static_cast<GLint>(cx);
            // GL window coordinates put the origin bottom-left; cy is top-down.
            const GLint py = vp[1] + static_cast<GLint>(static_cast<float>(vp[3]) - cy);
            unsigned char rgba[4] = { 0, 0, 0, 0 };
            g.PixelStorei(GL_PACK_ALIGNMENT, 1);
            g.ReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            // Thresholded, not exact: an sRGB-encoded or 10-bit framebuffer can
            // shift the values without meaning the draw failed. The raw numbers
            // are logged either way, so a near miss is still readable.
            s.st.readbackMatched = (rgba[0] > 200 && rgba[1] < 80 && rgba[2] > 200);
            {
                // The engine's rect here is where the engine WOULD put its bar if
                // it mapped normalized coordinates the way UiViewport does - a
                // prediction, not a measurement. Only the screenshot says where
                // it actually landed, which is exactly the comparison being set up.
                DEBUG_INFO_F("GlProbe: viewport pixels - GL(magenta) x %d..%d y %d..%d drawn; "
                             "engine(cyan) x %d..%d y %d..%d EXPECTED if the mappings agree. "
                             "Compare against the screenshot: same x range and flush in y = agree",
                             static_cast<int>(uiX + bx0 * uiW), static_cast<int>(uiX + bx1 * uiW),
                             static_cast<int>(uiY + by0 * uiH), static_cast<int>(uiY + by1 * uiH),
                             static_cast<int>(uiX + bx0 * uiW), static_cast<int>(uiX + bx1 * uiW),
                             static_cast<int>(uiY + rby0 * uiH), static_cast<int>(uiY + rby1 * uiH));
                DEBUG_INFO_F("GlProbe: readback at (%d,%d) = R%u G%u B%u A%u -> %s",
                             px, py, rgba[0], rgba[1], rgba[2], rgba[3],
                             s.st.readbackMatched ? "PROBE QUAD IS IN THE FRAMEBUFFER"
                                                  : "probe color NOT found");
            }
        } else {
            DEBUG_WARN_F("GlProbe: readback skipped (draw fb=%d, read fb=%d, viewport %dx%d) - "
                         "cannot confirm the draw landed without reading the wrong surface",
                         s.st.drawFramebuffer, s.st.readFramebuffer, vp[2], vp[3]);
        }
    }

    // Did the MEASUREMENT LOAD paint? Separate question from the probe bar, and
    // the one that decides whether a sweep row means anything. Read at the load
    // quad's centre; it is drawn at UI-rect (0,0)-(0.01,0.01) with alpha 64, so
    // a few dozen overlapping quads saturate it to white.
    if (verify && UiConfig::getInstance().getGlProbeQuads() >= 32) {
        GLint lvp[4] = { 0, 0, 0, 0 };
        g.GetIntegerv(GL_VIEWPORT, lvp);
        const bool sameTarget2 =
            (s.st.drawFramebuffer == s.st.readFramebuffer) || s.st.glVersion < 30;
        if (sameTarget2 && lvp[2] > 0 && lvp[3] > 0) {
            const UiViewport::Rect lui = UiViewport::compute(lvp[2], lvp[3]);
            const bool fs = UiConfig::getInstance().getRenderProbeFullscreen();
            const float half = fs ? 0.5f : 0.005f;
            const float lx = static_cast<float>(lui.x) + half * static_cast<float>(lui.w);
            const float ly = static_cast<float>(lui.y) + half * static_cast<float>(lui.h);
            unsigned char lp[4] = { 0, 0, 0, 0 };
            g.PixelStorei(GL_PACK_ALIGNMENT, 1);
            g.ReadPixels(lvp[0] + static_cast<GLint>(lx),
                         lvp[1] + static_cast<GLint>(static_cast<float>(lvp[3]) - ly),
                         1, 1, GL_RGBA, GL_UNSIGNED_BYTE, lp);
            s.st.loadPainted = (lp[0] > 200 && lp[1] > 200 && lp[2] > 200) ? 1 : 0;
            DEBUG_INFO_F("GlProbe: measurement load N=%d batch=%d -> readback R%u G%u B%u : %s",
                         UiConfig::getInstance().getGlProbeQuads(),
                         UiConfig::getInstance().getGlProbeBatch(),
                         lp[0], lp[1], lp[2],
                         s.st.loadPainted == 1 ? "PAINTED"
                                               : "DREW NOTHING - this row's timing is meaningless");
        }
    }

    // --- restore ----------------------------------------------------------
    g.MatrixMode(GL_MODELVIEW);  g.PopMatrix();
    g.MatrixMode(GL_PROJECTION); g.PopMatrix();
    g.MatrixMode(GL_TEXTURE);    g.PopMatrix();
    if (g.UseProgram && prevProgram != 0) g.UseProgram(static_cast<GLuint>(prevProgram));
    if (clientPushed) g.PopClientAttrib();
    g.PopAttrib();

    // --- was the restore airtight? ---------------------------------------
    if (verify) {
        glprobe::Fingerprint after =
            glprobe::capture(s.st.glVersion, s.st.compatProfile, reader);
        std::vector<std::string> diffs;
        s.st.stateDiffs = glprobe::diff(before, after, diffs);

        int errs = 0;
        while (g.GetError() != GL_NO_ERROR_ && errs < 32) ++errs;
        s.st.glErrors = errs;

        {
            if (s.st.stateDiffs == 0 && errs == 0) {
                DEBUG_INFO_F("GlProbe: state restored clean (%d values compared, 0 differ, "
                             "0 GL errors)", before.used);
            } else {
                DEBUG_WARN_F("GlProbe: state NOT clean after restore - %d of %d values differ, "
                             "%d GL errors. Each difference below is a bit that would corrupt "
                             "the game's next draw:", s.st.stateDiffs, before.used, errs);
                for (const std::string& d : diffs) DEBUG_WARN_F("GlProbe:   %s", d.c_str());
            }
        }
    }
}

}  // namespace

void onDraw(int iState) {
    const int mode = UiConfig::getInstance().getGlProbe();
    if (mode <= 0) return;

    ProbeState& s = state();
    s.st.ran = true;
    // Before the context work: the cadence question is worth answering even on a
    // build where no context is current, and at mode 1 as much as mode 2.
    heartbeat(s, iState);
    if (!resolveCore(s)) {
        if (!s.reported) {
            s.reported = true;
            DEBUG_WARN_F("GlProbe: %s - the GL in-context renderer premise does not hold "
                         "for this game (see plans/gl_in_context_renderer.md).",
                         s.st.moduleResident
                             ? "opengl32.dll is resident but its entry points did not resolve"
                             : "opengl32.dll is NOT loaded in this process");
        }
        return;
    }

    sample(s);

    // Extension lookup needs a current context, so it waits for one.
    if (s.st.contextCurrent && !s.extResolved) {
        s.extResolved = true;
        s.gl.UseProgram = reinterpret_cast<decltype(s.gl.UseProgram)>(
            reinterpret_cast<void*>(s.gl.wglGetProcAddress("glUseProgram")));
        s.gl.BindBuffer = reinterpret_cast<decltype(s.gl.BindBuffer)>(
            reinterpret_cast<void*>(s.gl.wglGetProcAddress("glBindBuffer")));
        s.gl.BindVertexArray = reinterpret_cast<decltype(s.gl.BindVertexArray)>(
            reinterpret_cast<void*>(s.gl.wglGetProcAddress("glBindVertexArray")));
    }

    describeOnce(s);

    if (mode < 2 || !s.st.contextCurrent || !s.st.compatProfile) return;

    // Each sweep step changes the load, and each step is its own experiment -
    // so re-arm the checks rather than verifying only the session's first
    // frames. Without this the sweep's GL rows carry no engagement check at all.
    const int loadN = UiConfig::getInstance().getGlProbeQuads();
    if (loadN != s.lastLoadN) {
        s.lastLoadN = loadN;
        s.verifyFramesLeft = 3;
        s.st.loadPainted = -1;
    }
    const bool verify = s.verifyFramesLeft > 0;
    if (verify) --s.verifyFramesLeft;
    drawProbeQuad(s, verify);
}

Status status() { return state().st; }

void referenceBarRect(float& x0, float& y0, float& x1, float& y1) {
    float bx0, bx1, gy0, gy1, ry0, ry1;
    barRects(UiConfig::getInstance().getGlProbeX(), UiConfig::getInstance().getGlProbeY(),
             bx0, bx1, gy0, gy1, ry0, ry1);
    x0 = bx0; x1 = bx1; y0 = ry0; y1 = ry1;
}

}  // namespace GlProbe

#else   // !_WIN32

namespace GlProbe {
void onDraw(int) {}
Status status() { return Status{}; }
void referenceBarRect(float& x0, float& y0, float& x1, float& y1) { x0 = y0 = x1 = y1 = 0.0f; }
}  // namespace GlProbe

#endif
