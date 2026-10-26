// ============================================================================
// tests/unit/test_media_app_filter.cpp
// Now playing's player filter ([General] nowPlayingApps; core/media_app_filter.h):
// comma-separated names matched case-insensitively anywhere in a session's
// app id, empty = every player. Ids as Windows reports them for the desktop
// and Store Spotify, Firefox and Chrome.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/core/media_app_filter.h"

using MediaAppFilter::matches;

TEST_CASE("media app filter: empty follows every player") {
    CHECK(matches("Spotify.exe", ""));
    CHECK(matches("Spotify.exe", nullptr));
    CHECK(matches("Spotify.exe", " , ,"));
    CHECK(matches("", ""));
}

TEST_CASE("media app filter: a name matches anywhere, in any case") {
    CHECK(matches("Spotify.exe", "spotify"));
    CHECK(matches("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "SPOTIFY"));
    CHECK(matches("308046B0AF4A39CB", "308046b0af4a39cb"));
    CHECK_FALSE(matches("308046B0AF4A39CB", "spotify"));
    CHECK_FALSE(matches("Chrome", "chromium"));   // the whole name, not a prefix of it
    CHECK_FALSE(matches("", "spotify"));
}

TEST_CASE("media app filter: a list matches any of its names, spaces ignored") {
    CHECK(matches("Chrome", "spotify, chrome"));
    CHECK(matches("Spotify.exe", " spotify ,chrome "));
    CHECK_FALSE(matches("308046B0AF4A39CB", "spotify, chrome"));
    CHECK(matches("foobar2000.exe", "spotify,,foobar"));
}
