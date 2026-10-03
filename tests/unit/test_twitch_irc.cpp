// ============================================================================
// tests/unit/test_twitch_irc.cpp
// Unit tests for core/twitch_irc.h (and the core/chat_text.h shaping it uses) --
// everything the Twitch chat reader decides
// about a line: IRCv3 parsing, the Twitch commands it acts on (including the
// moderation ones that must remove text from a streamer's screen), channel-name
// clean-up of pasted URLs, CP1252 conversion for the in-game font, emote
// stripping, colour fallback/readability and the chat wrap.
// Header-only, no network -- see tests/unit/README.md.
// ============================================================================
#include "doctest.h"

#include "core/twitch_irc.h"

using namespace TwitchIrc;
using namespace ChatText;
using namespace Chat;

TEST_CASE("twitch irc: parses tags, prefix, command and trailing param") {
    Line l;
    REQUIRE(parseLine("@badge-info=;badges=broadcaster/1;color=#1E90FF;display-name=Some\\sOne;"
                      "id=abc-123;tmi-sent-ts=1700000000123 :someone!someone@someone.tmi.twitch.tv "
                      "PRIVMSG #chan :hello there : world", l));
    CHECK(l.command == "PRIVMSG");
    CHECK(l.nick() == "someone");
    REQUIRE(l.params.size() == 2);
    CHECK(l.params[0] == "#chan");
    CHECK(l.params[1] == "hello there : world");
    REQUIRE(l.tag("display-name"));
    CHECK(*l.tag("display-name") == "Some One");
    REQUIRE(l.tag("badge-info"));
    CHECK(l.tag("badge-info")->empty());
    CHECK(l.tag("missing") == nullptr);

    // A repeated key takes its last value (IRCv3).
    REQUIRE(parseLine("@color=;color=#FF0000 :a!a@a PRIVMSG #c :x", l));
    CHECK(*l.tag("color") == "#FF0000");
}

TEST_CASE("twitch irc: tag unescaping") {
    CHECK(unescapeTagValue("a\\sb\\:c\\\\d") == "a b;c\\d");
    CHECK(unescapeTagValue("trailing\\") == "trailing");
    CHECK(unescapeTagValue("\\x") == "x");
}

TEST_CASE("twitch irc: malformed lines are rejected, not crashed on") {
    Line l;
    CHECK_FALSE(parseLine("", l));
    CHECK_FALSE(parseLine("@only-tags", l));
    CHECK_FALSE(parseLine(":prefixonly", l));
    CHECK(parseLine("PING :tmi.twitch.tv", l));
    CHECK(l.command == "PING");
}

TEST_CASE("twitch irc: channel names are reduced from whatever was pasted") {
    CHECK(normalizeChannel("SomeOne") == "someone");
    CHECK(normalizeChannel("  @SomeOne ") == "someone");
    CHECK(normalizeChannel("#some_one") == "some_one");
    CHECK(normalizeChannel("@@SomeOne") == "someone");  // a doubled '@' from a paste
    CHECK(normalizeChannel("\t#@SomeOne\r\n") == "someone");
    CHECK(normalizeChannel("twitch.tv/@SomeOne") == "someone");
    CHECK(normalizeChannel("https://www.twitch.tv/SomeOne") == "someone");
    CHECK(normalizeChannel("twitch.tv/someone/videos?filter=all") == "someone");
    CHECK(normalizeChannel("https://m.twitch.tv/someone") == "someone");
    CHECK(normalizeChannel("https://www.twitch.tv/popout/someone/chat?popout=") == "someone");
    CHECK(normalizeChannel("") == "");
    CHECK(normalizeChannel("https://") == "");
    CHECK(normalizeChannel(std::string(40, 'a')).size() == MAX_CHANNEL_LEN);
}

TEST_CASE("twitch irc: badges map to roles, highest one shown") {
    const uint8_t r = parseBadges("subscriber/12,vip/1");
    CHECK(r == (ROLE_SUBSCRIBER | ROLE_VIP));
    CHECK(primaryRole(r, 0xFF) == ROLE_VIP);
    CHECK(primaryRole(r, ROLE_SUBSCRIBER) == ROLE_SUBSCRIBER);  // VIP tag switched off
    CHECK(primaryRole(parseBadges("broadcaster/1,subscriber/0"), 0xFF) == ROLE_BROADCASTER);
    CHECK(parseBadges("staff/1") == ROLE_STAFF);
    CHECK(parseBadges("admin/1,global_mod/1") == ROLE_STAFF);  // retired badges
    CHECK(parseBadges("partner/1") == ROLE_PARTNER);
    CHECK(parseBadges("bits/1000,sub-gifter/5") == ROLE_NONE);  // channel extras
    // Ranking: staff over moderator, partner between VIP and subscriber.
    CHECK(primaryRole(ROLE_STAFF | ROLE_MODERATOR, 0xFF) == ROLE_STAFF);
    CHECK(primaryRole(ROLE_VIP | ROLE_PARTNER, 0xFF) == ROLE_VIP);
    CHECK(primaryRole(ROLE_PARTNER | ROLE_SUBSCRIBER, 0xFF) == ROLE_PARTNER);
    // The newer and platform-wide badges, by their IRC names.
    CHECK(parseBadges("lead_moderator/1") == ROLE_LEAD_MOD);
    CHECK(parseBadges("premium/1") == ROLE_PRIME);
    CHECK(parseBadges("turbo/1") == ROLE_TURBO);
    CHECK(parseBadges("artist-badge/1") == ROLE_ARTIST);
    const uint16_t all = 0xFFFF;
    CHECK(primaryRole(ROLE_LEAD_MOD | ROLE_MODERATOR, all) == ROLE_LEAD_MOD);
    CHECK(primaryRole(ROLE_STAFF | ROLE_LEAD_MOD, all) == ROLE_STAFF);
    CHECK(primaryRole(ROLE_PARTNER | ROLE_ARTIST, all) == ROLE_PARTNER);
    CHECK(primaryRole(ROLE_ARTIST | ROLE_PRIME | ROLE_TURBO, all) == ROLE_ARTIST);
    CHECK(primaryRole(ROLE_PRIME | ROLE_TURBO, all) == ROLE_PRIME);
    CHECK(primaryRole(ROLE_TURBO | ROLE_SUBSCRIBER, all) == ROLE_TURBO);
    CHECK(primaryRole(ROLE_FOUNDER | ROLE_SUBSCRIBER, all) == ROLE_FOUNDER);
    CHECK(parseBadges("founder/0") == ROLE_FOUNDER);
    CHECK(parseBadges("") == ROLE_NONE);
    CHECK(primaryRole(ROLE_NONE, 0xFF) == ROLE_NONE);
}

TEST_CASE("twitch irc: colour parse, default palette and readability") {
    uint32_t rgb = 0;
    CHECK(parseHexColor("#1E90FF", rgb));
    CHECK(rgb == 0x1E90FF);
    CHECK_FALSE(parseHexColor("", rgb));
    CHECK_FALSE(parseHexColor("1E90FF", rgb));
    CHECK_FALSE(parseHexColor("#1E90FG", rgb));

    // Deterministic, and one of the 15 palette entries.
    CHECK(defaultNameColor("someone") == defaultNameColor("someone"));
    CHECK(defaultNameColor("") == 0xFF0000);

    // Bright colours and Twitch's saturated defaults untouched; blue, navy and
    // black lifted to the floor, hue kept.
    CHECK(ensureReadable(0xFFFF00) == 0xFFFF00);
    CHECK(ensureReadable(0xFF0000) == 0xFF0000);
    CHECK(ensureReadable(0x008000) == 0x008000);
    CHECK(ensureReadable(0x0000FF) != 0x0000FF);
    const uint32_t navy = ensureReadable(0x000080);
    CHECK(rgbLuma(navy) >= 69);
    CHECK((navy & 0xFF) > ((navy >> 16) & 0xFF));  // still bluer than red
    CHECK(rgbLuma(ensureReadable(0x000000)) >= 69);
}

TEST_CASE("twitch irc: CP1252 conversion keeps Latin-1, drops emoji, collapses gaps") {
    CHECK(utf8ToCp1252("caf\xC3\xA9") == "caf\xE9");                       // é
    CHECK(utf8ToCp1252("\xE2\x82\xAC" "5") == "\x80" "5");                 // € -> 0x80
    CHECK(utf8ToCp1252("gg \xF0\x9F\x98\x82 wp") == "gg wp");             // 😂 removed
    CHECK(utf8ToCp1252("\xD0\x9F\xD1\x80\xD0\xB8") == "");                // Cyrillic
    CHECK(utf8ToCp1252("  padded  ") == "padded");
    CHECK(utf8ToCp1252("bad\xFF" "byte") == "badbyte");                    // invalid byte
    CHECK(utf8ToCp1252("cut\xE2\x82") == "cut");                           // truncated seq
    CHECK(isFullyRenderable("Ren\xC3\xA9"));
    CHECK_FALSE(isFullyRenderable("\xE6\x97\xA5"));
}

TEST_CASE("twitch irc: emote ranges index code points") {
    // "Kappa é Kappa" -- the é is 2 bytes but 1 code point, so the second range
    // (8-12) only lines up when counting code points.
    const auto cps = decodeUtf8("Kappa \xC3\xA9 Kappa");
    CHECK(codepointsToCp1252(stripEmoteRanges(cps, "25:0-4,8-12")) == "\xE9");
    // Malformed / out-of-range ranges are ignored.
    CHECK(codepointsToCp1252(stripEmoteRanges(cps, "25:x-4,3-1,90-99")) == "Kappa \xE9 Kappa");
    CHECK(codepointsToCp1252(stripEmoteRanges(cps, "")) == "Kappa \xE9 Kappa");
}

TEST_CASE("twitch irc: PRIVMSG interpretation") {
    Line l;
    REQUIRE(parseLine("@badges=moderator/1;color=;display-name=\xE6\x97\xA5\xE6\x9C\xAC;emotes=25:0-4;"
                      "id=m1;tmi-sent-ts=1700000000123 :jp_user!jp_user@x PRIVMSG #c :Kappa hi", l));
    const Event ev = interpret(l);
    CHECK(ev.kind == EventKind::Message);
    CHECK(ev.id == "m1");
    CHECK(ev.login == "jp_user");
    CHECK(ev.display == "jp_user");      // unrenderable display name -> login
    CHECK_FALSE(ev.hasColor);
    CHECK(ev.roles == ROLE_MODERATOR);
    CHECK(ev.sentMs == 1700000000123LL);
    CHECK(ev.text == "Kappa hi");
    CHECK(ev.textNoEmotes == "hi");
    CHECK_FALSE(ev.isAction);
}

TEST_CASE("twitch irc: /me actions are unwrapped before emote ranges apply") {
    Line l;
    REQUIRE(parseLine("@color=#FF0000;display-name=Bob;emotes=25:6-10 :bob!bob@x PRIVMSG #c :"
                      "\x01" "ACTION waves Kappa\x01", l));
    const Event ev = interpret(l);
    CHECK(ev.isAction);
    CHECK(ev.hasColor);
    CHECK(ev.rgb == 0xFF0000);
    CHECK(ev.text == "waves Kappa");
    CHECK(ev.textNoEmotes == "waves");
}

TEST_CASE("twitch irc: moderation and control commands") {
    Line l;
    REQUIRE(parseLine("@ban-duration=600 :tmi.twitch.tv CLEARCHAT #c :spammer", l));
    Event ev = interpret(l);
    CHECK(ev.kind == EventKind::ClearUser);
    CHECK(ev.login == "spammer");

    REQUIRE(parseLine(":tmi.twitch.tv CLEARCHAT #c", l));
    CHECK(interpret(l).kind == EventKind::ClearAll);

    REQUIRE(parseLine("@login=x;target-msg-id=m42 :tmi.twitch.tv CLEARMSG #c :bad words", l));
    ev = interpret(l);
    CHECK(ev.kind == EventKind::ClearMsg);
    CHECK(ev.id == "m42");

    REQUIRE(parseLine("@emote-only=0 :tmi.twitch.tv ROOMSTATE #c", l));
    CHECK(interpret(l).kind == EventKind::Joined);

    REQUIRE(parseLine("PING :tmi.twitch.tv", l));
    ev = interpret(l);
    CHECK(ev.kind == EventKind::Ping);
    CHECK(ev.text == "tmi.twitch.tv");

    // A suspended channel's NOTICE carries its reason in msg-id.
    REQUIRE(parseLine("@msg-id=msg_channel_suspended :tmi.twitch.tv NOTICE #gone :This channel does not exist or has been suspended.", l));
    ev = interpret(l);
    CHECK(ev.kind == EventKind::Notice);
    CHECK(ev.id == "msg_channel_suspended");

    REQUIRE(parseLine(":tmi.twitch.tv RECONNECT", l));
    CHECK(interpret(l).kind == EventKind::Reconnect);

    REQUIRE(parseLine(":tmi.twitch.tv 001 justinfan1 :Welcome, GLHF!", l));
    CHECK(interpret(l).kind == EventKind::None);
}

TEST_CASE("twitch irc: filters") {
    CHECK(isCommand("!uptime"));
    CHECK_FALSE(isCommand("hi !uptime"));
    CHECK(containsLink("check https://example.org"));
    CHECK(containsLink("go to www.example"));
    CHECK(containsLink("follow me at spam.tv/abc"));
    CHECK(containsLink("buy followers dot EXAMPLE.COM"));
    CHECK_FALSE(containsLink("that was a .com joke? no"));
    CHECK_FALSE(containsLink("gg wp"));
    CHECK(isKnownBot("nightbot"));
    CHECK_FALSE(isKnownBot("someone"));
}

TEST_CASE("twitch irc: wrapChat") {
    // Fits on the first row.
    auto rows = wrapChat("hello", 10, 20);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0] == "hello");

    // Word boundary wrap, later rows use the full width.
    rows = wrapChat("one two three four", 8, 12);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "one two");
    CHECK(rows[1] == "three four");

    // A word too long for the short first row moves down instead of splitting.
    rows = wrapChat("abcdefgh", 3, 12);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "");
    CHECK(rows[1] == "abcdefgh");

    // A word too long for any row is hard-broken.
    rows = wrapChat("abcdefghijkl", 5, 5);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0] == "abcde");
    CHECK(rows[2] == "kl");

    // Name filled the first row entirely.
    rows = wrapChat("hi", 0, 10);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0] == "");
    CHECK(rows[1] == "hi");

    // Empty body still owns its name row.
    CHECK(wrapChat("", 10, 10).size() == 1);
}
