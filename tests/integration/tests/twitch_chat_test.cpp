// ============================================================================
// tests/integration/tests/twitch_chat_test.cpp
// The Stream Chat HUD's Twitch side end to end, minus the network: raw IRC lines go through
// TwitchChatManager's own parse-and-queue path (MXBMRP3_Test_TwitchInjectLine),
// the HUD drains them on a real Draw, and what it would put on screen is read
// back row by row. Pins:
//
//   1. Messages render as "Name: text", oldest at the bottom like Twitch, and a
//      long message WRAPS onto continuation rows instead of being cut off.
//   2. Moderation takes text OFF the streamer's screen: a deleted message
//      (CLEARMSG), a banned/timed-out user (CLEARCHAT <user>) and /clear.
//   3. The filters: bot commands, known bots, and a message that was nothing but
//      emotes (nothing left to draw once emote names are dropped).
//   4. Colours: a Twitch colour is used as sent when readable, a dark one is
//      lifted, and a user with no colour gets the default palette.
//   5. [Twitch] and [StreamChat] round-trip: a pasted channel URL loads as the
//      bare name, the layout keys survive a save, and none of it lands in a
//      per-profile section.
//   6. Width (characters) drives the wrap: at the minimum width every row fits
//      what is left beside the icon and time columns, and a name too long for
//      it is cut rather than drawn past the panel edge.
//   7. Hide/show brings the rows back; auto-hide expires and only a SHOWN
//      message re-wakes it.
//   8. A passing state (retrying) is a status line on the row away from new
//      messages, gone when connected; a state the chat cannot get past by
//      itself (switched off, no channel, not found) replaces the chat with a
//      centred headline + hint, the Gamepad widget's disconnected message.
//   9. Every role shows its own icon (checked against this test's own
//      role -> icon list, not the HUD's), and a user's own twitch_<badge>_1.tga
//      in textures/ replaces the icon for that role only.
//  10. A message Twitch sends twice (same id) is shown once, and a line read
//      before a channel switch (an older generation) is dropped.
//  11. The log stays anonymous: a Twitch channel loaded from the INI, a YouTube
//      channel typed in, and a NOTICE naming the channel never reach it (logs
//      get shared; through the 1.31 pre-release every one of them did).
//
// Headless runs stay offline: no case sets a channel while the HUD is visible,
// except case 8, whose forced status keeps the worker from ever starting.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\";
const char* kIniWin = "Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\mxbmrp3\\mxbmrp3_settings.ini";

std::string privmsg(const char* login, const char* text, const char* extraTags = "",
                    const char* id = "m0") {
    std::string s = "@badges=;color=;display-name=";
    s += login;
    s += ";id=";
    s += id;
    s += ";tmi-sent-ts=1700000000000";
    s += extraTags;
    s += " :";
    s += login;
    s += "!";
    s += login;
    s += "@";
    s += login;
    s += ".tmi.twitch.tv PRIVMSG #chan :";
    s += text;
    return s;
}

// Only the chat HUD on screen, so its rows are the only thing being asserted.
// `connected` (the default) puts the chat in the state it renders messages in
// -- on, a channel, connected -- with no socket; the connection-state cases
// pass false and set the state themselves. Being saved, that state reaches
// later cases too, which is why every drawing case comes through here.
void chatOnly(PluginHost& host, bool connected = true) {
    if (connected) host.twitchSimulateConnected();
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible("stream_chat_hud", true));
    host.draw();
}
}  // namespace

TEST_CASE("twitch chat: messages render, wrap, and moderation removes them") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE_MESSAGE(host.hasTwitchHooks(), "Twitch test hooks not exported (test build?)");
    chatOnly(host);

    host.twitchInject(privmsg("alice", "hello chat", "", "a1").c_str());
    host.twitchInject(privmsg("bob", "gg", "", "b1").c_str());
    host.draw();

    // Newest at the bottom like Twitch: alice above bob, on the bottom rows.
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "alice:|hello chat");
    CHECK(host.chatRow(1) == "bob:|gg");

    SUBCASE("a long message wraps instead of being cut off") {
        const std::string longText =
            "this is a deliberately long chat message that cannot possibly fit on one row of the hud";
        host.twitchInject(privmsg("carol", longText.c_str(), "", "c1").c_str());
        host.draw();
        const int rows = host.chatRowCount();
        REQUIRE(rows >= 4);  // alice, bob, and carol's 2+ rows
        std::string rebuilt;
        for (int i = 2; i < rows; ++i) {
            const std::string r = host.chatRow(i);
            const size_t bar = r.find('|');
            REQUIRE(bar != std::string::npos);
            if (i == 2) CHECK(r.substr(0, bar) == "carol:");
            else CHECK_MESSAGE(bar == 0, "continuation row carries a name: " << r);
            if (!rebuilt.empty()) rebuilt += ' ';
            rebuilt += r.substr(bar + 1);
        }
        CHECK(rebuilt == longText);  // every word is on screen, in order
    }

    SUBCASE("CLEARMSG deletes the one message") {
        host.twitchInject("@login=alice;target-msg-id=a1 :tmi.twitch.tv CLEARMSG #chan :hello chat");
        host.draw();
        REQUIRE(host.chatRowCount() == 1);
        CHECK(host.chatRow(0) == "bob:|gg");
    }

    SUBCASE("CLEARCHAT <user> removes everything that user said") {
        host.twitchInject(privmsg("bob", "second", "", "b2").c_str());
        host.twitchInject("@ban-duration=600 :tmi.twitch.tv CLEARCHAT #chan :bob");
        host.draw();
        REQUIRE(host.chatRowCount() == 1);
        CHECK(host.chatRow(0) == "alice:|hello chat");
    }

    SUBCASE("CLEARCHAT without a user clears the chat") {
        host.twitchInject(":tmi.twitch.tv CLEARCHAT #chan");
        host.draw();
        CHECK(host.chatRowCount() == 0);
        CHECK(host.chatEntryCount() == 0);
    }

    host.shutdown();
}

TEST_CASE("twitch chat: filters hide commands, bots and emote-only lines") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);

    host.twitchInject(privmsg("viewer", "!uptime", "", "f1").c_str());
    host.twitchInject(privmsg("nightbot", "Stream has been live for 2h", "", "f2").c_str());
    host.twitchInject(privmsg("viewer", "Kappa", ";emotes=25:0-4", "f3").c_str());
    host.twitchInject(privmsg("viewer", "\xF0\x9F\x98\x82\xF0\x9F\x98\x82", "", "f4").c_str());  // emoji only
    host.twitchInject(privmsg("viewer", "nice pass Kappa", ";emotes=25:10-14", "f5").c_str());
    host.draw();

    CHECK(host.chatEntryCount() == 5);  // all held; filtering is a display decision
    REQUIRE(host.chatRowCount() == 1);
    CHECK(host.chatRow(0) == "viewer:|nice pass");
    host.shutdown();
}

TEST_CASE("twitch chat: name colours are Twitch's, lifted when too dark") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);

    host.twitchInject(privmsg("red", "a", ";color=#FF0000", "k1").c_str());
    host.twitchInject(privmsg("navy", "b", ";color=#000080", "k2").c_str());
    host.twitchInject(privmsg("nocolor", "c", "", "k3").c_str());
    host.draw();

    // Game colours are ABGR.
    CHECK(host.chatNameColor(0) == 0xFF0000FFul);            // red, as sent
    const unsigned long navy = host.chatNameColor(1);
    CHECK(navy != 0xFF800000ul);                               // lifted, not raw navy
    CHECK(((navy >> 16) & 0xFF) > (navy & 0xFF));              // still blue
    CHECK(host.chatNameColor(2) != 0);                       // default palette entry
    host.shutdown();
}

namespace {
// Replace one key's value in a saved settings file: the connection's keys
// (enabled, channel) live in [Twitch], the chat HUD's in [StreamChat].
void setChatKey(std::string& text, const std::string& key, const std::string& value) {
    const char* header = (key == "enabled" || key == "channel") ? "[Twitch]" : "[StreamChat]";
    const size_t sec = text.find(header);
    REQUIRE_MESSAGE(sec != std::string::npos, "no " << header << " section was saved");
    const size_t at = text.find("\n" + key + "=", sec);
    REQUIRE(at != std::string::npos);
    const size_t end = text.find('\n', at + 1);
    text.replace(at + 1, end - at - 1, key + "=" + value);
}
}  // namespace

TEST_CASE("twitch chat: [Twitch] and [StreamChat] round-trip and stay global") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    // Hand-edit the saved file: a pasted URL and non-default layout. The HUD
    // stays hidden, so loading a channel does not open a connection.
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "https://www.twitch.tv/SomeOne/videos");
    setChatKey(text, "rows", "12");
    setChatKey(text, "width", "30");
    setChatKey(text, "hideBots", "0");
    REQUIRE(ini::writeFile(kIniWin, text));

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasTwitchHooks());
        CHECK(host.twitchChannel() == "someone");
        host.shutdown();
    }

    const ini::Map saved = ini::parse(ini::readFile(kIniWin));
    CHECK(saved.at({"Twitch", "channel"}) == "someone");
    CHECK(saved.at({"StreamChat", "rows"}) == "12");
    CHECK(saved.at({"StreamChat", "width"}) == "30");
    CHECK(saved.at({"StreamChat", "hideBots"}) == "0");
    // Global: nothing of it in a per-profile section.
    for (const auto& kv : saved) {
        CHECK_MESSAGE(kv.first.first.rfind("Twitch:", 0) != 0,
                      "per-profile Twitch section written: [" << kv.first.first << "]");
        CHECK_MESSAGE(kv.first.first.rfind("StreamChat:", 0) != 0,
                      "per-profile StreamChat section written: [" << kv.first.first << "]");
        CHECK_MESSAGE(kv.first.first != "StreamChatHud",
                      "the chat HUD leaked into the per-profile HUD sections");
    }
}

TEST_CASE("twitch chat: Width drives the wrap and cuts a name that cannot fit") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    // No channel, so showing the HUD opens no socket; minimum width.
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "");
    setChatKey(text, "width", "30");
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);

    // Width 30 with role icons (3) and timestamps (6) on leaves 21 characters
    // for "name: text" on a row.
    constexpr size_t kTextCols = 30 - 3 - 6;
    host.twitchInject(privmsg("averyveryverylongname2025",  // 25, Twitch's maximum
        "narrow panels wrap this message onto several short rows", "", "w1").c_str());
    host.draw();

    const int rows = host.chatRowCount();
    REQUIRE(rows >= 3);
    for (int i = 0; i < rows; ++i) {
        const std::string r = host.chatRow(i);
        const size_t bar = r.find('|');
        REQUIRE(bar != std::string::npos);
        // "name:|text" stands for "name: text" (the bar is the space);
        // "|text" is a continuation row.
        const size_t used = (bar == 0) ? r.size() - 1 : r.size();
        CHECK_MESSAGE(used <= kTextCols, "row wider than the panel: " << r);
    }
    const std::string first = host.chatRow(0);
    CHECK(first.substr(0, first.find('|')) == "averyveryverylongna:");  // cut to 19 + ':'
    host.shutdown();
}

TEST_CASE("twitch chat: rows come back when the HUD is hidden and shown again") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);

    host.twitchInject(privmsg("alice", "still here", "", "h1").c_str());
    host.draw();
    REQUIRE(host.chatRowCount() == 1);

    // Hidden drops the primitives (and would drop the socket); the messages stay.
    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    CHECK(host.chatRowCount() == 0);
    CHECK(host.chatEntryCount() == 1);

    REQUIRE(host.setHudVisible("stream_chat_hud", true));
    host.draw();
    REQUIRE(host.chatRowCount() == 1);
    CHECK(host.chatRow(0) == "alice:|still here");
    host.shutdown();
}

TEST_CASE("twitch chat: auto-hide hides after the timeout and a new message brings it back") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "");
    setChatKey(text, "showMode", "2");
    setChatKey(text, "autoHideMs", "1000");
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);
    CHECK(host.chatRowCount() == 0);  // nothing yet: auto-hide shows nothing

    host.twitchInject(privmsg("alice", "hi", "", "t1").c_str());
    host.draw();
    CHECK(host.chatRowCount() == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    host.draw();
    CHECK(host.chatRowCount() == 0);

    // Filtered lines do not wake it: a bot command is not a message to show.
    host.twitchInject(privmsg("bob", "!uptime", "", "t2").c_str());
    host.draw();
    CHECK(host.chatRowCount() == 0);

    host.twitchInject(privmsg("bob", "back again", "", "t3").c_str());
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(1) == "bob:|back again");
    host.shutdown();
}

TEST_CASE("twitch chat: the panel shows the connection status while it is not connected") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "testchan");
    setChatKey(text, "enabled", "1");  // on -- the forced status below keeps it offline
    setChatKey(text, "width", "43");  // cases share one settings file; the Width case left 30
    setChatKey(text, "showMode", "1");
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    host.twitchForceStatus(5);  // retrying -- and, being forced, no socket is opened
    chatOnly(host, false);
    host.twitchInject(privmsg("alice", "hi", "", "s1").c_str());
    host.draw();

    // The status takes the TOP row, the chat keeps the bottom.
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "~|Connection lost - retrying...");
    CHECK(host.chatRow(1) == "alice:|hi");

    host.twitchForceStatus(4);  // not found: blocking, so the chat gives way
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "!|Channel not found");
    CHECK(host.chatRow(1) == "!|Check MXBMRP3 Settings > Stream Chat");

    host.twitchForceStatus(3);  // connected: nothing to say
    host.draw();
    REQUIRE(host.chatRowCount() == 1);
    CHECK(host.chatRow(0) == "alice:|hi");

    // Hide before releasing the forced status, so no draw ever wants a socket.
    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.twitchForceStatus(-1);
    host.shutdown();
}

TEST_CASE("twitch chat: every role shows its own icon") {
    // THE EXPECTED MAPPING, stated here independently of the HUD's table: each
    // TwitchIrc::Role bit and the icon standing in for its Twitch badge. The HUD
    // must resolve each role to exactly that icon's sprite, so swapping two
    // entries in the HUD's table fails here.
    struct Expect { int role; const char* icon; };
    const Expect expected[] = {
        { 1,    "video" },       { 2,    "shield" },         { 4,   "gem" },
        { 8,    "star" },        { 16,   "wrench" },         { 32,  "certificate" },
        { 64,   "gavel" },       { 128,  "crown" },          { 256, "bolt-lightning" },
        { 512,  "paintbrush" },  { 1024, "award" },
    };

    // The discovery tree starts empty in the harness, so stage the SHIPPED icons
    // through the plugin's own user-asset sync (savePath\\mxbmrp3\\icons,
    // mirrored into the discovery tree at Startup) -- the path gl_render_test
    // explains. Staging from the repo means a missing icon fails here.
    const std::string save = kSaveWin;
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    for (const Expect& e : expected) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), MXB_REPO_DATA_DIR "/icons/%s.tga", e.icon);
        snprintf(dst, sizeof(dst), "%smxbmrp3\\icons\\%s.tga", save.c_str(), e.icon);
        REQUIRE_MESSAGE(CopyFileA(src, dst, FALSE) != 0, "shipped icon missing: " << std::string(src));
    }

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    for (const Expect& e : expected) {
        const std::string icon = e.icon;
        const int want = host.iconSpriteForName(e.icon);
        REQUIRE_MESSAGE(want > 0, icon << " was not registered as an icon");
        CHECK_MESSAGE(host.twitchBadgeSprite(e.role) == want,
                      "role bit " << e.role << " does not show " << icon);
    }
    CHECK(host.twitchBadgeSprite(0) == 0);  // no role, no icon
    host.shutdown();
}

TEST_CASE("twitch chat: a message Twitch resends is shown once") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);
    const std::string line = privmsg("alice", "only once", "", "dup1");
    host.twitchInject(line.c_str());
    host.twitchInject(line.c_str());  // same id: Twitch's resend
    host.draw();
    CHECK(host.chatEntryCount() == 1);
    CHECK(host.chatRowCount() == 1);
    host.shutdown();
}

TEST_CASE("twitch chat: a line from before a channel switch is dropped") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host);
    host.twitchInjectStale(privmsg("old_channel", "should not show", "", "g1").c_str());
    host.twitchInject(privmsg("alice", "current", "", "g2").c_str());
    host.draw();
    CHECK(host.chatEntryCount() == 1);
    REQUIRE(host.chatRowCount() == 1);
    CHECK(host.chatRow(0) == "alice:|current");
    host.shutdown();
}

TEST_CASE("twitch chat: a user's own badge texture replaces that role's icon") {
    // Any shipped texture will do as the "badge": it is copied in under the
    // name a streamer would give Twitch's VIP art. Staged through the user-asset
    // sync like every file here; one icon too, to show the others are untouched.
    const std::string save = kSaveWin;
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    REQUIRE(CopyFileA(MXB_REPO_DATA_DIR "/textures/badge_1.tga",
                      (save + "mxbmrp3\\textures\\twitch_vip_1.tga").c_str(), FALSE) != 0);
    REQUIRE(CopyFileA(MXB_REPO_DATA_DIR "/icons/shield.tga",
                      (save + "mxbmrp3\\icons\\shield.tga").c_str(), FALSE) != 0);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    const int userVip = host.textureSprite("twitch_vip");
    REQUIRE_MESSAGE(userVip > 0, "the staged twitch_vip_1.tga was not registered");
    CHECK(host.twitchBadgeSprite(4) == userVip);                          // VIP: the user's file
    CHECK(host.twitchBadgeSprite(2) == host.iconSpriteForName("shield")); // moderator: still the icon
    host.shutdown();
}

TEST_CASE("twitch chat: switched off, the panel says so instead of the chat") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    // A channel, but the connection switched off: nothing may connect.
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "testchan");
    setChatKey(text, "enabled", "0");
    setChatKey(text, "width", "43");
    setChatKey(text, "showMode", "1");
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host, false);
    host.twitchInject(privmsg("alice", "last words", "", "o1").c_str());
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "!|Stream chat off");
    CHECK(host.chatRow(1) == "!|Check MXBMRP3 Settings > Stream Chat");
    CHECK(host.twitchStatus() == 0);  // off: no worker was started
    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.shutdown();
}

TEST_CASE("twitch chat: switched on with no channel, the panel asks for one") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    // On, but nothing to connect to: no worker may start.
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "");
    setChatKey(text, "enabled", "1");
    setChatKey(text, "width", "43");
    setChatKey(text, "showMode", "1");
    setChatKey(text, "rows", "6");
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    chatOnly(host, false);
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "!|No channel set");
    CHECK(host.chatRow(1) == "!|Check MXBMRP3 Settings > Stream Chat");
    CHECK(host.twitchStatus() == 0);

    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.shutdown();
}

TEST_CASE("twitch chat: a new player starts hidden and switched off") {
    // A fresh save directory of its own: the other cases' settings file must not
    // stand in for a first run.
    const char* fresh = "Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\fresh\\";
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\fresh", nullptr);
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(fresh);
        host.draw();
        CHECK(host.twitchStatus() == 0);
        host.shutdown();
    }
    const ini::Map saved = ini::parse(ini::readFile(
        "Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\fresh\\mxbmrp3\\mxbmrp3_settings.ini"));
    CHECK(saved.at({"StreamChat", "visible"}) == "0");
    CHECK(saved.at({"Twitch", "enabled"}) == "0");
    CHECK(saved.at({"Twitch", "channel"}) == "");
}

TEST_CASE("twitch chat: the log never names the channel") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    std::string text = ini::readFile(kIniWin);
    setChatKey(text, "channel", "privchan4242");
    setChatKey(text, "enabled", "0");   // loaded, but no worker: nothing goes on the network
    REQUIRE(ini::writeFile(kIniWin, text));

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasTwitchHooks());
    REQUIRE(host.hasYouTubeHooks());
    host.twitchForceStatus(0);
    host.youtubeForceStatus(0);         // forced: setting a channel starts no worker
    host.youtubeSetChannel("@PrivHandle4242");
    host.twitchInject("@msg-id=msg_channel_suspended :tmi.twitch.tv NOTICE #privchan4242 "
                      ":privchan4242 does not exist or has been suspended.");
    host.youtubeSetChannel("");
    host.shutdown();   // flushes the log

    std::string log = ini::readFile("Z:\\tmp\\mxbmrp3-tests\\twitch_chat\\mxbmrp3\\mxbmrp3_log.txt");
    REQUIRE_FALSE(log.empty());
    std::transform(log.begin(), log.end(), log.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    CHECK_MESSAGE(log.find("privchan4242") == std::string::npos, "the Twitch channel reached the log");
    CHECK_MESSAGE(log.find("privhandle4242") == std::string::npos, "the YouTube channel reached the log");
    CHECK(log.find("twitchchat: notice (msg_channel_suspended)") != std::string::npos);
}
