// ============================================================================
// tests/integration/tests/gl_render_test.cpp
// The in-context GL backend's actual RENDERED OUTPUT (core/hud_gl_renderer.h).
//
// This is coverage no GPU backend in this repo has ever had. hud_gpu_renderer.h
// spent its whole life saying such a thing "needs a real device AND a display,
// and headless CI has neither" - which turned out to be inherited rather than
// tested. Under Xvfb the harness makes a real GL context on the thread that
// calls draw(), so the backend can be driven and its pixels read back.
//
// What that buys, precisely: this is Mesa/llvmpipe under Wine, not an AMD or
// NVIDIA driver, so it is weak evidence about any particular player's machine
// and strong evidence about the things that are the same everywhere - did the
// quad land where the coordinate mapping says, is the colour the colour we
// asked for rather than a swizzle, does the later primitive win the z-order,
// does the GL_ALPHA/MODULATE text path put ink on the screen at all, does the
// context come back exactly as it was found, and - the only one of these that
// raises no GL error, so the backend cannot fall back on its own - does a
// shader program the game left bound hijack our vertices. Those are the
// failures that would otherwise cost a game launch each to discover.
//
// Without a display these cases can do nothing and say so, rather than passing
// silently. Xvfb recipe is in gl_probe_test.cpp's header.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <chrono>
#include <string>
#include <thread>

namespace {
// Sample points are PERCENTAGES of the real GL viewport: the probe renders at
// whatever size the context actually is, so the test never has to know the
// harness window's dimensions. Getting that wrong is what made the first run
// report a quad "bleeding past its right edge" when it was correctly placed.
constexpr int kW = 0, kH = 0;   // unused by the probe; kept for signature shape
// 0xRRGGBBAA -> components, so a failure message names the colour it got.
int R(int p) { return (p >> 24) & 0xFF; }
int G(int p) { return (p >> 16) & 0xFF; }
int B(int p) { return (p >> 8) & 0xFF; }
std::string hex(int p) { char b[16]; snprintf(b, sizeof(b), "0x%08X", p); return b; }

// Scenario 2 draws a large green "III" at the top-left. Text is the sensitive
// instrument for anything that disturbs TEXCOORDS: a glyph's UVs are a
// sub-rectangle of the atlas, so a coordinate that is merely wrong lands on
// empty atlas and the text vanishes, where a full-texture sprite sampled at one
// constant texel still comes back roughly its own colour. That asymmetry is not
// incidental - it is exactly how the field bug presented, as a HUD whose panels
// looked right and whose glyphs and icons were solid blocks.
bool textInkAppears(PluginHost& host) {
    for (int x = 1; x < 30; ++x)
        for (int y = 5; y < 95; y += 3) {
            const int p = host.glRenderProbe(kW, kH, x, y, 2);
            if (p != -1 && G(p) > 150 && R(p) < 100) return true;
        }
    return false;
}
}  // namespace

TEST_CASE("gl render: a quad lands where the coordinate mapping says, in its own colour") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment - the GL backend cannot be exercised");
        return;
    }

    // Scenario 0: an opaque RED quad covering x 0..0.5, full height.
    // Inside it, near the left edge:
    const int inside = host.glRenderProbe(kW, kH, 10, 50, 0);
    REQUIRE_MESSAGE(inside != -1, "GL backend failed to render");
    CHECK_MESSAGE(R(inside) > 200, "expected red inside the quad, got " << hex(inside));
    CHECK(G(inside) < 60);
    CHECK(B(inside) < 60);

    // Outside it, past the halfway line - must NOT be painted. This is the half
    // that catches a mapping that is right about colour and wrong about extent.
    const int outside = host.glRenderProbe(kW, kH, 90, 50, 0);
    REQUIRE(outside != -1);
    CHECK_MESSAGE(R(outside) < 60, "quad bled past its right edge, got " << hex(outside));
}

TEST_CASE("gl render: colour is not swizzled") {
    // The batcher passes m_ulColor through as raw bytes and GL reads them as
    // RGBA. A red/blue swap here would tint the entire HUD and is exactly the
    // kind of thing that looks fine until someone notices every warning is blue.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }

    // The quad is 0xFF0000FF in the game's ABGR packing = opaque RED.
    const int p = host.glRenderProbe(kW, kH, 10, 50, 0);
    REQUIRE(p != -1);
    CHECK_MESSAGE(R(p) > B(p), "red and blue look swapped: " << hex(p));
}

TEST_CASE("gl render: the later primitive wins, so z-order survives batching") {
    // Scenario 1 draws red then blue, both full-screen. Submission order IS
    // z-order for this HUD; if run coalescing ever reordered primitives, a
    // panel would swallow its own labels and nothing else would notice.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }

    const int p = host.glRenderProbe(kW, kH, 50, 50, 1);
    REQUIRE(p != -1);
    CHECK_MESSAGE(B(p) > 200, "expected the LATER (blue) quad on top, got " << hex(p));
    CHECK(R(p) < 60);
}

TEST_CASE("gl render: the fixed-function text path puts ink on the screen") {
    // The GL backend has no shaders: the D3D text shader's
    // float4(col.rgb, coverage * col.a) is reproduced by a GL_ALPHA texture
    // under GL_MODULATE. That equivalence is the least obvious claim in the
    // backend, so it gets checked against a real driver rather than reasoned
    // about. A large green "III" at the top-left must colour SOME pixel green.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }

    bool foundInk = false;
    int sample = 0;
    for (int x = 1; x < 30 && !foundInk; ++x) {
        for (int y = 5; y < 95; y += 3) {
            const int p = host.glRenderProbe(kW, kH, x, y, 2);
            if (p == -1) continue;
            if (G(p) > 150 && R(p) < 100) { foundInk = true; sample = p; break; }
        }
    }
    CHECK_MESSAGE(foundInk, "no green text pixel found - the GL_ALPHA/MODULATE "
                            "text path drew nothing (last sample " << hex(sample) << ")");
}

TEST_CASE("gl render: the game's context comes back exactly as it was found") {
    // The backend is a guest in someone else's context, so this is the property
    // that matters most: a leaked bit corrupts the GAME's next draw, not ours.
    // The probe's own fingerprint is the instrument - same 51 sampled values
    // Phase 0 used, now applied after a full HUD-shaped render rather than a
    // single test quad.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }

    host.glProbe(2);
    for (int i = 0; i < 4; ++i) host.draw();
    REQUIRE(host.glProbeStatus(3) == 1);            // contextCurrent

    // Render through the GL backend, then let the probe re-measure the context.
    REQUIRE(host.glRenderProbe(kW, kH, 1, 1, 1) != -1);
    for (int i = 0; i < 4; ++i) host.draw();

    CHECK_MESSAGE(host.glProbeStatus(8) == 0,       // stateDiffs
                  "the GL backend leaked state into the context");
    CHECK_MESSAGE(host.glProbeStatus(9) == 0, "GL errors after rendering");
    host.glProbe(0);
}

TEST_CASE("gl in-game: suppression follows what drew, never the setting") {
    // The invariant that matters more than any performance number, and the same
    // one the overlay window pins: with the flag on but the backend unable to
    // run, the engine MUST keep getting the frame. A HUD that vanishes because
    // an experimental renderer could not start is a far worse outcome than one
    // that is merely drawn the slow way.
    //
    // The harness has no GL context on its Draw thread unless a test makes one,
    // so this case exercises the cannot-run path naturally - which is exactly
    // the path a player on a non-GL or unusual driver would take.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");

    host.draw();
    const int baseQuads = host.lastGameQuads();
    REQUIRE(baseQuads > 0);

    host.glInGame(true);
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK_MESSAGE(host.lastGameQuads() == baseQuads,
                  "the engine stopped receiving the frame even though the GL "
                  "backend could not run");
    CHECK(host.glDrewLastFrame() == false);

    host.glInGame(false);
    host.draw();
    CHECK(host.lastGameQuads() == baseQuads);
}

TEST_CASE("gl in-game: with a live context it draws, and the engine gets nothing") {
    // The other direction. Once the backend genuinely renders, the engine must
    // be handed an empty frame - that is where the whole saving comes from, and
    // a version that drew in-context AND handed the engine its primitives would
    // be strictly slower than not doing it at all, while looking correct.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment - cannot exercise the drawing path");
        return;
    }

    host.draw();
    const int baseQuads = host.lastGameQuads();
    REQUIRE(baseQuads > 0);

    host.glInGame(true);
    host.draw();
    if (!host.glDrewLastFrame()) {
        MESSAGE("GL backend declined to draw here; the fallback case above covers that");
        return;
    }
    CHECK_MESSAGE(host.lastGameQuads() == 0,
                  "GL drew the frame but the engine was still handed primitives");
    CHECK(host.lastGameStrings() == 0);

    // And turning it off restores the engine handoff on the very next frame.
    host.glInGame(false);
    host.draw();
    CHECK(host.lastGameQuads() == baseQuads);
}

TEST_CASE("gl render: a shader program bound by the game does not hijack our draw") {
    // THE ONE FAILURE THIS BACKEND CANNOT FEEL. Everything else that goes wrong
    // raises a GL error, and render() turns any GL error into a clean fallback
    // to engine drawing. A bound shader program raises NOTHING: fixed-function
    // processing is simply replaced, our vertices go through the GAME's shader,
    // and the HUD comes out garbage or absent. It degrades to WRONG where the
    // rest of the design degrades to SAFE, which is why it is handled and
    // pinned rather than reasoned about.
    //
    // Phase 0 is NOT evidence against this. Its probe explicitly unbound any
    // bound program before drawing its test quad, so its clean result says
    // nothing about whether the game had one bound.
    //
    // The planted program paints pure BLUE, so if the backend ever stops
    // unbinding, the red quad below arrives blue and this fails loudly rather
    // than subtly.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }
    if (!host.glBindHijackingProgram()) {
        MESSAGE("this context cannot compile shaders - nothing to simulate");
        return;
    }

    // Scenario 0 asks for an opaque RED quad over the left half.
    const int p = host.glRenderProbe(0, 0, 10, 50, 0);
    REQUIRE_MESSAGE(p != -1, "backend declined to render with a program bound");
    const int r = (p >> 24) & 0xFF, g = (p >> 16) & 0xFF, b = (p >> 8) & 0xFF;
    CHECK_MESSAGE(r > 200, "expected RED; got R" << r << " G" << g << " B" << b
                           << " - the game's shader is painting our vertices");
    CHECK_MESSAGE(b < 60, "that is the planted shader's blue, not our colour");
}

TEST_CASE("gl render: the frame carries render names, not the game's asset paths") {
    // THE BUG NO PIXEL TEST ABOVE COULD SEE, and the reason this case exists at
    // a different altitude from the rest of the file.
    //
    // HudManager keeps two shapes of the same table. m_fontNames/m_spriteNames
    // are full paths with extensions ("mxbmrp3_data\\fonts\\X.fnt") because that
    // is what the game's DrawInit wants. hudsw::Frame wants RENDER names, which
    // the backend joins to its own asset root. renderInContextGl handed over the
    // former, so every lookup missed, the batcher skipped the primitive, and no
    // GL error was raised anywhere: the HUD lost all text and every textured
    // sprite while untextured panel fills kept drawing perfectly.
    //
    // Every render case above passes with that bug present, because the probe
    // builds its own frame with a hand-written basename and so never exercises
    // the line that was wrong. A test is only evidence for the code it actually
    // routes through.

    // Stage a real font through the plugin's OWN user-override sync: drop it in
    // savePath\\mxbmrp3\\fonts\\ before Startup and syncUserAssets mirrors it into
    // the discovery tree. Copying into plugins/ by hand instead skips the sync
    // and yields discovery-without-registration, which is indistinguishable from
    // a renderer bug - it cost a reviewer an afternoon before this test existed.
    const std::string save = "Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\";
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\\\fonts").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\\\icons").c_str(), nullptr);
    REQUIRE_MESSAGE(CopyFileA(MXB_REPO_DATA_DIR "/fonts/IBMPlexMono-Regular.fnt",
                              (save + "mxbmrp3\\\\fonts\\\\IBMPlexMono-Regular.fnt").c_str(),
                              FALSE) != 0,
                    "could not stage a font - the font half would be vacuous");
    // An icon too: SPRITES are half of what this bug destroyed, and a loop over
    // an empty table asserts nothing. The screenshot that started this was
    // missing its icons exactly as it was missing its glyphs.
    REQUIRE_MESSAGE(CopyFileA(MXB_REPO_DATA_DIR "/icons/angle-up.tga",
                              (save + "mxbmrp3\\\\icons\\\\angle-up.tga").c_str(),
                              FALSE) != 0,
                    "could not stage an icon - the sprite half would be vacuous");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(save.c_str());

    const int nFonts = host.glFrameAssetCount(0);
    // Not a skip: the staging above is what makes this case mean anything, so a
    // zero here is a broken test rather than an environment without fonts.
    REQUIRE_MESSAGE(nFonts > 0, "no fonts registered despite staging one");

    for (int i = 0; i < nFonts; ++i) {
        const std::string n = host.glFrameAssetName(0, i);
        CHECK_MESSAGE(n.find(".fnt") == std::string::npos,
                      "font render name still carries its extension: '" << n << "'");
        CHECK_MESSAGE(n.find('\\') == std::string::npos,
                      "font render name still carries a path separator: '" << n << "'");
        CHECK_MESSAGE(n.find("mxbmrp3_data") == std::string::npos,
                      "font render name still carries the resource root: '" << n << "'");
    }

    // Sprites may legitimately keep a RELATIVE path ("themes/rounded/corner"),
    // so only the extension and the root are wrong for them - not the slash.
    const int nSprites = host.glFrameAssetCount(1);
    REQUIRE_MESSAGE(nSprites > 0, "no sprites registered despite staging one");
    for (int i = 0; i < nSprites; ++i) {
        const std::string n = host.glFrameAssetName(1, i);
        CHECK_MESSAGE(n.find(".tga") == std::string::npos,
                      "sprite render name still carries its extension: '" << n << "'");
        CHECK_MESSAGE(n.find("mxbmrp3_data") == std::string::npos,
                      "sprite render name still carries the resource root: '" << n << "'");
    }
}

TEST_CASE("gl in-game: the probe wins, so its paired bars stay engine-vs-GL") {
    // glProbe=2 draws ONE bar into the GL context and asks the ENGINE to draw a
    // matching one flush beneath it; the pair is the whole method, because two
    // mappings agreeing is the only thing that says our coordinates are right.
    //
    // glInGame=1 suppresses the engine frame - and the engine's reference bar is
    // IN that frame, so it would be drawn through the GL backend too. The
    // comparison silently becomes GL against GL: still looks like agreement,
    // proves nothing. Third instance on this branch of an instrument that
    // removes what it is measuring against and reports success, which is why it
    // is pinned rather than left to the checklist.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    host.glMakeContext(true);          // whether this succeeds is not the point here

    host.glProbe(2);
    host.glInGame(true);
    for (int i = 0; i < 4; ++i) host.draw();

    CHECK_MESSAGE(!host.glDrewLastFrame(),
                  "glInGame drew while glProbe=2 - the probe's engine-drawn "
                  "reference bar is being routed through GL, so its alignment "
                  "comparison is measuring GL against itself");
    CHECK_MESSAGE(host.lastGameQuads() > 0,
                  "the engine got an empty frame, so the reference bar cannot appear");
    host.glInGame(false);
    host.glProbe(0);
}

TEST_CASE("gl in-game: glDrewLastFrame cannot report a stale true") {
    // The hook is read to decide whether suppression was legitimate, so a stale
    // true is a hook that lies - the same family as the sweep that reported a
    // draw which never happened. It is cleared by renderInContextGl, which is
    // why both Draw call sites now call that unconditionally instead of gating
    // on a non-empty frame.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }

    host.glInGame(true);
    for (int i = 0; i < 4; ++i) host.draw();
    const bool drewWhileOn = host.glDrewLastFrame();

    // Turning it off must be reflected on the very next frame, not carried over.
    host.glInGame(false);
    for (int i = 0; i < 2; ++i) host.draw();
    CHECK_MESSAGE(!host.glDrewLastFrame(),
                  "glDrewLastFrame stayed true after the backend stopped drawing"
                  << (drewWhileOn ? "" : " (and it had not drawn to begin with)"));
}

TEST_CASE("gl render: a NESTED pack sprite resolves and paints") {
    // Found in-game, not here, and that is the point of the case existing now.
    //
    // Render names come in two shapes. A flat asset is a bare basename that
    // resolves under /textures/ or /icons/; a NESTED pack asset - themes,
    // gamepads, pitboards, gauges - keeps a relative path ("gauges/classic/tacho")
    // and resolves against the asset root directly. hudassets::spritePath is
    // renderName's inverse and handles both. The GL backend hand-rolled the join
    // and implemented only the flat half, so icons and flat textures rendered
    // perfectly while EVERY pack's art silently failed to load: no GL error, no
    // log line, just a pit board, a gamepad and two dials that were not there.
    //
    // Every other render case above draws untextured quads through the 1x1 white
    // texture, so not one of them touches path resolution. That is why five
    // passing pixel tests said nothing about it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }

    // Scenario 3 draws the shipped gauges/classic/tacho full-screen in white, so
    // the texel passes through unmodulated. The dial face is not uniform, so we
    // sample a few points and require SOMETHING to have been painted - with the
    // path bug the quad is dropped entirely and every sample is background.
    bool painted = false;
    int last = 0;
    for (int pct = 30; pct <= 70 && !painted; pct += 10) {
        const int p = host.glRenderProbe(0, 0, pct, pct, 3);
        if (p == -1) continue;
        last = p;
        if (((p >> 24) & 0xFF) || ((p >> 16) & 0xFF) || ((p >> 8) & 0xFF)) painted = true;
    }
    CHECK_MESSAGE(painted, "nested pack sprite drew nothing - spritePath's nested "
                           "case is not being used (last sample 0x" << std::hex << last << ")");
}

TEST_CASE("gl render: a context change abandons stale caches and keeps drawing") {
    // FOUND IN-GAME. Changing resolution destroys the game's GL context and
    // makes a new one. Every texture name the backend cached belonged to the
    // dead context, so the HUD came back as white blocks.
    //
    // The RELOAD_CONFIG hotkey appeared to fix it, which is what made the shape
    // of the bug legible: art-reload drops textures but deliberately KEEPS
    // fonts, so textures returned and glyphs did not. The hotkey was masking
    // half of a problem it was never the answer to.
    //
    // Caches are abandoned, never glDeleteTextures'd - those names mean nothing
    // in the new context and may already belong to the GAME's textures, so
    // deleting them would corrupt the game's rendering. This case would pass
    // either way; the reason is recorded here because only review can catch it.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }

    // Populate the caches: a nested sprite and a glyph atlas.
    REQUIRE(host.glRenderProbe(0, 0, 50, 50, 3) != -1);
    REQUIRE(host.glRenderProbe(0, 0, 1, 1, 2) != -1);

    // The resolution change: tear the context down and make a fresh one. The
    // renderer instance survives, exactly as it does in the game.
    host.glMakeContext(false);
    REQUIRE_MESSAGE(host.glMakeContext(true), "could not recreate the GL context");

    // Textures must come back...
    bool painted = false;
    for (int pct = 30; pct <= 70 && !painted; pct += 10) {
        const int p = host.glRenderProbe(0, 0, pct, pct, 3);
        if (p == -1) continue;
        if (((p >> 24) & 0xFF) || ((p >> 16) & 0xFF) || ((p >> 8) & 0xFF)) painted = true;
    }
    CHECK_MESSAGE(painted, "nested sprite did not survive a context change");

    // ...and so must GLYPHS, which are the half the reload hotkey never fixed.
    bool ink = false;
    for (int x = 1; x < 30 && !ink; ++x)
        for (int y = 5; y < 95; y += 3) {
            const int p = host.glRenderProbe(0, 0, x, y, 2);
            if (p == -1) continue;
            if (((p >> 16) & 0xFF) > 150 && ((p >> 24) & 0xFF) < 100) { ink = true; break; }
        }
    // HONEST LIMITATION: the must-catch run (context handling disabled) failed on
    // `painted` but NOT on this one - under Wine/llvmpipe the font atlas name
    // happened to stay usable across the swap, where in-game BOTH broke. So the
    // texture half is proven to catch the regression and the glyph half is not.
    // It stays because it is the half the reload hotkey never fixed, and because
    // a driver that invalidates names more aggressively will exercise it.
    CHECK_MESSAGE(ink, "no glyph ink after a context change - the font atlas cache "
                       "is still holding names from the dead context");
}

TEST_CASE("gl in-game: works on the pluginThread path too, which is its own call site") {
    // Worth its own case rather than trusting the sync one, for two reasons.
    //
    // First, the THREADED branch of PluginManager::handleDraw has a SEPARATE
    // renderInContextGl call site from the legacy one - different guards,
    // different frame source (the worker's triple-buffered frame), and it is the
    // branch that also has to clear glDrewLastFrame when no frame was ready.
    //
    // Second, this combination is where the data race lived: requestGlArtReload
    // reaches m_glRenderer from processKeyboardInput, which produceFrame runs on
    // the WORKER thread in this mode, while renderInContextGl writes it from the
    // game thread. That is why those members are atomic, and this is the only
    // configuration in which the two threads both exist.
    //
    // The GL context stays on THIS thread - the one calling draw() - because a
    // context is per-thread and the worker has none. That is the whole reason
    // the render happens in handleDraw rather than inside produceFrame.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }

    host.pluginThreadEnable();
    REQUIRE_MESSAGE(host.pluginThreadEnabled(), "worker did not start");
    host.glInGame(true);

    // Several frames: the first requests, later ones take what the worker built.
    for (int i = 0; i < 12; ++i) { host.draw(); host.pluginThreadFlush(); }

    CHECK_MESSAGE(host.glDrewLastFrame(),
                  "the GL backend never drew on the pluginThread path");
    CHECK_MESSAGE(host.lastGameQuads() == 0,
                  "engine got " << host.lastGameQuads() << " quads while GL drew - "
                  "suppression is not following what drew on the threaded branch");

    // The art-reload gesture is what reaches m_glRenderer from the worker.
    // It must not crash, and drawing must continue afterwards.
    host.reloadAssetLayouts();
    for (int i = 0; i < 6; ++i) { host.draw(); host.pluginThreadFlush(); }
    CHECK_MESSAGE(host.glDrewLastFrame(), "stopped drawing after a reload on the worker path");

    host.glInGame(false);
    host.pluginThreadStop();
}

TEST_CASE("gl render: somebody else's queued GL error does not latch us off") {
    // FOUND IN THE FIELD, by a ReShade user. ReShade installs itself AS
    // opengl32.dll and does its own GL work; it left a GL_INVALID_ENUM in the
    // queue before our Draw. GL errors are a QUEUE and glGetError pops one at a
    // time, so our end-of-render check read that error, concluded we had
    // corrupted the context, and latched the backend off for the whole session.
    //
    // The report said gl_ingame=1 gl_drew=0 - the setting on, the backend never
    // drawing - which is the only reason this was diagnosable at all rather than
    // looking like "the feature does nothing on my machine".
    //
    // The fix drains the queue on entry so the check at the bottom means what it
    // says: an error raised BY OUR CALLS. Without it, any injected layer that is
    // not scrupulous about its own errors disables the feature silently.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) {
        MESSAGE("no GL context in this environment");
        return;
    }
    if (!host.glPlantStaleError()) {
        MESSAGE("could not plant a GL error here");
        return;
    }

    // Scenario 0 asks for an opaque RED quad. With the stale error inherited,
    // render() returns false and NOTHING is drawn - so a red pixel proves both
    // that we drew and that we did not blame ourselves for someone else's error.
    const int p = host.glRenderProbe(0, 0, 10, 50, 0);
    REQUIRE_MESSAGE(p != -1, "backend refused to render because of an error it did not cause");
    CHECK_MESSAGE(((p >> 24) & 0xFF) > 200,
                  "expected the red quad; a stale error from another GL user "
                  "latched the backend off");
}

TEST_CASE("gl render: the texture units the game left behind do not reach our draw") {
    // THE FIELD BUG, and the second failure this backend cannot feel. A bound
    // shader program (above) was the first; this is the same shape - no GL
    // error, so no fallback, so the HUD is simply wrong - and it shipped.
    //
    // Every texturing call the backend makes is PER-UNIT, and "unit 0" was an
    // assumption about the game rather than something the backend stated. Two
    // ways that goes wrong, planted together because a game leaves them
    // together:
    //   - CLIENT_ACTIVE_TEXTURE on unit 1. glTexCoordPointer feeds the CLIENT-
    //     active unit, so our UVs go to a unit that is not drawing and unit 0
    //     samples ONE CONSTANT TEXEL for the whole frame. Text is what catches
    //     it; a solid-colour quad cannot, because the 1x1 white texture it
    //     draws through is its own only texel.
    //   - Unit 1 ENABLED, here with a solid BLUE texture. Fixed-function applies
    //     every enabled unit in turn, so the red quad arrives black unless the
    //     backend turned that unit off.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }
    if (!host.glPlantSilentState(1)) {
        MESSAGE("this context has no second texture unit - nothing to be wrong about");
        return;
    }

    const int p = host.glRenderProbe(kW, kH, 10, 50, 0);
    REQUIRE_MESSAGE(p != -1, "backend declined to render with hostile texture units");
    CHECK_MESSAGE(R(p) > 200, "expected RED; got " << hex(p) << " - unit 1 is still "
                              "enabled and modulating our fragments with its texture");
    CHECK_MESSAGE(textInkAppears(host),
                  "no text pixel - our texcoords went to the client-active unit the "
                  "game left selected, so every quad sampled one constant texel");
}

TEST_CASE("gl render: fixed-function state we depend on is state we set") {
    // The rest of the same class, one planted state per subcase so a failure
    // names the cap rather than "something in the pipeline". None of these
    // raises a GL error either; the backend used to inherit whatever the game
    // had and hope.
    struct Case {
        int  mask;
        const char* what;
        bool checkQuad;   // this one corrupts a solid quad
        bool checkText;   // this one corrupts texcoords, so only text sees it
    };
    const Case cases[] = {
        { 2, "texgen (generated UVs replace the ones we supplied)", false, true },
        { 4, "fog (every fragment dragged toward the fog colour)",  true,  false },
        { 8, "GL_COLOR_LOGIC_OP (replaces blending outright)",      true,  false },
        { 16, "glPolygonMode(GL_LINE) (a wireframe HUD is still silent)", true, false },
        { 32, "a clip plane (takes the whole frame away)",          true,  false },
    };
    // Three of the backend's disables are deliberately NOT pinned here, because
    // a plant this driver ignores would pass against a broken backend and that
    // is worse than no case at all:
    //   - GL_POLYGON_STIPPLE: an all-zero stipple, which must draw nothing, is
    //     ignored by llvmpipe - the quad comes back fully red with the disable
    //     removed. Measured, not assumed.
    //   - GL_SAMPLE_ALPHA_TO_COVERAGE and its two neighbours, and the
    //     GL_MULTISAMPLE the backend ENABLES beside them: all four need a
    //     multisample framebuffer, which the probe's context does not have. The
    //     enable is the one piece of inherited state the backend wants rather
    //     than removes - it is what gives the map ribbon's angled quads edge
    //     antialiasing - and it is unverified here for that reason, not
    //     overlooked.
    // Every other row below was verified must-catch by removing its disable from
    // the backend and watching this case fail.
    for (const Case& c : cases) {
        // std::string, not the raw const char*: doctest streams the pointer
        // through a bool conversion and the label prints as "1".
        const std::string what(c.what);
        CAPTURE(what);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_render\\\\");
        if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }
        if (!host.glPlantSilentState(c.mask)) {
            MESSAGE("this context cannot express: " << what);
            continue;
        }
        if (c.checkQuad) {
            const int p = host.glRenderProbe(kW, kH, 10, 50, 0);
            REQUIRE_MESSAGE(p != -1, "backend declined to render under: " << what);
            CHECK_MESSAGE(R(p) > 200, "expected RED, got " << hex(p) << ", under: " << what);
        }
        if (c.checkText)
            CHECK_MESSAGE(textInkAppears(host), "no text pixel under: " << what);
    }
}

TEST_CASE("gl confirm: silence reverts the setting, so nobody is stranded on a broken renderer") {
    // THE FAILURE THIS EXISTS FOR. Direct GL's bad day is not a blank screen, it
    // is a WRONG one: a tester's build drew panels correctly while every glyph
    // came out a solid block, leaving a HUD that was still laid out and still
    // clickable and completely unreadable. He could not find the row to turn it
    // back off. Recovery meant hand-editing the INI.
    //
    // So the prompt is a dead-man's switch: confirming is deliberate, and doing
    // nothing reverts. The user who cannot read the question is exactly the user
    // who has to end up back on engine rendering.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_confirm\\\\");
    if (!host.hasGlConfirm()) { MESSAGE("build without the confirm hooks"); return; }

    host.glInGame(true);
    host.glConfirmArm(true);
    CHECK(host.glConfirmActive());
    CHECK(host.glConfirmPct() == 100);

    // Time only counts while frames are being DRAWN - see tickDrawn. Halfway
    // through, it is still up and the setting is untouched.
    host.glConfirmTick(4000);
    CHECK(host.glConfirmActive());
    CHECK_MESSAGE(host.glConfirmPct() < 100, "the countdown did not move");
    CHECK_MESSAGE(host.glConfirmPct() > 0, "it expired at the halfway mark");
    CHECK_MESSAGE(host.glInGameEnabled(), "reverted before the timer ran out");

    // Past the end: gone, and the setting is back off.
    host.glConfirmTick(8000);
    CHECK_MESSAGE(!host.glConfirmActive(), "the prompt outlived its own countdown");
    CHECK_MESSAGE(!host.glInGameEnabled(),
                  "the timer ran out and Direct GL is STILL on - this is the "
                  "stranded user the prompt exists to rescue");
}

TEST_CASE("gl confirm: the prompt goes THROUGH Direct GL, so it is its own test") {
    // The design, and the one an earlier version of this file asserted the
    // opposite of. That version routed the prompt to the engine so it would stay
    // readable whatever GL did - which makes it render perfectly however badly GL
    // is misdrawing, so clicking "Keep" proves nothing, and a player whose HUD is
    // all gauges and no text has nothing to judge by at all.
    //
    // A prompt asking "can you read this?" has to be drawn by the thing being
    // asked about. The observable: with GL drawing, the engine's arrays come back
    // EMPTY whether or not the prompt is up - because the prompt went through GL
    // with everything else.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_confirm\\\\");
    if (!host.glMakeContext(true)) { MESSAGE("no GL context here"); return; }
    if (!host.hasGlConfirm()) { MESSAGE("build without the confirm hooks"); return; }

    host.glInGame(true);
    host.glConfirmArm(true);
    for (int i = 0; i < 4; ++i) host.draw();
    REQUIRE_MESSAGE(host.glDrewLastFrame(), "the prompt stopped GL drawing the HUD");
    CHECK_MESSAGE(host.lastGameQuads() == 0,
                  "the engine got " << host.lastGameQuads() << " quads while GL drew - "
                  "the prompt is being drawn by the engine, which would make it "
                  "render correctly however badly GL is failing");
}

TEST_CASE("gl confirm: the escape needs no readable text") {
    // WHY DRAWING IT THROUGH GL IS SAFE. The observed failure took glyphs and
    // icons while quads kept drawing correctly, so the prompt must stay usable
    // with every string on it reduced to mush: a shrinking BAR and a coloured
    // button, both quads, are what the user actually steers by.
    //
    // Pinned as "the panel emits quads of its own", which a countdown rendered as
    // a number of seconds - the obvious way to write it - would fail.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_confirm\\\\");
    if (!host.hasGlConfirm()) { MESSAGE("build without the confirm hooks"); return; }

    host.glConfirmArm(true);
    host.draw();
    PluginHost::PanelRect r = host.hudPanelRect(PluginHost::HUD_GL_CONFIRM);
    CHECK_MESSAGE(r.quads > 0, "the prompt drew no quads at all - with the glyph path "
                               "broken there would be nothing on screen to steer by");
    CHECK(r.w > 0);
    CHECK(r.h > 0);
    host.glConfirmArm(false);
}

TEST_CASE("gl confirm: the prompt reaches the screen on the pluginThread path too") {
    // THE BUG THIS CASE COMES FROM, and it was found in-game rather than here.
    // The prompt's first design routed its primitives to the engine from
    // PluginManager::handleDraw - but only from the SYNC branch. The threaded
    // branch returns several lines earlier, so with [Advanced] pluginThread on
    // (which the author runs) the panel was built every frame and drawn by
    // nothing: the settings menu correctly stood down, the countdown correctly
    // ran out and reverted, and in between there was simply nothing on screen to
    // click. A dead-man's switch nobody can see is worse than none.
    //
    // Drawing it as an ORDINARY HUD is what fixes that, structurally: it is in the
    // frame the worker builds, so both call sites carry it without either knowing
    // it exists. This case exists to keep it that way.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_confirm\\\\");
    if (!host.hasGlConfirm()) { MESSAGE("build without the confirm hooks"); return; }

    host.pluginThreadEnable();
    REQUIRE_MESSAGE(host.pluginThreadEnabled(), "worker did not start");

    // The worker builds on its OWN clock, so draws alone do not advance it - a
    // tight loop of forty returned an empty frame every time. Sleep between them,
    // as plugin_thread_latency_test does.
    auto settle = [&] {
        for (int i = 0; i < 12; ++i) {
            host.draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    };

    settle();
    const int without = host.lastGameQuads();

    host.glConfirmArm(true);
    settle();
    const int with = host.lastGameQuads();

    PluginHost::PanelRect r = host.hudPanelRect(PluginHost::HUD_GL_CONFIRM);
    CHECK_MESSAGE(r.quads > 0,
                  "the prompt built nothing on the worker - on the threaded path it "
                  "would be invisible, which is exactly how it shipped the first time");
    // The DIFFERENCE is the assertion: a frame that merely happens to be non-empty
    // proves nothing, since other HUDs are in it too. Arming the prompt has to make
    // the worker's frame grow by the prompt's own quads.
    CHECK_MESSAGE(with >= without + r.quads,
                  "arming the prompt added " << (with - without) << " quads to the "
                  "worker's frame but the panel itself has " << r.quads
                  << " - it is not reaching the frame the game is handed");
    host.glConfirmArm(false);
}

TEST_CASE("gl confirm: no frames means no time, so it cannot expire behind a menu") {
    // The requirement, stated the way it actually holds: the countdown advances
    // from update(), update() is only reached from produceFrame, and produceFrame
    // is only reached from the game's Draw callback. The game issues no Draw in
    // menus, so a player who opens one does not come back to a reverted setting.
    //
    // This replaced a version that gated on HudManager::glDrewLastFrame(), which
    // looked like the same question and is not: that flag is cleared on every
    // entry to renderInContextGl and set only when it draws, so on the
    // pluginThread path - where the game thread calls it every frame, empty
    // frames included - most samples read false and each threw its interval away.
    // It measured perfectly here and crawled in game, which is why this case now
    // asserts the property by WITHHOLDING FRAMES rather than by withholding GL.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\\\tmp\\\\mxbmrp3-tests\\\\gl_confirm\\\\");
    if (!host.hasGlConfirm()) { MESSAGE("build without the confirm hooks"); return; }

    host.glConfirmArm(true);
    host.draw();                     // one frame to establish the clock
    REQUIRE(host.glConfirmPct() == 100);

    // Real time passes; no Draw does. Nothing may move.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK_MESSAGE(host.glConfirmPct() == 100,
                  "the countdown fell to " << host.glConfirmPct() << "% without a single "
                  "Draw - in a menu it would run out behind the pause screen and revert "
                  "a setting nobody was asked about");

    // Frames resume: it must now move, and at the real rate rather than a
    // fraction of it - the bug this case was rewritten for made it advance, just
    // roughly a hundred times too slowly.
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 150; ++i) {
        host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const float wall =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    const int expected = 100 - static_cast<int>(wall * 100.0f / 10.0f);   // 10s timeout
    const int got = host.glConfirmPct();
    const bool onRate = (got <= expected + 6) && (got >= expected - 6);
    CHECK_MESSAGE(onRate,
                  "after " << wall << "s of frames the countdown reads " << got
                  << "% where it should read about " << expected
                  << "% - it is running at the wrong rate, which is how ten seconds "
                  "became minutes in game while measuring perfectly here");
    host.glConfirmArm(false);
}

