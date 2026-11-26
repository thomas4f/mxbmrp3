// ============================================================================
// tests/integration/tests/hud_scroll_test.cpp
// A HUD LIST'S SCROLLBACK (hud/hud_scroll.h), driven through the real Event
// Log with the injected mouse -- what the unit test of the arithmetic cannot
// see:
//   * with the pointer away there is no bar, and the newest events show;
//   * pointer up and over the HUD, each click on the bar's top arrow moves
//     the rows back one event, a click just beside the thin bar included;
//   * scrolled back, a new event does not move the rows, and the caption
//     says "1 new";
//   * held there, pointer up and still, the HUD stops rebuilding -- the bar
//     costs nothing while nothing moves (the perf ledger runs pointer-down
//     and cannot see this);
//   * the pointer going away takes the list back to the newest events and
//     clears the count -- a HUD is never raced with stuck in old history;
//   * Stream Chat on auto-hide stays up while the bar shows, past its
//     timeout, and fades once the pointer goes;
//   * with Motion on, a press GLIDES the rows (the settings lists' glide):
//     mid-way one row more is drawn, cut at the list's one frame on the
//     companion and faded on the game's frame, which carries no cut; landed,
//     neither cuts.
//   * THE WHEEL (core/mouse_wheel.h), on the harness's stand-in for the game
//     window: pointer up and on the HUD, a notch away from the player moves
//     the rows back one event, as the top arrow does, and never reaches the
//     game; with the cursor gone idle, or the pointer away, every notch is
//     the game's, and with no list to scroll the window has its own
//     procedure back.
//   * the wheel goes to the HUD on TOP under the pointer: with the settings
//     panel open over a scrollable Event Log, a notch moves the panel's
//     list, never the log beneath it.
// The bar sits in the panel's right padding, between the card and the
// panel's edge. Self-contained doctest; see
// run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
// The Event Log's director rows, top to bottom (each toggle logs one).
std::vector<PluginHost::DrawnText> directorRows(PluginHost& host) {
    std::vector<PluginHost::DrawnText> out;
    for (const PluginHost::DrawnText& t : host.lastStrings()) {
        if (t.alpha > 0 && t.text.rfind("Auto-director", 0) == 0) out.push_back(t);
    }
    std::sort(out.begin(), out.end(),
              [](const PluginHost::DrawnText& a, const PluginHost::DrawnText& b) { return a.y < b.y; });
    return out;
}

bool drawn(PluginHost& host, const char* text) {
    for (const PluginHost::DrawnText& t : host.lastStrings())
        if (t.alpha > 0 && t.text == text) return true;
    return false;
}
}  // namespace

TEST_CASE("hud scroll: the event log scrolls back, holds while events arrive, and returns") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_scroll\\");
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.eventLogSetVisible(true);
    host.eventLogEnableDirector(true);
    REQUIRE(host.setHudOffset("event_log_hud", 0.0f, 0.0f));   // build space == screen
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "R10");
    host.draw();
    // Twelve director events, alternating, so a one-row move shows in the text.
    for (int i = 0; i < 12; ++i) {
        host.directorSetEnabled(i % 2 == 0);
        host.draw();
    }
    host.draw();

    const std::vector<PluginHost::DrawnText> newest = directorRows(host);
    REQUIRE(newest.size() >= 3);
    const float lh = newest[1].y - newest[0].y;
    REQUIRE(lh > 0.0f);
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("event_log_hud", &card, &panel));
    REQUIRE(panel.r > card.r);
    const float barX = static_cast<float>(card.r + panel.r) * 0.5f;
    // The list's top row: the oldest shown (oldest-first order), director rows last.
    const std::vector<PluginHost::DrawnText> all = host.lastStrings();
    float top = newest.front().y;
    for (const PluginHost::DrawnText& t : all)
        if (t.alpha > 0 && t.y > static_cast<float>(card.t) && t.y < top && t.x < barX - 0.05f) top = t.y;

    // Pointer up, over the HUD: a press on the top arrow moves the rows one
    // event (the texts alternate, so each one-row move shows); a second, one more.
    host.injectMouse(true, static_cast<float>(card.l) + 0.01f, newest.front().y, 0);
    host.draw();
    const auto pressUp = [&](float x) {
        host.injectMouse(true, x, top + lh * 0.5f, 0); host.draw();
        host.injectMouse(true, x, top + lh * 0.5f, 1); host.draw();
        host.injectMouse(true, x, top + lh * 0.5f, 0); host.draw();
    };
    pressUp(barX);
    // The bar is beside the card, never on it: a list whose content runs to the
    // card's edge (the Lap Log's times) is not covered.
    bool barSeen = false;
    double barL = 1.0;
    for (const PluginHost::QuadRect& q : host.hudQuadRects("event_log_hud")) {
        if (q.r - q.l < 0.02 && q.b - q.t > lh && q.l > card.l + (card.r - card.l) * 0.5) {
            barSeen = true;
            barL = std::min(barL, q.l);
            CHECK(q.l >= card.r - 1e-4);
            CHECK(q.r <= panel.r + 1e-4);
        }
    }
    CHECK(barSeen);
    const std::vector<PluginHost::DrawnText> one = directorRows(host);
    REQUIRE(one.size() == newest.size());
    CHECK(one.front().text != newest.front().text);
    CHECK(one.back().text != newest.back().text);   // the newest is off the foot
    // The bar is thin, but its hit strip is wider: the second press lands
    // just left of it, off the drawn bar (its right reaches the panel's edge,
    // past which the click is no longer the HUD's).
    pressUp(static_cast<float>(barL) - 0.0015f);
    const std::vector<PluginHost::DrawnText> back = directorRows(host);
    REQUIRE(back.size() == newest.size());
    CHECK(back.front().text != one.front().text);

    // An event arrives: the rows hold, and the caption counts it.
    host.directorSetEnabled(newest.back().text == "Auto-director disabled");
    host.draw();
    host.draw();
    const std::vector<PluginHost::DrawnText> held = directorRows(host);
    REQUIRE(held.size() == back.size());
    for (size_t i = 0; i < held.size(); ++i) CHECK(held[i].text == back[i].text);
    CHECK(drawn(host, "1 new"));

    // Pointer up and still, nothing arriving: no rebuild, frame after frame.
    // The rebuild counter only counts while the profiler collects, which the
    // Benchmark widget being shown turns on.
    host.bmSetVisible(true);
    host.draw();
    const int rebuilds = host.hudRebuildCount("event_log_hud");
    REQUIRE(rebuilds >= 0);
    for (int i = 0; i < 20; ++i) host.draw();
    CHECK(host.hudRebuildCount("event_log_hud") == rebuilds);
    // ...and the counter is live: a press on the bar does rebuild it.
    pressUp(barX);
    CHECK(host.hudRebuildCount("event_log_hud") > rebuilds);

    // The pointer goes away: back at the newest, the count gone.
    host.injectMouse(false);
    host.draw();
    host.draw();
    const std::vector<PluginHost::DrawnText> returned = directorRows(host);
    REQUIRE(!returned.empty());
    // Three events on from where it held (two scrolled back, one arrived); the
    // texts alternate, so an odd move shows.
    CHECK(returned.back().text != held.back().text);
    CHECK_FALSE(drawn(host, "1 new"));
    host.shutdown();
}

namespace {
const char* kChatSave = "Z:\\tmp\\mxbmrp3-tests\\hud_scroll\\";
const char* kChatIni = "Z:\\tmp\\mxbmrp3-tests\\hud_scroll\\mxbmrp3\\mxbmrp3_settings.ini";

void setStreamChatKey(std::string& text, const std::string& key, const std::string& value) {
    const size_t sec = text.find("[StreamChat]");
    REQUIRE(sec != std::string::npos);
    const size_t at = text.find("\n" + key + "=", sec);
    REQUIRE(at != std::string::npos);
    const size_t end = text.find('\n', at + 1);
    text.replace(at + 1, end - at - 1, key + "=" + value);
}
}  // namespace

TEST_CASE("hud scroll: stream chat on auto-hide stays up while scrolled back, and fades after") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kChatSave);
        host.shutdown();
    }
    std::string text = ini::readFile(kChatIni);
    setStreamChatKey(text, "showMode", "2");      // auto-hide
    setStreamChatKey(text, "autoHideMs", "1000");
    REQUIRE(ini::writeFile(kChatIni, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kChatSave);
    REQUIRE(host.hasTwitchHooks());
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.twitchSimulateConnected();
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible("stream_chat_hud", true));
    REQUIRE(host.setHudOffset("stream_chat_hud", 0.0f, 0.0f));
    for (int i = 0; i < 30; ++i) {
        std::string raw = "@badges=;color=;display-name=viewer;id=m" + std::to_string(i) +
                          ";tmi-sent-ts=1700000000000 :viewer!viewer@viewer.tmi.twitch.tv PRIVMSG #chan :line " +
                          std::to_string(i);
        host.twitchInject(raw.c_str());
    }
    host.draw();
    host.draw();
    REQUIRE(host.chatRowCount() > 0);

    // Pointer up over the panel: the bar shows (the list is longer than the view).
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("stream_chat_hud", &card, &panel));
    const float cx = static_cast<float>(card.l + card.r) * 0.5f;
    const float cy = static_cast<float>(card.t + card.b) * 0.5f;
    host.injectMouse(true, cx, cy, 0);
    host.draw();
    host.draw();

    // Past the timeout with the bar up: still there.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    host.draw();
    CHECK(host.chatRowCount() > 0);

    // The pointer goes: the hold ends and the timed-out panel fades.
    host.injectMouse(false);
    host.draw();
    host.draw();
    CHECK(host.chatRowCount() == 0);
    host.shutdown();
}

TEST_CASE("hud scroll: with Motion on a press glides, cut on the companion and faded in game") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_scroll_glide\\");
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    REQUIRE(host.hasFrameClips());
    REQUIRE(host.hasMotion());
    const long long t0 = 1000000000ll;
    host.setMotion(2);
    host.setMotionNowUs(t0);
    host.companionWindow(true);
    // The press is on the game's copy, so pin the game surface: the companion
    // window is a real window here and can take the pointer once a frame runs
    // slow, and the press then lands in its space (failed at random on a busy
    // host).
    host.forceActiveSurface(0);
    REQUIRE(host.setHudVisibleBoth("dash_hud", false));
    REQUIRE(host.setHudVisibleBoth("event_log_hud", true));
    host.eventLogEnableDirector(true);
    REQUIRE(host.setHudOffset("event_log_hud", 0.0f, 0.0f));
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "R10");
    host.draw();
    for (int i = 0; i < 12; ++i) {
        host.directorSetEnabled(i % 2 == 0);
        host.draw();
    }
    // The HUD's own fade-in settles first.
    for (int i = 1; i <= 10; ++i) { host.setMotionNowUs(t0 + i * 100000ll); host.draw(); }
    const long long t1 = t0 + 1000000ll;

    const std::vector<PluginHost::DrawnText> newest = directorRows(host);
    REQUIRE(newest.size() >= 3);
    const float lh = newest[1].y - newest[0].y;
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("event_log_hud", &card, &panel));
    const float barX = static_cast<float>(card.r + panel.r) * 0.5f;
    const float top = static_cast<float>(card.t) + lh * 0.5f;
    host.injectMouse(true, static_cast<float>(card.l) + 0.01f, newest.front().y, 0);
    host.draw();
    CHECK(host.frameClips(true).empty());   // at rest on a whole row: nothing cut

    // A press on the top arrow, then half way through the glide.
    host.injectMouse(true, barX, top, 1); host.draw();
    host.injectMouse(true, barX, top, 0); host.draw();
    host.setMotionNowUs(t1 + 90000ll);
    host.draw();
    host.draw();

    // The game's frame never carries a cut; rows crossing the edge fade there.
    CHECK(host.frameClips(false).empty());
    int faded = 0;
    for (const PluginHost::DrawnText& t : host.lastStrings())
        if (t.alpha > 0 && t.alpha < 255 && t.text.rfind("Auto-director", 0) == 0) ++faded;
    CHECK(faded >= 1);
    // The companion's cuts the rows at ONE frame: the list's view.
    const auto cuts = host.frameClips(true);
    REQUIRE(cuts.size() >= 1);
    for (size_t i = 0; i < cuts.size(); ++i) {
        if (i > 0) CHECK(cuts[i].q0 >= cuts[i - 1].q1);
        CHECK(cuts[i].t == doctest::Approx(cuts[0].t));
        CHECK(cuts[i].b == doctest::Approx(cuts[0].b));
        CHECK(cuts[i].b - cuts[i].t == doctest::Approx(lh * static_cast<float>(newest.size())).epsilon(0.05));
    }

    // Landed: whole rows again, nothing cut and nothing faded.
    host.setMotionNowUs(t1 + 1000000ll);
    host.draw();
    host.draw();
    CHECK(host.frameClips(true).empty());
    for (const PluginHost::DrawnText& t : host.lastStrings())
        if (t.text.rfind("Auto-director", 0) == 0) CHECK((t.alpha == 0 || t.alpha == 255));
    host.setMotion(0);
    host.setMotionNowUs(-1);
    host.shutdown();
}


TEST_CASE("hud scroll: the wheel moves the event log under a showing cursor, and is the game's otherwise") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_scroll_wheel\\");
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    REQUIRE_MESSAGE(host.useWheelStandIn(), "build without the wheel hooks, or no X server");
    host.forceActiveSurface(0);
    host.eventLogSetVisible(true);
    // The Position widget sits over the log's corner at offset 0, and the HUD
    // on top owns the wheel.
    REQUIRE(host.setHudVisible("position_widget", false));
    host.eventLogEnableDirector(true);
    REQUIRE(host.setHudOffset("event_log_hud", 0.0f, 0.0f));   // build space == screen
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "R10");
    host.draw();
    for (int i = 0; i < 12; ++i) {
        host.directorSetEnabled(i % 2 == 0);
        host.draw();
    }
    host.draw();
    const std::vector<PluginHost::DrawnText> newest = directorRows(host);
    REQUIRE(newest.size() >= 3);
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("event_log_hud", &card, &panel));
    constexpr int NOTCH = 120;   // WHEEL_DELTA

    // Pointer away: no list to scroll, the game window untouched.
    CHECK(PluginHost::gameOwnsWheelWindow());

    // Pointer up and on the HUD: ours in front, a notch away from the player
    // one event back (the texts alternate), and the game never sees it.
    const float onX = static_cast<float>(card.l) + 0.01f, onY = newest.front().y;
    host.injectMouse(true, onX, onY, 0);
    host.draw();
    CHECK((host.wheelState() & 1) == 1);
    const int game0 = PluginHost::wheelGameCount();
    CHECK(PluginHost::wheelSend(NOTCH) == game0);
    host.draw();
    const std::vector<PluginHost::DrawnText> one = directorRows(host);
    REQUIRE(one.size() == newest.size());
    CHECK(one.front().text != newest.front().text);
    // ...and a notch toward the player, back to the newest.
    PluginHost::wheelSend(-NOTCH);
    host.draw();
    const std::vector<PluginHost::DrawnText> again = directorRows(host);
    REQUIRE(again.size() == newest.size());
    CHECK(again.front().text == newest.front().text);
    CHECK(PluginHost::wheelGameCount() == game0);

    // The cursor gone idle (hidden) over the HUD: the game's, the rows stay.
    host.cursorIdle();
    host.draw();
    host.draw();
    CHECK(PluginHost::wheelSend(NOTCH) == game0 + 1);
    host.draw();
    CHECK(directorRows(host).front().text == newest.front().text);

    // The pointer away: the game's, and the window has its own procedure back.
    host.injectMouse(false);
    host.draw();
    CHECK(PluginHost::gameOwnsWheelWindow());
    CHECK(PluginHost::wheelSend(NOTCH) == game0 + 2);
    host.shutdown();
}


TEST_CASE("hud scroll: the wheel over the settings panel moves its list, not the event log beneath") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_scroll_wheel_under\\");
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    REQUIRE_MESSAGE(host.useWheelStandIn(), "build without the wheel hooks, or no X server");
    host.forceActiveSurface(0);
    host.eventLogSetVisible(true);
    host.eventLogEnableDirector(true);
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "R10");
    host.draw();
    for (int i = 0; i < 12; ++i) {
        host.directorSetEnabled(i % 2 == 0);
        host.draw();
    }
    constexpr int ACHIEVEMENTS = 2;   // ScrollView::ACHIEVEMENTS
    constexpr int NOTCH = 120;        // WHEEL_DELTA
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    REQUIRE(v.maxRest > 0.0f);
    host.stepScroll(ACHIEVEMENTS, 3);   // somewhere to go back up to
    const float down = host.settingsScroll(ACHIEVEMENTS).target;
    REQUIRE(down > 0.0f);

    // The Event Log moved under the panel's list (its rect is pre-offset), the
    // pointer on both.
    REQUIRE(host.setHudOffset("event_log_hud", 0.0f, 0.0f));
    host.draw();
    PluginHost::QuadRect card, panel;
    REQUIRE(host.hudCardRect("event_log_hud", &card, &panel));
    const float px = v.x + v.w * 0.5f, py = v.y + v.h * 0.5f;
    const float ox = px - static_cast<float>(panel.l + panel.r) * 0.5f;
    const float oy = py - static_cast<float>(panel.t + panel.b) * 0.5f;
    REQUIRE(host.setHudOffset("event_log_hud", ox, oy));
    host.injectMouse(true, px, py, 0);
    host.draw();
    host.draw();
    REQUIRE(host.hudCardRect("event_log_hud", &card, &panel));
    REQUIRE(px > panel.l + ox);
    REQUIRE(px < panel.r + ox);
    REQUIRE(py > panel.t + oy);
    REQUIRE(py < panel.b + oy);
    const std::vector<PluginHost::DrawnText> before = directorRows(host);
    REQUIRE(before.size() >= 2);
    CHECK((host.wheelState() & 1) == 1);

    // A notch away from the player: the panel's list goes up, the log stays.
    const int game0 = PluginHost::wheelGameCount();
    CHECK(PluginHost::wheelSend(NOTCH) == game0);
    host.draw();
    host.draw();
    CHECK(host.settingsScroll(ACHIEVEMENTS).target < down);
    const std::vector<PluginHost::DrawnText> after = directorRows(host);
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < after.size(); ++i) CHECK(after[i].text == before[i].text);
    host.injectMouse(false);
    host.shutdown();
}
