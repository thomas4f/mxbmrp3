// ============================================================================
// tests/unit/asset_path_test.cpp
// Regression test for AssetPath::renderName (core/asset_path.h).
//
// THE BUG. This function used to flatten every registered resource path to its
// final component, because the flat asset folders (fonts/ textures/ icons/) were
// the only kind that existed. When panel themes added a NESTED folder
// (themes/<name>/frame_corner_tl.tga), every theme's sprites collapsed to
// "frame_corner_tl"/"frame_edge_top"/"frame_center", the renderer could not find them, and drawQuad
// returned without drawing -- so every themed HUD background rendered as NOTHING.
//
// It was invisible to everything except a screenshot: a missing sprite draws
// nothing rather than raising, the rest of the frame renders normally, and the
// capture tool's blank-frame guard stays happy. The unit suite, the geometry
// tests and the build were all green with the feature completely broken.
//
// Hence this file. renderName was moved out of an anonymous namespace in
// companion_window.cpp specifically so the rule could be pinned here.
// ============================================================================
#include "doctest.h"

#include "core/asset_path.h"

using AssetPath::renderName;

TEST_CASE("renderName: flat asset folders reduce to the bare base name") {
    // The renderer picks fonts/ vs textures/ vs icons/ itself from the sprite
    // index, so these must NOT carry a path.
    CHECK(renderName("mxbmrp3_data\\fonts\\RobotoMono-Regular.fnt") == "RobotoMono-Regular");
    CHECK(renderName("mxbmrp3_data\\textures\\standings_hud_1.tga") == "standings_hud_1");
    CHECK(renderName("mxbmrp3_data\\icons\\hud-laplog.tga") == "hud-laplog");
}

TEST_CASE("renderName: nested assets keep their relative path") {
    // THE REGRESSION. Flattening these to "frame_corner_tl"/"frame_edge_top"/"frame_center" is what made
    // every panel theme render as nothing at all.
    CHECK(renderName("mxbmrp3_data\\themes\\debug\\frame_corner_tl.tga") == "themes/debug/frame_corner_tl");
    CHECK(renderName("mxbmrp3_data\\themes\\debug\\frame_edge_left.tga") == "themes/debug/frame_edge_left");
    CHECK(renderName("mxbmrp3_data\\themes\\neon\\frame_center.tga") == "themes/neon/frame_center");

    // Two themes must not collide, which is exactly what flattening caused.
    CHECK(renderName("mxbmrp3_data\\themes\\a\\frame_corner_tl.tga")
          != renderName("mxbmrp3_data\\themes\\b\\frame_corner_tl.tga"));
}

TEST_CASE("renderName: separators are normalised") {
    // The game registers backslash paths; the renderer builds forward-slash ones.
    CHECK(renderName("mxbmrp3_data/themes/debug/frame_corner_tl.tga") == "themes/debug/frame_corner_tl");
    CHECK(renderName("mxbmrp3_data\\themes\\debug\\frame_corner_tl.tga") == "themes/debug/frame_corner_tl");
}

TEST_CASE("renderName: only a real extension is stripped") {
    // A dot in a DIRECTORY name must not be mistaken for an extension.
    CHECK(renderName("mxbmrp3_data\\themes\\my.theme\\frame_corner_tl.tga") == "themes/my.theme/frame_corner_tl");
    // No extension at all is left alone.
    CHECK(renderName("mxbmrp3_data\\icons\\plain") == "plain");
}

TEST_CASE("renderName: tolerates paths without the resource-root prefix") {
    // Defensive: callers pass whatever was registered, and a bare name is valid.
    CHECK(renderName("RobotoMono-Regular.fnt") == "RobotoMono-Regular");
    CHECK(renderName("icons\\hud-map.tga") == "hud-map");
    // A deeper path with no root prefix still keeps its shape.
    CHECK(renderName("themes\\debug\\frame_corner_tl.tga") == "themes/debug/frame_corner_tl");
}

TEST_CASE("renderName: an absolute install path still reduces correctly") {
    // The prefix search is a find(), not a rfind(0), so a full path works.
    CHECK(renderName("C:\\Games\\MX Bikes\\plugins\\mxbmrp3_data\\fonts\\Tiny5-Regular.fnt")
          == "Tiny5-Regular");
    CHECK(renderName("C:\\Games\\MX Bikes\\plugins\\mxbmrp3_data\\themes\\debug\\frame_edge_top.tga")
          == "themes/debug/frame_edge_top");
}
