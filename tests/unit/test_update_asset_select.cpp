// ============================================================================
// tests/unit/test_update_asset_select.cpp
// Guards UpdateChecker::selectAssetIndex — the auto-updater's release-asset
// picker. Regression test for the field bug where the updater downloaded
// "mxbmrp3-symbols-vX.Y.Z.B.zip" (a .pdb/.map bundle with no .dlo) instead of
// "mxbmrp3.zip", so every client's update aborted with "Release not for this
// game". The old logic was "first .zip in the assets array"; GitHub returns the
// symbols zip BEFORE the release zip ('-' < '.'), so it matched first.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/update_checker.h"

#include <string>
#include <vector>

TEST_CASE("update asset select: picks the release zip, never the symbols bundle") {
    // The exact asset set + ordering a real release produces (symbols sorts
    // before mxbmrp3.zip, which is what fooled the old "first .zip" logic).
    const std::vector<std::string> real = {
        "mxbmrp3-Setup.exe",
        "mxbmrp3-symbols-v1.27.3.38.zip",
        "mxbmrp3.cdx.json",
        "mxbmrp3.zip",
    };
    const int idx = UpdateChecker::selectAssetIndex(real);
    REQUIRE(idx >= 0);
    CHECK(real[idx] == "mxbmrp3.zip");   // NOT the symbols zip at index 1
}

TEST_CASE("update asset select: still works when only the release zip is present") {
    // The emergency fix (symbols detached) and older releases: one .zip only.
    CHECK(UpdateChecker::selectAssetIndex({"mxbmrp3-Setup.exe", "mxbmrp3.zip", "mxbmrp3.cdx.json"}) == 1);
    CHECK(UpdateChecker::selectAssetIndex({"mxbmrp3.zip"}) == 0);
}

TEST_CASE("update asset select: falls back to first non-symbols zip when name differs") {
    // Older releases named the archive "mxbmrp3-vX.Y.Z.zip" (see the m_assetName
    // comment). No exact "mxbmrp3.zip", so the non-symbols fallback must catch it
    // and still skip the symbols bundle regardless of order.
    CHECK(UpdateChecker::selectAssetIndex(
              {"mxbmrp3-symbols-v1.10.3.0.zip", "mxbmrp3-v1.10.3.0.zip"}) == 1);
    CHECK(UpdateChecker::selectAssetIndex(
              {"mxbmrp3-v1.10.3.0.zip", "mxbmrp3-symbols-v1.10.3.0.zip"}) == 0);
    // Case-insensitive on the symbols marker.
    CHECK(UpdateChecker::selectAssetIndex({"mxbmrp3-SYMBOLS-v1.zip", "mxbmrp3-v1.zip"}) == 1);
}

TEST_CASE("update asset select: no suitable asset yields -1") {
    // Only a symbols zip / non-zip assets ⇒ nothing installable; never return the
    // symbols bundle as a last resort.
    CHECK(UpdateChecker::selectAssetIndex({"mxbmrp3-symbols-v1.27.3.38.zip"}) == -1);
    CHECK(UpdateChecker::selectAssetIndex({"mxbmrp3-Setup.exe", "mxbmrp3.cdx.json"}) == -1);
    CHECK(UpdateChecker::selectAssetIndex({}) == -1);
}
