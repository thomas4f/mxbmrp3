// ============================================================================
// tools/demo_reel/hover_reel.cpp
// The hover-preview reel: plays a Recorder tape (MXBHREC: a real one from the
// game, or tapegen's) into the plugin while the companion window renders it,
// and scripts only the mouse: unfold the Toggles strip, rest on every switch
// that previews something (Session is clicked on), fold, step the Dash to its
// Fuel page, then Settings > Appearance, each from its dropdown: the panel
// theme REEL_KEEP_THEME (kept for the rest of the reel), French and back to
// English, Tomorrow fonts; then the
// Standings Last lap column and opacity 0, and the Gamepad switched on,
// recoloured and shrunk; closed, the pad is dragged from its spot bottom left
// to the bottom right. The pad's sticks and triggers follow the rider's inputs
// on the tape. Each beat names itself with a caption: written
// to captions.tsv (seconds, beat, detail, box as x0,y0,x1,y1; CP1252, as drawn)
// for make_demo.sh to draw.
// REEL_SHORT=1 skips the strip pass, for iterating on the rest; the strip pass
// rests on every switch that previews something, REEL_STRIP=main on the main
// ones only (a 130 s cut).
// REEL_W/REEL_H set the capture size (default 1920x1080); REEL_CAPTION_CHECK=1
// counts frames where anything is drawn under the caption's box.
// The tape's last demoSecs play in real time; everything before them is
// replayed at once, so the HUDs open with the history the tape carries.
//   wine showreel.exe mxbmrp3_test.dlo race.tape [demoSecs]
// Built and run by make_demo.sh. With REEL_CAPTURE set it writes the companion
// frame to capture.rgba (1920x1080, 30 fps) for the script to encode.
// Staging a tape cannot carry: Records (from the records server), Stream Chat
// lines, Steam friends, the welcome notice, Motion, achievements already earned.
//
// PluginHost keeps its tape dispatch private (its replay helpers own the clock),
// and this reel needs to interleave events with its own wall clock, so it opens
// the class with `#define private public` -- a demo tool's shortcut, kept out of
// the test suite.
// ============================================================================
#include <any>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <variant>
#include <array>
#include "nlohmann/json.hpp"
#define private public   // PluginHost::dispatch: the replayer's own event switch
#include "plugin_host.h"
#undef private
#include "tape.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

static float ease(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); }

struct TapeEvent { tape::EventType type; uint64_t us; std::vector<uint8_t> data; };
static bool loadTape(const char* path, std::vector<TapeEvent>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    tape::FileHeader fh{};
    if (fread(&fh, sizeof(fh), 1, f) != 1 || std::memcmp(fh.magic, "MXBHREC", 7) != 0) { fclose(f); return false; }
    tape::EventHeader eh{};
    while (fread(&eh, sizeof(eh), 1, f) == 1) {
        TapeEvent e{ static_cast<tape::EventType>(eh.eventType), eh.timestampUs, std::vector<uint8_t>(eh.dataSize) };
        if (eh.dataSize && fread(e.data.data(), 1, eh.dataSize, f) != eh.dataSize) break;
        out.push_back(std::move(e));
    }
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const char* dll = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* tapePath = argc > 2 ? argv[2] : "demo.tape";
    const double demoSecs = argc > 3 ? std::atof(argv[3]) : 165.0;
    std::vector<TapeEvent> events;
    if (!loadTape(tapePath, events) || events.empty()) { fprintf(stderr, "cannot read tape %s\n", tapePath); return 1; }
    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    auto setNowUs = host.sym<void (*)(long long)>("MXBMRP3_Test_LapTimerSetNowUs");
    if (!setNowUs) { fprintf(stderr, "no lap timer hook\n"); return 1; }

    host.startup("Z:\\tmp\\mxbmrp3-tests\\companion\\");
    // Every achievement row already earned except Leap Forward, which a PB on the
    // tape earns for real.
    host.achievementForceTiers(4, "pb_leaps");
    host.messagesStartup(true, "1.33");   // a fresh install: the welcome card
    // Steam friends (Steam data, not on a tape): staged when the DLL has the demo hook.
    if (auto inject = host.sym<void (*)(const char*, const char*, const char*, const char*, const char*,
                                        const char*, int)>("MXBMRP3_Test_FriendsInject")) {
        inject("Thomas", "MXB Sunday Club", "Southwick", "Race 1", "In Progress", "6 TO GO", 2);   // our own row
        inject("Wilhelmina", "MXB Sunday Club", "Southwick", "Race 1", "In Progress", "6 TO GO", 1);
        inject("dirtdiver_88", "Euro Open #2", "Club MX", "Race 2", "In Progress", "04:12", 0);
        inject("holeshot_hannah", "MXB Sunday Club", "Southwick", "Race 1", "In Progress", "6 TO GO", 1);
        inject("rutmaster", "", "", "", "", "", 0);
    }

    // Replay. A Draw on the tape is a game frame; a thinned tape without them gets
    // one after each telemetry frame, as the game would.
    const uint64_t endUs = events.back().us;
    const uint64_t liveUs = endUs > demoSecs * 1e6 ? endUs - static_cast<uint64_t>(demoSecs * 1e6) : 0;
    const bool tapeDraws = std::any_of(events.begin(), events.end(),
                                       [](const TapeEvent& e) { return e.type == tape::EventType::Draw; });
    constexpr long long kClockBaseUs = 1000000000LL;   // the lap timer's clock, offset from 0
    size_t next = 0;
    auto play = [&](uint64_t untilUs) {
        for (; next < events.size() && events[next].us <= untilUs; ++next) {
            TapeEvent& e = events[next];
            setNowUs(kClockBaseUs + static_cast<long long>(e.us));
            if (e.type == tape::EventType::Draw) { host.drawWithState(0); continue; }
            host.dispatch(e.type, e.data);
            if (!tapeDraws && e.type == tape::EventType::RunTelemetry) host.drawWithState(0);
        }
    };
    // The Gamepad widget's controller, from the rider's inputs on the tape: throttle
    // on the right trigger, front brake on the left, steering on the left stick
    // (from the turn rate, which every tape carries, unlike m_fSteer).
    auto fakePad = host.sym<void (*)(float, float, float, float)>("MXBMRP3_Test_FakeGamepadInput");
    float padSteer = 0, padThrottle = 0, padBrake = 0, lastYaw = 0;
    uint64_t lastYawUs = 0;
    auto padFrom = [&](const TapeEvent& e) {
        if (e.type != tape::EventType::RunTelemetry || e.data.size() < sizeof(SPluginsBikeData_t)) return;
        SPluginsBikeData_t b;
        std::memcpy(&b, e.data.data(), sizeof(b));
        padThrottle = std::clamp(b.m_fThrottle, 0.0f, 1.0f);
        padBrake = std::clamp(b.m_fFrontBrake, 0.0f, 1.0f);
        if (lastYawUs && e.us > lastYawUs) {
            float dy = b.m_fYaw - lastYaw;
            if (dy > 180) dy -= 360;
            if (dy < -180) dy += 360;
            const float rate = dy / static_cast<float>((e.us - lastYawUs) / 1e6);   // degrees a second
            padSteer += (std::clamp(rate / 70.0f, -1.0f, 1.0f) - padSteer) * 0.08f;
        }
        lastYaw = b.m_fYaw; lastYawUs = e.us;
        if (fakePad) fakePad(padSteer, 0.0f, padBrake, padThrottle);
    };
    // The window opens kLeadSecs of tape before filming: the tape plays on in real
    // time meanwhile, so notices from the history run out and nobody stands still.
    constexpr double kLeadSecs = 9.0;
    const uint64_t leadUs = liveUs > kLeadSecs * 1e6 ? liveUs - static_cast<uint64_t>(kLeadSecs * 1e6) : 0;
    play(leadUs);
    fprintf(stderr, "tape %.1f s, live from %.1f s\n", endUs / 1e6, liveUs / 1e6);
    host.helmetOnCompanion(true);
    host.recordsParse(0, R"({"notice":"Data by CBR","records":[
        {"player":"Wilhelmina","bike":"Kawasaki KX450 2023","laptime":94120,"timestamp":"2026-10-12T12:34:56Z"},
        {"player":"Bartholomew","bike":"Honda CRF450R 2023","laptime":94870,"timestamp":"2026-10-15T08:00:00Z"},
        {"player":"Thomas","bike":"KTM 450 SX-F 2023","laptime":95310,"timestamp":"2026-10-20T18:30:00Z"},
        {"player":"Cassiopeia","bike":"Yamaha YZ450F 2023","laptime":95904,"timestamp":"2026-10-21T18:30:00Z"},
        {"player":"Aleksander","bike":"GasGas MC 450F 2024","laptime":96455,"timestamp":"2026-10-22T18:30:00Z"}]})");
    host.twitchSimulateConnected();
    {
        auto line = [](const char* login, const char* text, const char* id) {
            std::string s = "@badges=;color=;display-name="; s += login; s += ";id="; s += id;
            s += ";tmi-sent-ts=1700000000000 :"; s += login; s += "!"; s += login; s += "@"; s += login;
            s += ".tmi.twitch.tv PRIVMSG #chan :"; s += text; return s;
        };
        host.twitchInject(line("trackside_tom", "that pass on 22 though", "c1").c_str());
        host.twitchInject(line("mx_mia", "PB incoming", "c2").c_str());
        host.twitchInject(line("holeshot", "which HUD is that?", "c3").c_str());
    }
    host.setMotion(1);
    // The shipped defaults the test build starts with off.
    host.setDropShadow(true);
    host.setGlow(true);
    host.forceActiveSurface(1);
    host.companionWindow(true);
    {
        const auto l0 = std::chrono::steady_clock::now();
        for (;;) {
            const double e = std::chrono::duration<double>(std::chrono::steady_clock::now() - l0).count();
            const uint64_t at = leadUs + static_cast<uint64_t>(e * 1e6);
            if (at >= liveUs) break;
            for (; next < events.size() && events[next].us <= at; ++next) {
                TapeEvent& ev = events[next];
                if (ev.type == tape::EventType::Draw) continue;
                setNowUs(kClockBaseUs + static_cast<long long>(ev.us));
                host.dispatch(ev.type, ev.data);
                padFrom(ev);
            }
            setNowUs(kClockBaseUs + static_cast<long long>(at));
            host.drawWithState(0);
            Sleep(8);
        }
        play(liveUs);
    }
    host.settleStartupMessages();
    host.forceActiveSurface(1);
    host.injectMouse(true, 0.62f, 0.55f, 0);
    host.cursorIdle();
    host.drawWithState(0);
    fprintf(stderr, "companion window opened\n");

    // ---- the script --------------------------------------------------------
    // Each step runs for `dur` seconds; `target` is resolved when the step starts.
    float mx = 0.62f, my = 0.55f;
    int buttons = 0;
    struct Step {
        enum Kind { WAIT, MOVE, CLICK, RDRAG, ACTION, HOLD, REST, KEEP, PRESS } kind;
        double dur;
        std::function<bool(float&, float&)> target;   // MOVE/RDRAG destination
        std::function<void()> action;
        const char* label;
        std::function<bool()> done;   // PRESS: the button is held until this holds (dur caps it)
        std::function<bool()> skip;   // the step is passed over while this holds
    };
    // Captions: what each beat demonstrates. A caption is a beat name and a
    // detail line; a new detail inside the same beat (a theme's name) is a new
    // entry. `box` is where make_demo.sh's caption layout draws it (kCaptionBoxes),
    // the region the capture checks stays clear.
    struct Caption { double t; std::string beat, detail; int box; };
    std::vector<Caption> captions;
    double nowT = 0;                       // the script's clock, set each loop
    std::atomic<int> captionBox{ -1 };     // the box in use now, for the capture thread
    std::atomic<int> captionCount{ 0 };    // captions so far, for the capture thread
    auto caption = [&](const char* beat, std::string detail, int box = 0) {
        if (!captions.empty() && captions.back().beat == beat && captions.back().detail == detail) return;
        captions.push_back({ nowT, beat, std::move(detail), box });
        captionBox = box;
        captionCount = static_cast<int>(captions.size());
        fprintf(stderr, "%6.2f caption %s: %s\n", nowT, beat, captions.back().detail.c_str());
    };
    auto say = [&](const char* beat, const char* detail, int box = 0) {
        return Step{ Step::ACTION, 0, nullptr, [&caption, beat, detail, box] { caption(beat, detail, box); }, "" };
    };
    auto stripRow = [&](const char* name) {
        return [&host, name](float& x, float& y) {
            int n = host.toggleStripRow(0);
            for (int i = 0; i < n; ++i) {
                std::string nm;
                float tx, ty;
                host.toggleStripRow(i, &tx, &ty, &nm);
                if (nm == name) { x = tx; y = ty; return true; }
            }
            fprintf(stderr, "strip row '%s' not found (%d rows)\n", name, n);
            return false;
        };
    };
    auto region = [&](const char* id) {
        return [&host, id](float& x, float& y) {
            if (host.settingsRegionCenter(id, &x, &y)) return true;
            fprintf(stderr, "region '%s' not found\n", id);
            return false;
        };
    };
    auto at = [](float x, float y) {
        return [x, y](float& ox, float& oy) { ox = x; oy = y; return true; };
    };
    auto rowCenterLeft = [&](const char* id, float dx) {   // a checkbox sits at the row's left
        return [&host, id, dx](float& x, float& y) {
            if (!host.settingsRegionCenter(id, &x, &y)) { fprintf(stderr, "region '%s' not found\n", id); return false; }
            x += dx; return true;
        };
    };
    (void)rowCenterLeft;
    // A cycle row's ">" caret: the first ">" right of the row's centre on its line.
    auto caret = [&](const char* id) {
        return [&host, id](float& x, float& y) {
            float cx, cy;
            if (!host.settingsRegionCenter(id, &cx, &cy)) { fprintf(stderr, "region '%s' not found\n", id); return false; }
            double best = 9;
            for (const auto& r : host.hudStringRows("settings_hud")) {
                if (r.text != ">" || r.x < cx || r.y > cy || r.y < cy - 0.03) continue;
                if (r.x < best) best = r.x;
            }
            if (best > 1) { fprintf(stderr, "caret for '%s' not found\n", id); return false; }
            x = static_cast<float>(best + 0.003); y = cy; return true;
        };
    };
    // The same row's "<" caret: the nearest "<" left of its ">" on that line.
    auto leftCaret = [&](const char* id) {
        return [&host, caret, id](float& x, float& y) {
            float rx, ry;
            if (!caret(id)(rx, ry)) return false;
            double best = -1;
            for (const auto& r : host.hudStringRows("settings_hud")) {
                if (r.text != "<" || r.x >= rx || r.y > ry || r.y < ry - 0.03) continue;
                if (r.x > best) best = r.x;
            }
            if (best < 0) { fprintf(stderr, "left caret for '%s' not found\n", id); return false; }
            x = static_cast<float>(best + 0.003); y = ry; return true;
        };
    };
    float gapX = 0.5f, gapY = 0.2f;
    bool mouseIdle = false;
    float dashX = 0.5f, dashY = 0.05f;
    // Start folded: unfold with the caret, rest on each switch that shows
    // something, pass over the rest, fold again at the bottom.
    std::vector<Step> script;
    bool pastEnd = false;     // the strip has no row this far down
    int baseQuads = -1;       // companion frame quads with nothing previewed
    auto compQuads = [&host] { return host.surfaceFrameStats().companionQuads; };
    script.push_back(say("Default HUDs", "Fresh install, live race"));
    script.push_back({ Step::WAIT, 2.0, nullptr, nullptr, "default screen" });
    script.push_back(say("Toggles strip", "Hover to preview, click to keep"));
    script.push_back({ Step::MOVE, 1.0, stripRow("Settings"), nullptr, "wake the strip" });
    script.push_back({ Step::MOVE, 0.4, stripRow("Expand"), nullptr, "to the caret" });
    script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "expand" });
    script.push_back({ Step::WAIT, 0.7, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { baseQuads = compQuads(); }, "baseline" });
    const bool shortRun = getenv("REEL_SHORT") != nullptr;   // iterate on Settings: skip the strip pass
    for (int i = 2; i < (shortRun ? 2 : 48); ++i) {
        script.push_back({ Step::MOVE, 0.18, [&host, i, &pastEnd](float& x, float& y) {
            if (host.toggleStripRow(i, &x, &y) > i) return true;
            pastEnd = true; return false; }, nullptr, "" });
        // Past the 250 ms rest and the fade-in, then hold longer if something shows.
        script.push_back({ Step::REST, 0.42, nullptr, nullptr, "" });
        script.push_back({ Step::HOLD, 1.5, nullptr, nullptr, "" });
        script.push_back({ Step::KEEP, 0.25, nullptr, nullptr, "" });   // a click on Session only
    }
    script.push_back({ Step::ACTION, 0, nullptr, [&] { pastEnd = false; }, "bottom" });
    script.push_back({ Step::MOVE, 0.9, stripRow("Collapse"), nullptr, "to the caret" });
    script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "collapse" });
    script.push_back({ Step::WAIT, 0.6, nullptr, nullptr, "" });
    // The Dash steps to its Fuel page (Page Down, as bound), a page at a time.
    // The cursor waits just right of the card, beside what it steps through.
    script.push_back(say("Dash pages", "Page Down to the Fuel page"));
    script.push_back({ Step::MOVE, 0.8, [&host](float& x, float& y) {
        double l = 9, t = 9, r = -9, b = -9;
        for (const auto& q : host.hudQuadRects("dash_hud")) {
            l = std::min(l, q.l); t = std::min(t, q.t); r = std::max(r, q.r); b = std::max(b, q.b);
        }
        if (r < l) return false;
        x = static_cast<float>(r + 0.03); y = static_cast<float>((t + b) / 2); return true; },
        nullptr, "dash pages" });
    for (int k = 0; k < 6; ++k) {
        script.push_back({ Step::ACTION, 0, nullptr, [&] {
            if (host.dashPage() != 4) host.injectHotkey("dash_next"); }, "" });   // 4 = DashHud::Page::FUEL
        script.push_back({ Step::WAIT, 0.9, nullptr, nullptr, "" });
    }
    script.push_back({ Step::ACTION, 0, nullptr, [&] { fprintf(stderr, "dash page %d\n", host.dashPage()); }, "" });
    script.push_back({ Step::WAIT, 1.2, nullptr, nullptr, "" });
    // Settings, on the Appearance tab: themes, languages, then fonts.
    script.push_back({ Step::MOVE, 0.8, stripRow("Settings"), nullptr, "to settings" });
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "open settings" });
    script.push_back({ Step::WAIT, 0.6, nullptr, nullptr, "" });
    script.push_back({ Step::MOVE, 0.8, region("appearance"), nullptr, "appearance tab" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 0.7, nullptr, nullptr, "" });
    // From the dropdowns: Tomorrow for titles and digits.
    auto dropdownBox = [&host](const char* id) {
        return [&host, id](float& x, float& y) {
            float x0, yy, w;
            if (!host.settingsDropdownSpan(id, &x0, &yy, &w)) { fprintf(stderr, "dropdown '%s' not found\n", id); return false; }
            x = x0 + w * 0.5f; y = yy; return true;
        };
    };
    // An entry of the list a dropdown opened: the lowest string with that prefix
    // (the list hangs below the row, which may already show the same name).
    auto listItem = [&host](const char* id, const char* prefix) {
        return [&host, id, prefix](float& x, float& y) {
            double bx = -1, byy = -1;
            for (const auto& r : host.hudStringRows("settings_hud")) {
                if (r.text.rfind(prefix, 0) != 0 || r.y <= byy) continue;
                bx = r.x; byy = r.y;
            }
            if (byy < 0) { fprintf(stderr, "list item '%s' not found for %s\n", prefix, id); return false; }
            x = static_cast<float>(bx + 0.02); y = static_cast<float>(byy + 0.006); return true;
        };
    };
    // A control on a settings row: the n-th `glyph` caret (">" or "<") on the line
    // of the string `label` nearest the row's region (strings sit at the line's
    // top, regions at its centre), or with n < 0 the label itself.
    auto rowCaret = [&host](const char* regionId, const char* label, int n, const char* glyph = ">") {
        return [&host, regionId, label, n, glyph](float& x, float& y) {
            float rx, ry;
            if (!host.settingsRegionCenter(regionId, &rx, &ry)) { fprintf(stderr, "region '%s' not found\n", regionId); return false; }
            const auto rows = host.hudStringRows("settings_hud");
            double ly = -1, lx = 0;
            for (const auto& r : rows)
                if (r.text == label && (ly < 0 || std::fabs(r.y - ry) < std::fabs(ly - ry))) { ly = r.y; lx = r.x; }
            if (ly < 0) { fprintf(stderr, "label '%s' not found\n", label); return false; }
            if (n < 0) { x = static_cast<float>(lx + 0.01); y = static_cast<float>(ly + 0.01); return true; }
            std::vector<double> xs;
            for (const auto& r : rows) if (r.text == glyph && std::fabs(r.y - ly) < 0.002 && r.x > lx) xs.push_back(r.x);
            std::sort(xs.begin(), xs.end());
            if (static_cast<int>(xs.size()) <= n) { fprintf(stderr, "caret %d of '%s' not found\n", n, label); return false; }
            x = static_cast<float>(xs[n] + 0.003); y = static_cast<float>(ly + 0.01); return true;
        };
    };
    auto rowDump = [&host](const char* label) {
        double ly = -1;
        const auto rows = host.hudStringRows("settings_hud");
        for (const auto& r : rows) if (r.text == label) ly = r.y;
        std::string line;
        for (const auto& r : rows) if (ly >= 0 && std::fabs(r.y - ly) < 0.002) { line += r.text; line += '|'; }
        fprintf(stderr, "row %s: %s\n", label, line.c_str());
    };
    // A cycle row's shown value: the text between its "<" and ">" carets, found
    // from the row's region (its label may be translated by then).
    auto rowValue = [&](const char* id) -> std::string {
        float rx, ry, lx, ly;
        if (!caret(id)(rx, ry) || !leftCaret(id)(lx, ly)) return std::string();
        double bx = 9;
        std::string v;
        for (const auto& r : host.hudStringRows("settings_hud"))
            if (r.y <= ry && r.y >= ry - 0.03 && r.x > lx && r.x < rx && r.text.size() > 1 && r.x < bx) { bx = r.x; v = r.text; }
        v.erase(v.find_last_not_of(' ') + 1);   // the value box pads to its width
        return v;
    };
    // A pick from a settings dropdown: open it, glide to the entry, click.
    auto pick = [&](const char* row, const char* item, const char* label) {
        script.push_back({ Step::MOVE, 0.7, dropdownBox(row), nullptr, label });
        script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
        script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
        script.push_back({ Step::WAIT, 0.9, nullptr, nullptr, "" });   // the list, open
        script.push_back({ Step::MOVE, 0.8, listItem(row, item), nullptr, item });
        script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
        script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    };
    // A panel theme from the dropdown, with Settings open (the panel and every
    // HUD around it restyle), kept for the rest of the reel.
    {
        const char* keepEnv = getenv("REEL_KEEP_THEME");
        static const std::string keep = keepEnv ? keepEnv : "Modern Dark";
        script.push_back(say("Panel themes", "Pick one from the dropdown"));
        pick("appearance.theme", keep.c_str(), "panel theme");
        script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
        script.push_back({ Step::ACTION, 0, nullptr, [&] { caption("Panel themes", rowValue("appearance.theme")); }, "" });
        script.push_back({ Step::WAIT, 2.5, nullptr, nullptr, "" });
    }
    // A language from the dropdown (French, the native-checked pack), then back
    // to English, whose labels the rest of the script finds its rows by. The
    // list names every language in itself, English always as "English".
    script.push_back(say("Languages", "Pick one from the dropdown"));
    pick("appearance.language", "Fran", "language");
    script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { caption("Languages", rowValue("appearance.language")); }, "" });
    script.push_back({ Step::WAIT, 3.0, nullptr, nullptr, "" });
    script.push_back(say("Languages", "Back to English"));
    pick("appearance.language", "English", "");
    script.push_back({ Step::WAIT, 1.0, nullptr, nullptr, "" });
    // A row's rightmost percentage (Scale on a widget row, Opacity on its own row).
    auto pctOnRow = [&host](const char* label) {
        const auto rows = host.hudStringRows("settings_hud");
        double ly = -1;
        for (const auto& r : rows) if (r.text == label) ly = r.y;
        int pct = -1;
        double px = -1;
        for (const auto& r : rows)
            if (ly >= 0 && std::fabs(r.y - ly) < 0.002 && r.x > px && r.text.find('%') != std::string::npos) {
                px = r.x; pct = std::atoi(r.text.c_str());
            }
        return pct;
    };
    // Fonts after the theme, so it first shows its own: Tomorrow for titles
    // and digits, over it.
    script.push_back(say("Fonts", "Tomorrow over the theme's own"));
    struct DdPick { const char* row; const char* item; };
    for (const DdPick& pk : { DdPick{ "appearance.font_title", "Tomorrow Bold" },
                              DdPick{ "appearance.font_digits", "Tomorrow Bold" } }) {
        script.push_back({ Step::MOVE, 0.7, dropdownBox(pk.row), nullptr, pk.row });
        script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
        script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
        script.push_back({ Step::WAIT, 0.6, nullptr, nullptr, "" });
        script.push_back({ Step::MOVE, 0.7, listItem(pk.row, pk.item), nullptr, pk.item });
        script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
        script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
        script.push_back({ Step::WAIT, 0.9, nullptr, nullptr, "" });
    }
    // Standings: the Last lap column on.
    script.push_back(say("Columns, opacity", "Standings: Last lap on, opacity 0"));
    script.push_back({ Step::MOVE, 0.8, region("standings"), nullptr, "standings tab" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 0.7, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { rowDump("Last lap time"); }, "" });
    script.push_back({ Step::MOVE, 0.8, rowCaret("standings.col_lastlap", "Last lap time", 0), nullptr, "last lap column" });
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 0.9, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { rowDump("Last lap time"); }, "" });
    // ...and its opacity down to 0, held.
    script.push_back({ Step::MOVE, 0.8, rowCaret("common.opacity", "Opacity", 0, "<"), nullptr, "standings opacity" });
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::PRESS, 4.0, nullptr, nullptr, "", [&] { return pctOnRow("Opacity") == 0; } });
    script.push_back({ Step::WAIT, 0.8, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { rowDump("Opacity"); }, "" });
    // Widgets: the Gamepad on, recoloured, shrunk, then dragged bottom right.
    script.push_back(say("Gamepad widget", "Follows your inputs"));
    script.push_back({ Step::MOVE, 0.8, region("widgets"), nullptr, "widgets tab" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 0.7, nullptr, nullptr, "" });
    script.push_back({ Step::MOVE, 0.8, rowCaret("widgets.gamepad", "Gamepad", 0), nullptr, "gamepad on" });
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 1.0, nullptr, nullptr, "" });
    // Back from the shipped Xbox One through the DualShock to the orange Xbox.
    script.push_back({ Step::MOVE, 0.6, rowCaret("widgets.gamepad", "Gamepad", 2, "<"), nullptr, "gamepad colour" });
    for (int k = 0; k < 5; ++k) {
        script.push_back({ Step::WAIT, 0.15, nullptr, nullptr, "" });
        script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
        script.push_back({ Step::WAIT, 0.35, nullptr, nullptr, "" });
    }
    script.push_back({ Step::ACTION, 0, nullptr, [&] { rowDump("Gamepad"); }, "" });
    script.push_back({ Step::WAIT, 0.5, nullptr, nullptr, "" });
    // Smaller: the pad at 100% is a third of the screen wide.
    script.push_back({ Step::MOVE, 0.6, rowCaret("widgets.gamepad", "Gamepad", 4, "<"), nullptr, "gamepad scale" });
    // Held: the stepper repeats and speeds up, as for a player holding the button.
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::PRESS, 6.0, nullptr, nullptr, "", [&host] {
        const auto rows = host.hudStringRows("settings_hud");
        double ly = -1;
        for (const auto& r : rows) if (r.text == "Gamepad") ly = r.y;
        int pct = 0;   // the row's rightmost percentage: Scale
        double px = -1;
        for (const auto& r : rows)
            if (std::fabs(r.y - ly) < 0.002 && r.x > px && r.text.find('%') != std::string::npos) {
                px = r.x; pct = std::atoi(r.text.c_str());
            }
        return pct > 0 && pct <= 60; } });
    script.push_back({ Step::ACTION, 0, nullptr, [&] { rowDump("Gamepad"); }, "" });
    script.push_back({ Step::WAIT, 0.5, nullptr, nullptr, "" });
    script.push_back(say("Gamepad widget", "Any colour, any size"));
    script.push_back({ Step::MOVE, 0.9, stripRow("Settings"), nullptr, "close settings" });
    script.push_back({ Step::WAIT, 0.2, nullptr, nullptr, "" });
    script.push_back({ Step::CLICK, 0.25, nullptr, nullptr, "" });
    script.push_back({ Step::WAIT, 0.4, nullptr, nullptr, "" });
    // Grab the pad from its spot bottom left and drop it bottom right, its bottom
    // on the screen's last whole cell, two cells left of Gear and RPM.
    auto padBox = [&host](double& l, double& t, double& r, double& b) {
        l = t = 9; r = b = -9;
        for (const auto& q : host.hudQuadRects("gamepad_widget")) {
            l = std::min(l, q.l); t = std::min(t, q.t); r = std::max(r, q.r); b = std::max(b, q.b);
        }
        return r > l;
    };
    float padDx = 0, padDy = 0;
    script.push_back(say("Move any HUD", "Right-drag it anywhere", 1));   // the pad crosses the bottom box
    script.push_back({ Step::MOVE, 0.8, [&](float& x, float& y) {
        double l, t, r, b;
        if (!padBox(l, t, r, b)) { fprintf(stderr, "gamepad not drawn\n"); return false; }
        fprintf(stderr, "pad box %.3f %.3f %.3f %.3f\n", l, t, r, b);
        x = static_cast<float>((l + r) / 2); y = static_cast<float>((t + b) / 2);
        double gl = 9;
        for (const char* w : { "gear_widget", "rpm_widget", "speed_widget" })
            for (const auto& q : host.hudQuadRects(w)) gl = std::min(gl, q.l);
        if (gl > 1) gl = 0.88;
        padDx = static_cast<float>(gl - 2 * 0.0055 - r); padDy = static_cast<float>(0.9899 - b);
        return true; }, nullptr, "to the pad" });
    script.push_back({ Step::WAIT, 0.3, nullptr, nullptr, "" });
    script.push_back({ Step::RDRAG, 1.2, [&](float& x, float& y) { x += padDx; y += padDy; return true; },
                       nullptr, "drag the pad" });
    script.push_back({ Step::WAIT, 0.6, nullptr, nullptr, "" });
    script.push_back({ Step::ACTION, 0, nullptr, [&] {
        double l, t, r, b;
        if (padBox(l, t, r, b)) fprintf(stderr, "pad box after %.3f %.3f %.3f %.3f\n", l, t, r, b); }, "" });
    script.push_back({ Step::MOVE, 0.8, at(0.80f, 0.55f), nullptr, "away" });
    script.push_back(say("Racing", "Positions, gaps, personal bests"));
    script.push_back({ Step::ACTION, 0, nullptr, [&] { mouseIdle = true; host.cursorIdle(); }, "idle" });
    // What the Dash calls out while the race runs, timed from the reel's end:
    // tapegen puts the catch, the crash and the PB lap that far from it.
    struct Callout { double before; const char* detail; };
    for (const Callout& c : { Callout{ 27.0, "Lapper ahead, on the Dash" }, Callout{ 16.0, "Hazard ahead: a rider down" },
                              Callout{ 5.5, "Personal best, on the Dash" } }) {
        script.push_back({ Step::WAIT, 60, nullptr, nullptr, "", [&nowT, demoSecs, c] { return nowT >= demoSecs - c.before; } });
        script.push_back({ Step::ACTION, 0, nullptr, [&, c] { caption("Racing", c.detail); }, "" });
    }
    script.push_back({ Step::WAIT, 60, nullptr, nullptr, "end" });
    (void)region; (void)caret; (void)gapX; (void)gapY; (void)dashX; (void)dashY;

    // Transparent capture: the companion's frame on a clear backdrop, straight
    // it a FIFO that ffmpeg encodes as it arrives, so raw RGBA never lands on disk).
    // it a FIFO that ffmpeg encodes as it arrives, so full HD never lands on disk).
    const int CAP_W = getenv("REEL_W") ? atoi(getenv("REEL_W")) : 1920;
    const int CAP_H = getenv("REEL_H") ? atoi(getenv("REEL_H")) : 1080;
    // The caption layout's boxes (fractions of the frame): 0 bottom centre, below
    // even a title-bar theme's taller Settings panel and clear of the pad, empty
    // through the whole reel; 1 top, between Position and the Dash, for the drag
    // (the pad crosses the bottom). make_demo.sh draws them from captions.tsv.
    static constexpr float kCaptionBoxes[2][4] = { { 0.27f, 0.942f, 0.68f, 0.994f }, { 0.165f, 0.012f, 0.395f, 0.072f } };
    const bool captionCheck = getenv("REEL_CAPTION_CHECK") && *getenv("REEL_CAPTION_CHECK");
    int overlapFrames = 0, firstOverlap = -1;
    constexpr double CAP_FPS = 30.0;
    auto capture = host.sym<int (*)(int, int, unsigned char*)>("MXBMRP3_Test_CompanionCaptureRgba");
    std::vector<unsigned char> capBuf(static_cast<size_t>(CAP_W) * CAP_H * 4);
    FILE* capFile = capture && getenv("REEL_CAPTURE") ? fopen("capture.rgba", "wb") : nullptr;
    double nextCap = 0.0;
    int capFrames = 0;
    // The frame each caption first shows on. Captions are timed by FRAME, not by
    // the script's clock: when the encoder falls behind, the capture writes fewer
    // frames than seconds passed (a 1080p run lost 14 s that way), and clock-timed
    // captions drifted onto the wrong beat.
    std::vector<int> captionFrame;
    double capMs = 0;
    // Anything drawn under the caption's box (alpha > 0, sampled every 2 px).
    auto boxBusy = [&](int box) {
        const float* b = kCaptionBoxes[box];
        const int x0 = static_cast<int>(b[0] * CAP_W), x1 = static_cast<int>(b[2] * CAP_W);
        const int y0 = static_cast<int>(b[1] * CAP_H), y1 = static_cast<int>(b[3] * CAP_H);
        for (int y = y0; y < y1; y += 2)
            for (int x = x0; x < x1; x += 2)
                if (capBuf[(static_cast<size_t>(y) * CAP_W + x) * 4 + 3] != 0) return true;
        return false;
    };
    // Pre-roll: REEL_PREROLL frames of the opening scene, which make_demo.sh trims
    // off, so the encoder and the renderer are warm when filming starts. Without
    // it the first ~3.5 s came out at ~4 fps, the frame repeated to keep time.
    const char* prerollEnv = getenv("REEL_PREROLL");
    const int preroll = capFile && prerollEnv ? atoi(prerollEnv) : 0;
    for (int i = 0; i < preroll; ++i) {
        host.drawWithState(0);
        capture(CAP_W, CAP_H, capBuf.data());
        fwrite(capBuf.data(), 1, capBuf.size(), capFile);
    }
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    // Capture on its own thread at a steady CAP_FPS, from the companion's latest
    // submitted frame, so the render cost never slows the reel's own loop. A
    // capture that overruns its slot repeats the frame to keep time.
    std::atomic<bool> capRun{ capFile != nullptr };
    std::thread capThread([&] {
        while (capRun) {
            const double t = std::chrono::duration<double>(clock::now() - t0).count();
            if (t < nextCap) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
            const auto c0 = clock::now();
            const int shown = captionCount;   // before the capture: the frame shows at least these
            capture(CAP_W, CAP_H, capBuf.data());
            capMs += std::chrono::duration<double, std::milli>(clock::now() - c0).count();
            const int box = captionBox;
            if (captionCheck && box >= 0 && boxBusy(box)) {
                if (firstOverlap < 0) {   // keep the frame, so the log can show what it was
                    firstOverlap = capFrames;
                    if (FILE* of = fopen("overlap.rgba", "wb")) { fwrite(capBuf.data(), 1, capBuf.size(), of); fclose(of); }
                }
                ++overlapFrames;
            }
            const double now = std::chrono::duration<double>(clock::now() - t0).count();
            while (capRun && now >= nextCap) {
                while (static_cast<int>(captionFrame.size()) < shown) captionFrame.push_back(capFrames);
                fwrite(capBuf.data(), 1, capBuf.size(), capFile);
                ++capFrames;
                nextCap += 1.0 / CAP_FPS;
            }
        }
    });
    size_t si = 0;
    double stepStart = 0;
    float fromX = mx, fromY = my, toX = mx, toY = my;
    bool stepInit = false;
    for (;;) {
        const double t = std::chrono::duration<double>(clock::now() - t0).count();
        if (t > demoSecs) break;
        nowT = t;
        // The tape, in real time.
        const uint64_t tapeNow = liveUs + static_cast<uint64_t>(t * 1e6);
        for (; next < events.size() && events[next].us <= tapeNow; ++next) {
            TapeEvent& e = events[next];
            if (e.type == tape::EventType::Draw) continue;   // this loop draws per frame
            setNowUs(kClockBaseUs + static_cast<long long>(e.us));
            host.dispatch(e.type, e.data);
            padFrom(e);
        }
        setNowUs(kClockBaseUs + static_cast<long long>(tapeNow));

        // Mouse script.
        if (si < script.size()) {
            Step& st = script[si];
            if (!stepInit && pastEnd && !(st.label && *st.label)) { ++si; continue; }
            if (!stepInit && st.skip && st.skip()) { ++si; continue; }
            // REST: stop only on icons that preview something; glide past the rest.
            if (!stepInit && st.kind == Step::REST) {
                static const char* const kShowsAll[] = { "Helmet", "Stream Chat", "Spotter", "Achievements",
                    "Notices", "Timing", "Lap Log", "Gap Bar", "Delta Trace", "Pitboard", "Radar",
                    "Ideal Lap", "Session", "Performance", "Stats", "Telemetry", "Records", "FMX",
                    "Event Log", "Charts", "Friends" };
                // The short pass: the main ones, Session among them (it is kept on).
                static const char* const kShowsMain[] = { "Timing", "Lap Log", "Gap Bar", "Pitboard", "Radar",
                    "Session", "Performance", "Records", "Delta Trace" };
                static const bool fullStrip = !(getenv("REEL_STRIP") && std::string(getenv("REEL_STRIP")) == "main");
                std::string nm;
                float hx, hy;
                for (int r = 0; r < host.toggleStripRow(0); ++r) {
                    host.toggleStripRow(r, &hx, &hy, &nm);
                    if (std::fabs(hx - mx) < 0.02f && std::fabs(hy - my) < 0.01f) break;
                    nm.clear();
                }
                bool shows = false;
                if (fullStrip) { for (const char* k : kShowsAll) shows |= nm == k; }
                else { for (const char* k : kShowsMain) shows |= nm == k; }
                if (!shows) { si += 3; continue; }   // skip the rest, its hold and the keep
                st.kind = Step::WAIT;
            }
            // KEEP: switch Session on after its preview, so it stays for the rest.
            if (!stepInit && st.kind == Step::KEEP) {
                std::string nm;
                float hx, hy;
                for (int r = 0; r < host.toggleStripRow(0); ++r) {
                    host.toggleStripRow(r, &hx, &hy, &nm);
                    if (std::fabs(hx - mx) < 0.02f && std::fabs(hy - my) < 0.01f) break;
                    nm.clear();
                }
                if (nm != "Session") { ++si; continue; }
                fprintf(stderr, "%6.2f switch Session on\n", t);
                st.kind = Step::CLICK;
            }
            // HOLD: every switch that previews something (REST let it through) holds
            // the same time, whatever the tape has for it right now.
            if (!stepInit && st.kind == Step::HOLD) {
                const int q = compQuads();
                std::string nm;
                float hx, hy;
                for (int r = 0; r < host.toggleStripRow(0); ++r) {
                    host.toggleStripRow(r, &hx, &hy, &nm);
                    if (std::fabs(hx - mx) < 0.02f && std::fabs(hy - my) < 0.01f) break;
                    nm.clear();
                }
                fprintf(stderr, "%6.2f preview %s (+%d quads)\n",
                        std::chrono::duration<double>(clock::now() - t0).count(), nm.c_str(), q - baseQuads);
            }
            if (!stepInit) {
                stepInit = true;
                stepStart = t;
                fromX = mx; fromY = my;
                if (st.target) {
                    float x = mx, y = my;
                    if (st.target(x, y)) { toX = x; toY = y; } else { toX = mx; toY = my; }
                } else { toX = mx; toY = my; }
                if (st.kind == Step::ACTION && st.action) st.action();
                if (st.label && *st.label) fprintf(stderr, "%6.2f %s\n", t, st.label);
            }
            float a = st.dur > 0 ? static_cast<float>((t - stepStart) / st.dur) : 1.0f;
            switch (st.kind) {
                case Step::MOVE: case Step::RDRAG: {
                    float e = ease(a);
                    mx = fromX + (toX - fromX) * e;
                    my = fromY + (toY - fromY) * e;
                    buttons = st.kind == Step::RDRAG ? (a < 1.0f ? 2 : 0) : 0;
                    break;
                }
                case Step::CLICK: buttons = a < 0.5f ? 1 : 0; break;
                case Step::PRESS:
                    if (buttons == 1 && st.done && st.done()) a = 1.0f;
                    buttons = a < 1.0f ? 1 : 0;
                    break;
                case Step::WAIT:   // a WAIT with `done` ends early when it holds
                    if (st.done && st.done()) a = 1.0f;
                    buttons = 0;
                    break;
                default: buttons = 0; break;
            }
            if (!mouseIdle) host.injectMouse(true, mx, my, buttons);
            if (a >= 1.0f) { ++si; stepInit = false; }
        }
        host.drawWithState(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));   // draw faster than the 30 fps capture, or frames repeat
    }
    if (capFile) {
        capRun = false;
        capThread.join();
        fclose(capFile);
        fprintf(stderr, "captured %d frames, %.1f ms per capture\n", capFrames, capMs / std::max(1, capFrames));
        if (captionCheck)
            fprintf(stderr, "caption overlap: %d frames%s\n", overlapFrames,
                    firstOverlap >= 0 ? (" from " + std::to_string(firstOverlap / CAP_FPS) + " s").c_str() : "");
    }
    // seconds, beat, detail, box (fractions of the frame): one line per caption
    // change, so captions_ass.py draws exactly the box checked here.
    if (FILE* cf = fopen("captions.tsv", "wb")) {
        for (size_t i = 0; i < captions.size(); ++i) {
            const Caption& c = captions[i];
            const double at = !capFile ? c.t : i < captionFrame.size() ? captionFrame[i] / CAP_FPS : capFrames / CAP_FPS;
            const float* b = kCaptionBoxes[c.box];
            fprintf(cf, "%.3f\t%s\t%s\t%.3f,%.3f,%.3f,%.3f\n", at, c.beat.c_str(), c.detail.c_str(), b[0], b[1], b[2], b[3]);
        }
        fprintf(cf, "%.3f\t\t\t\n", capFile ? capFrames / CAP_FPS : demoSecs);
        fclose(cf);
    }
    fprintf(stderr, "done\n");
    host.companionWindow(false);
    host.shutdown();
    return 0;
}
