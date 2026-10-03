// ============================================================================
// tests/integration/tests/youtube_chat_test.cpp
// The YouTube half of the stream chat end to end, minus the network: a
// get_live_chat response body goes through YouTubeChatManager's own
// parse-and-queue path (MXBMRP3_Test_YouTubeInjectPoll), the one chat HUD
// drains it on a real Draw beside Twitch's lines, and what it would put on
// screen is read back row by row, icons included. Pins:
//
//   1. A mixed Twitch + YouTube chat renders in arrival order, each line with
//      its own platform icon (Auto: shown because both are on) and its own
//      platform's role icon.
//   2. Platform icons Auto hide while only one platform is on, so a
//      Twitch-only chat keeps its layout; On and Off force them.
//   3. Dedupe and moderation stay within a platform: the same id on both
//      platforms is two messages, a YouTube ban leaves a Twitch user alone, a
//      YouTube channel switch clears only YouTube's lines and Twitch's /clear
//      only Twitch's.
//   4. "Not live" is a status line over the chat, named by platform when both
//      are on, and does not hold an auto-hide panel open.
//   5. A changed response format reads "Unavailable" --
//      a line over Twitch's chat when Twitch is on, the whole panel when
//      YouTube is alone -- never a crash or a silent chat.
//   6. [YouTube] round-trips (a pasted URL loads as its @handle) and stays
//      global; a roles mask with YouTube's roles switched off loads as saved
//      (Stream Chat is unreleased, so no older mask needs migrating).
//   7. Every YouTube role shows its own icon.
//
// Headless runs stay offline: every case that switches YouTube on forces its
// status first, which keeps the worker from ever starting.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <string>
#include <vector>

namespace {
const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\youtube_chat\\";
const char* kIniWin = "Z:\\tmp\\mxbmrp3-tests\\youtube_chat\\mxbmrp3\\mxbmrp3_settings.ini";

constexpr int TWITCH = 0;
constexpr int YOUTUBE = 1;
constexpr int YT_NOT_LIVE = 3;

std::string privmsg(const char* login, const char* text, const char* id, const char* badges = "") {
    std::string s = "@badges=";
    s += badges;
    s += ";color=;display-name=";
    s += login;
    s += ";id=";
    s += id;
    s += ";tmi-sent-ts=1700000000000 :";
    s += login;
    s += "!";
    s += login;
    s += "@";
    s += login;
    s += ".tmi.twitch.tv PRIVMSG #chan :";
    s += text;
    return s;
}

// One liveChatTextMessageRenderer action. `badge` is an iconType (OWNER,
// MODERATOR, VERIFIED), "MEMBER" for a member's custom badge, or "".
std::string ytMessage(const char* id, const char* name, const char* channelId, const char* text,
                      const char* badge = "") {
    std::string badges;
    const std::string b = badge;
    if (b == "MEMBER") {
        badges = ",\"authorBadges\":[{\"liveChatAuthorBadgeRenderer\":{\"customThumbnail\":{\"thumbnails\":"
                 "[{\"url\":\"https://yt3.ggpht.com/b\"}]},\"tooltip\":\"Member (2 months)\"}}]";
    } else if (!b.empty()) {
        badges = ",\"authorBadges\":[{\"liveChatAuthorBadgeRenderer\":{\"icon\":{\"iconType\":\"" + b +
                 "\"},\"tooltip\":\"x\"}}]";
    }
    return std::string("{\"addChatItemAction\":{\"item\":{\"liveChatTextMessageRenderer\":{\"id\":\"") + id +
           "\",\"timestampUsec\":\"1790000000000000\",\"authorName\":{\"simpleText\":\"" + name +
           "\"},\"authorExternalChannelId\":\"" + channelId + "\",\"message\":{\"runs\":[{\"text\":\"" + text +
           "\"}]}" + badges + "}}}}";
}

// A get_live_chat response carrying `actions` (comma-joined JSON objects).
std::string ytPoll(const std::vector<std::string>& actions) {
    std::string joined;
    for (const auto& a : actions) {
        if (!joined.empty()) joined += ",";
        joined += a;
    }
    return "{\"responseContext\":{},\"continuationContents\":{\"liveChatContinuation\":{\"continuations\":"
           "[{\"invalidationContinuationData\":{\"continuation\":\"NEXT\",\"timeoutMs\":5000}}],\"actions\":[" +
           joined + "]}}}";
}

// Replace (or append) one key inside a section of a saved settings file.
void setKey(std::string& text, const char* section, const std::string& key, const std::string& value) {
    const std::string header = std::string("[") + section + "]";
    const size_t sec = text.find(header);
    REQUIRE_MESSAGE(sec != std::string::npos, "no " << header << " section was saved");
    const size_t at = text.find("\n" + key + "=", sec);
    const size_t next = text.find("\n[", sec + 1);
    if (at == std::string::npos || (next != std::string::npos && at > next)) {
        text.insert(sec + header.size(), "\n" + key + "=" + value);
        return;
    }
    const size_t end = text.find('\n', at + 1);
    text.replace(at + 1, end - at - 1, key + "=" + value);
}

// Only the chat HUD on screen, fresh from a first run's defaults.
void chatOnly(PluginHost& host) {
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible("stream_chat_hud", true));
    host.draw();
}

// Copy shipped icons into the user-asset folder the plugin syncs at Startup,
// the discovery path twitch_chat_test explains.
void stageIcons(const std::vector<const char*>& icons) {
    const std::string save = kSaveWin;
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\youtube_chat", nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    for (const char* icon : icons) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), MXB_REPO_DATA_DIR "/icons/%s.tga", icon);
        snprintf(dst, sizeof(dst), "%smxbmrp3\\icons\\%s.tga", save.c_str(), icon);
        REQUIRE_MESSAGE(CopyFileA(src, dst, FALSE) != 0, "shipped icon missing: " << std::string(src));
    }
}

// The platform marks are the brands' official logos, shipped as textures (full
// colour, drawn untinted), not icons: stage them the same way.
void stagePlatformMarks() {
    const std::string save = kSaveWin;
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\youtube_chat", nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    for (const char* mark : { "twitch", "youtube" }) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), MXB_REPO_DATA_DIR "/textures/%s_1.tga", mark);
        snprintf(dst, sizeof(dst), "%smxbmrp3\\textures\\%s_1.tga", save.c_str(), mark);
        REQUIRE_MESSAGE(CopyFileA(src, dst, FALSE) != 0, "shipped texture missing: " << std::string(src));
    }
}

// Fresh settings for a case: a first run writes the defaults, then `edit`
// changes what the case needs before the run it is about.
template <typename Edit>
void prepareIni(Edit edit) {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.shutdown();
    }
    std::string text = ini::readFile(kIniWin);
    edit(text);
    REQUIRE(ini::writeFile(kIniWin, text));
}

void defaults(std::string& text) {
    setKey(text, "Twitch", "enabled", "0");
    setKey(text, "Twitch", "channel", "");
    setKey(text, "StreamChat", "platformIcons", "2");
    setKey(text, "StreamChat", "rows", "6");
    setKey(text, "StreamChat", "showMode", "1");
    setKey(text, "YouTube", "enabled", "0");
    setKey(text, "YouTube", "channel", "");
}
}  // namespace

TEST_CASE("youtube chat: a mixed chat renders in arrival order with each platform's icons") {
    stageIcons({ "video", "wrench", "star", "circle-check", "shield" });
    stagePlatformMarks();
    prepareIni(defaults);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE_MESSAGE(host.hasYouTubeHooks(), "YouTube test hooks not exported (test build?)");
    host.twitchSimulateConnected();
    host.youtubeSimulateConnected();
    chatOnly(host);

    host.twitchInject(privmsg("alice", "hello from twitch", "t1", "moderator/1").c_str());
    REQUIRE(host.youtubeInjectPoll(ytPoll({
        ytMessage("y1", "@Owner", "UCowner0000000000000000a", "hello from youtube", "OWNER"),
        ytMessage("y2", "@Member", "UCmember000000000000000a", "sub for six months", "MEMBER"),
    }).c_str()) == 0);
    host.twitchInject(privmsg("bob", "gg", "t2").c_str());
    host.draw();

    REQUIRE(host.chatRowCount() == 4);
    CHECK(host.chatRow(0) == "alice:|hello from twitch");
    CHECK(host.chatRow(1) == "@Owner:|hello from youtube");
    CHECK(host.chatRow(2) == "@Member:|sub for six months");
    CHECK(host.chatRow(3) == "bob:|gg");

    const int twitchIcon = host.chatPlatformSprite(TWITCH);
    const int youtubeIcon = host.chatPlatformSprite(YOUTUBE);
    REQUIRE(twitchIcon > 0);
    REQUIRE(youtubeIcon > 0);
    CHECK(twitchIcon != youtubeIcon);
    // The official logo textures, not glyphs from the icon set.
    CHECK(twitchIcon == host.textureSprite("twitch"));
    CHECK(youtubeIcon == host.textureSprite("youtube"));
    struct Expect { int platform; int role; };
    const Expect rows[] = {
        { twitchIcon, host.iconSpriteForName("shield") },   // Twitch moderator
        { youtubeIcon, host.iconSpriteForName("video") },   // YouTube owner
        { youtubeIcon, host.iconSpriteForName("star") },    // YouTube member
        { twitchIcon, 0 },                                  // no role
    };
    for (int i = 0; i < 4; ++i) {
        int platform = -1, role = -1;
        REQUIRE(host.chatRowSprites(i, platform, role));
        CHECK_MESSAGE(platform == rows[i].platform, "row " << i << " platform icon");
        CHECK_MESSAGE(role == rows[i].role, "row " << i << " role icon");
    }

    SUBCASE("the same id on both platforms is two messages; a resend on one is dropped") {
        host.twitchInject(privmsg("carol", "same id", "shared").c_str());
        host.youtubeInjectPoll(ytPoll({ ytMessage("shared", "@Dave", "UCdave00000000000000000a", "same id") }).c_str());
        host.youtubeInjectPoll(ytPoll({ ytMessage("shared", "@Dave", "UCdave00000000000000000a", "same id") }).c_str());
        host.draw();
        CHECK(host.chatEntryCount() == 6);
    }

    SUBCASE("a YouTube ban removes that author's YouTube lines only") {
        // A Twitch login spelled like the banned channel id stays.
        host.twitchInject(privmsg("ucowner0000000000000000a", "stays", "t3").c_str());
        host.youtubeInjectPoll(ytPoll({
            "{\"markChatItemsByAuthorAsDeletedAction\":{\"externalChannelId\":\"UCowner0000000000000000a\"}}",
            "{\"markChatItemAsDeletedAction\":{\"targetItemId\":\"y2\"}}",
        }).c_str());
        host.draw();
        REQUIRE(host.chatRowCount() == 3);
        CHECK(host.chatRow(0) == "alice:|hello from twitch");
        CHECK(host.chatRow(1) == "bob:|gg");
        CHECK(host.chatRow(2) == "ucowner0000000000000000a:|stays");
    }

    SUBCASE("switching the YouTube channel clears only YouTube's lines") {
        host.youtubeSetChannel("@another_channel");  // status stays forced: no request
        host.draw();
        REQUIRE(host.chatRowCount() == 2);
        CHECK(host.chatRow(0) == "alice:|hello from twitch");
        CHECK(host.chatRow(1) == "bob:|gg");
    }

    SUBCASE("Twitch's /clear clears only Twitch's lines") {
        host.twitchInject(":tmi.twitch.tv CLEARCHAT #chan");
        host.draw();
        REQUIRE(host.chatRowCount() == 2);
        CHECK(host.chatRow(0) == "@Owner:|hello from youtube");
        CHECK(host.chatRow(1) == "@Member:|sub for six months");
    }

    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.shutdown();
}

TEST_CASE("youtube chat: platform icons follow Off / On / Auto") {
    stagePlatformMarks();
    prepareIni(defaults);

    // Each host is scoped: a second PluginHost while the first is loaded shares
    // its DLL instance, so the second's settings load would not be what it reads.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasYouTubeHooks());
        host.twitchSimulateConnected();  // Twitch alone
        chatOnly(host);
        host.twitchInject(privmsg("alice", "only twitch", "a1").c_str());
        host.draw();

        // Auto with one platform on: no platform column, so the layout is the
        // Twitch-only one.
        int platform = -1, role = -1;
        REQUIRE(host.chatRowSprites(0, platform, role));
        CHECK(platform == 0);

        // Auto with both on: the column appears.
        host.youtubeSimulateConnected();
        host.draw();
        REQUIRE(host.chatRowSprites(0, platform, role));
        CHECK(platform == host.chatPlatformSprite(TWITCH));

        REQUIRE(host.setHudVisible("stream_chat_hud", false));
        host.draw();
        host.shutdown();
    }

    // On forces it for Twitch alone; Off drops it with both on.
    for (const char* mode : { "1", "0" }) {
        const bool on = std::string(mode) == "1";
        prepareIni([&](std::string& text) {
            defaults(text);
            setKey(text, "StreamChat", "platformIcons", mode);
        });
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        host.twitchSimulateConnected();
        if (!on) host.youtubeSimulateConnected();
        chatOnly(host);
        host.twitchInject(privmsg("alice", "hi", "a2").c_str());
        host.draw();
        int platform = -1, role = -1;
        REQUIRE(host.chatRowSprites(0, platform, role));
        CHECK_MESSAGE(platform == (on ? host.chatPlatformSprite(TWITCH) : 0), "platformIcons=" << mode);
        REQUIRE(host.setHudVisible("stream_chat_hud", false));
        host.draw();
        host.shutdown();
    }
}

TEST_CASE("youtube chat: not live is a status line, named by platform when both are on") {
    prepareIni([](std::string& text) {
        defaults(text);
        setKey(text, "YouTube", "enabled", "1");
        setKey(text, "YouTube", "channel", "@testchan");
    });

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasYouTubeHooks());
    host.youtubeForceStatus(YT_NOT_LIVE);  // before the first draw: no worker, no request
    chatOnly(host);
    host.draw();

    // YouTube alone: the line over an empty chat.
    REQUIRE(host.chatRowCount() == 1);
    CHECK(host.chatRow(0) == "~|Not live - checking every minute");

    // With Twitch on too, the line names its platform and Twitch chats on.
    host.twitchSimulateConnected();
    host.twitchInject(privmsg("alice", "is it on?", "n1").c_str());
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "~|YouTube: Not live - checking every minute");
    CHECK(host.chatRow(1) == "alice:|is it on?");
    CHECK(host.youtubeStatus() == YT_NOT_LIVE);

    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.shutdown();
}


TEST_CASE("youtube chat: not live does not hold auto-hide open") {
    // "Not live" is where a streamer who has YouTube on but streams elsewhere
    // today sits for hours; unlike connecting or not-found, it is no reason to
    // keep an auto-hide panel on screen.
    prepareIni([](std::string& text) {
        defaults(text);
        setKey(text, "StreamChat", "showMode", "2");
        setKey(text, "YouTube", "enabled", "1");
        setKey(text, "YouTube", "channel", "@testchan");
    });

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasYouTubeHooks());
    host.youtubeForceStatus(YT_NOT_LIVE);  // before the first draw: no worker, no request
    chatOnly(host);
    host.draw();
    CHECK(host.chatRowCount() == 0);

    // A message still brings the panel back, with the status line above it.
    host.youtubeInjectPoll(ytPoll({ ytMessage("m1", "@Erin", "UCerin00000000000000000a", "hello") }).c_str());
    host.draw();
    REQUIRE(host.chatRowCount() == 2);
    CHECK(host.chatRow(0) == "~|Not live - checking every minute");

    REQUIRE(host.setHudVisible("stream_chat_hud", false));
    host.draw();
    host.shutdown();
}
TEST_CASE("youtube chat: a changed response format says Unavailable, and Twitch carries on") {
    prepareIni(defaults);
    const std::string changed =
        "{\"responseContext\":{},\"continuationContents\":{\"liveChatContinuation\":{\"continuations\":"
        "[{\"timedContinuationData\":{\"continuation\":\"N\",\"timeoutMs\":5000}}],\"actions\":"
        "[{\"addChatItemAction\":{\"item\":{\"liveChatTextMessageRenderer\":{\"messageId\":\"x\","
        "\"content\":{\"segments\":[{\"text\":\"moved\"}]}}}}}]}}}";

    SUBCASE("beside Twitch: a status line, Twitch's chat untouched") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasYouTubeHooks());
        host.twitchSimulateConnected();
        host.youtubeSimulateConnected();
        chatOnly(host);
        host.twitchInject(privmsg("alice", "still here", "u1").c_str());
        CHECK(host.youtubeInjectPoll(changed.c_str()) == 2);  // Unrecognized
        host.draw();
        CHECK(host.youtubeStatus() == 6);  // UNAVAILABLE
        REQUIRE(host.chatRowCount() == 2);
        CHECK(host.chatRow(0) == "~|YouTube: Unavailable");
        CHECK(host.chatRow(1) == "alice:|still here");
        REQUIRE(host.setHudVisible("stream_chat_hud", false));
        host.draw();
        host.shutdown();
    }

    SUBCASE("alone: the panel says so") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasYouTubeHooks());
        host.youtubeSimulateConnected();
        chatOnly(host);
        CHECK(host.youtubeInjectPoll("<html>not json</html>") == 2);
        host.draw();
        REQUIRE(host.chatRowCount() == 2);
        CHECK(host.chatRow(0) == "!|YouTube chat unavailable");
        CHECK(host.chatRow(1) == "!|YouTube's chat can't be read");
        REQUIRE(host.setHudVisible("stream_chat_hud", false));
        host.draw();
        host.shutdown();
    }
}

TEST_CASE("youtube chat: [YouTube] round-trips and stays global; role masks load as saved") {
    // Hidden HUD and switched off, so loading a channel opens no connection.
    prepareIni([](std::string& text) {
        defaults(text);
        setKey(text, "YouTube", "channel", "https://www.youtube.com/@Some.Rider-42/live");
        setKey(text, "StreamChat", "roles", "2047");  // every role but YouTube's member and verified
    });
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        REQUIRE(host.hasYouTubeHooks());
        CHECK(host.youtubeChannel() == "@Some.Rider-42");
        CHECK(host.youtubeStatus() == 0);
        host.shutdown();
    }
    const ini::Map saved = ini::parse(ini::readFile(kIniWin));
    CHECK(saved.at({"YouTube", "channel"}) == "@Some.Rider-42");
    CHECK(saved.at({"YouTube", "enabled"}) == "0");
    CHECK(saved.at({"StreamChat", "roles"}) == "2047");  // member and verified stay off
    for (const auto& kv : saved) {
        CHECK_MESSAGE(kv.first.first.rfind("YouTube:", 0) != 0,
                      "per-profile YouTube section written: [" << kv.first.first << "]");
    }
}

TEST_CASE("youtube chat: every YouTube role shows its own icon") {
    // THE EXPECTED MAPPING, stated here independently of the HUD's table.
    struct Expect { int role; const char* icon; };
    const Expect expected[] = {
        { 1,    "video" },         // owner (Chat::ROLE_BROADCASTER)
        { 2,    "wrench" },        // moderator
        { 2048, "star" },          // member
        { 4096, "circle-check" },  // verified
    };
    std::vector<const char*> icons;
    for (const Expect& e : expected) icons.push_back(e.icon);
    icons.push_back("shield");
    stageIcons(icons);

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasYouTubeHooks());
    for (const Expect& e : expected) {
        const std::string icon = e.icon;
        const int want = host.iconSpriteForName(e.icon);
        REQUIRE_MESSAGE(want > 0, icon << " was not registered as an icon");
        CHECK_MESSAGE(host.chatBadgeSprite(YOUTUBE, e.role) == want, "YouTube role bit " << e.role << " does not show " << icon);
    }
    // The shared moderator bit keeps each platform's own icon.
    CHECK(host.chatBadgeSprite(TWITCH, 2) == host.iconSpriteForName("shield"));
    // Twitch-only roles have no YouTube icon.
    CHECK(host.chatBadgeSprite(YOUTUBE, 4) == 0);  // VIP
    host.shutdown();
}
