// ============================================================================
// tests/unit/test_youtube_chat.cpp
// Unit tests for core/youtube_chat.h -- everything the YouTube chat reader
// decides without a network: what a typed or pasted channel means, what the
// channel's /live page says (live, not live, no such channel, or a page this
// build does not recognise), and what one chat poll holds.
//
// The responses are the saved examples in tests/fixtures/youtube/ (see its
// README: reconstructed from the maintained open-source readers, not captured).
// The case that matters most is the last pair: a format YouTube has changed
// must read as Unrecognized -- "Unavailable" -- never as a silent chat,
// a crash, or "not live".
// ============================================================================
#include "doctest.h"

#include "core/youtube_chat.h"

#include <fstream>
#include <iterator>
#include <string>

using namespace YouTubeChat;

namespace {
std::string fixture(const char* name) {
    const std::string path = std::string(YOUTUBE_FIXTURE_DIR) + "/" + name;
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "missing fixture " << path);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

const Chat::Event* byId(const PollInfo& p, const char* id) {
    for (const auto& e : p.events) {
        if (e.kind == Chat::EventKind::Message && e.id == id) return &e;
    }
    return nullptr;
}
}  // namespace

TEST_CASE("youtube chat: channel input is reduced to a handle or channel id") {
    CHECK(normalizeChannel("@SomeRider") == "@SomeRider");
    CHECK(normalizeChannel("  SomeRider ") == "@SomeRider");
    CHECK(normalizeChannel("https://www.youtube.com/@Some.Rider-42/live") == "@Some.Rider-42");
    CHECK(normalizeChannel("youtube.com/@SomeRider/streams") == "@SomeRider");
    CHECK(normalizeChannel("https://m.youtube.com/@SomeRider") == "@SomeRider");
    CHECK(normalizeChannel("https://www.youtube.com/channel/UCabcdefghijklmnopqrstuv") == "UCabcdefghijklmnopqrstuv");
    CHECK(normalizeChannel("UCabcdefghijklmnopqrstuv") == "UCabcdefghijklmnopqrstuv");

    // A video is not a channel: it would stop working when that stream ends,
    // and must not be misread as a handle ("@watch", "@live").
    CHECK(normalizeChannel("https://www.youtube.com/watch?v=abcDEF12345&t=10s") == "");
    CHECK(normalizeChannel("https://youtu.be/abcDEF12345?si=x") == "");
    CHECK(normalizeChannel("https://www.youtube.com/live/abcDEF12345") == "");
    CHECK(normalizeChannel("https://www.youtube.com/shorts/abcDEF12345") == "");
    CHECK(normalizeChannel("https://www.youtube.com/live_chat?is_popout=1&v=abcDEF12345") == "");
    // Nor are the legacy custom-name paths.
    CHECK(normalizeChannel("https://www.youtube.com/c/SomeRider") == "");
    CHECK(normalizeChannel("https://www.youtube.com/user/SomeRider") == "");

    // However many '@' were typed or pasted, the stored handle has one.
    CHECK(normalizeChannel("@@SomeRider") == "@SomeRider");
    CHECK(normalizeChannel(" \t@@SomeRider\r\n") == "@SomeRider");
    CHECK(normalizeChannel("https://www.youtube.com/@@SomeRider") == "@SomeRider");

    // Nothing usable: empty, too short, a channel URL with no id.
    CHECK(normalizeChannel("") == "");
    CHECK(normalizeChannel("@ab") == "");
    CHECK(normalizeChannel("https://www.youtube.com/channel/") == "");
    // A handle is capped at YouTube's 30 characters.
    CHECK(normalizeChannel("@abcdefghijklmnopqrstuvwxyz0123456789").size() == MAX_CHANNEL_LEN);
}

TEST_CASE("youtube chat: the channel field shows the handle's '@' unless the text brings its own") {
    // A typed handle: the field supplies the '@', and editing starts without it.
    CHECK(showsHandlePrefix(""));
    CHECK(showsHandlePrefix("SomeRider"));
    CHECK(editableChannel("@SomeRider") == "SomeRider");
    CHECK(normalizeChannel(editableChannel("@SomeRider")) == "@SomeRider");
    // Pasted text that is not a bare handle shows as it is.
    CHECK_FALSE(showsHandlePrefix("@SomeRider"));
    CHECK_FALSE(showsHandlePrefix("https://www.youtube.com/@SomeRider"));
    CHECK_FALSE(showsHandlePrefix("youtube.com/@SomeRider"));
    CHECK_FALSE(showsHandlePrefix("UCabcdefghijklmnopqrstuv"));
    CHECK(editableChannel("UCabcdefghijklmnopqrstuv") == "UCabcdefghijklmnopqrstuv");
    CHECK(editableChannel("") == "");
}

TEST_CASE("youtube chat: each stored form has its live page") {
    CHECK(livePagePath("@SomeRider") == "/@SomeRider/live");
    CHECK(livePagePath("UCabcdefghijklmnopqrstuv") == "/channel/UCabcdefghijklmnopqrstuv/live");
    CHECK(livePagePath("") == "");
}

TEST_CASE("youtube chat: a live page gives the video, the all-messages chat and the client") {
    const PageInfo p = parseLivePage(fixture("live_page.html"));
    REQUIRE(p.result == PageResult::Live);
    CHECK(p.videoId == "abcDEF12345");
    CHECK(p.continuation == "ALL_CHAT_TOKEN");  // "Live chat", not YouTube's default "Top chat"
    CHECK(p.clientVersion == "2.20260925.01.00");
    CHECK(p.visitorData == "CgtWaXNpdG9yRGF0YQ%3D%3D");
}

TEST_CASE("youtube chat: the chat page gives the first poll's continuation and the session") {
    // The watch page's own token drew HTTP 400 on every poll in the first live
    // test (44 characters, no visitor data sent); the chat page is where
    // YouTube's chat frame, chat-downloader and pytchat start from.
    CHECK(chatPagePath("abcDEF12345") == "/live_chat?is_popout=1&v=abcDEF12345");
    const ChatPageInfo c = parseChatPage(fixture("chat_page.html"));
    CHECK(c.continuation == "CHAT_PAGE_ALL_TOKEN");  // "Live chat", not "Top chat"
    CHECK(c.clientVersion == "2.20260928.03.00");
    // No VISITOR_DATA in ytcfg: the responseContext's visitorData is used.
    CHECK(c.visitorData == "CgtDaGF0VmlzaXRvcg%3D%3D");

    // Without the view selector, the renderer's own continuation, of any kind.
    const std::string noHeader =
        "var ytInitialData = {\"contents\":{\"liveChatRenderer\":{\"continuations\":"
        "[{\"timedContinuationData\":{\"timeoutMs\":5000,\"continuation\":\"TIMED\"}}]}}};";
    CHECK(parseChatPage(noHeader).continuation == "TIMED");

    CHECK(parseChatPage(fixture("changed_page.html")).continuation.empty());
    CHECK_FALSE(parseChatPage(fixture("changed_page.html")).noChat);
    CHECK(parseChatPage("").continuation.empty());
    CHECK_FALSE(parseChatPage("").noChat);
}

// The stream ended between the /live page (which lags the end) and the chat
// page: a page we know, with a message where the chat was. That is the chat
// ending, not a format change -- reading it as Unrecognized showed
// "Unavailable" for ten minutes after a stream went offline.
TEST_CASE("youtube chat: a chat page with no chat is noChat, a moved renderer is not") {
    const std::string ended =
        "var ytInitialData = {\"contents\":{\"messageRenderer\":{\"text\":{\"runs\":"
        "[{\"text\":\"Chat is disabled for this live stream.\"}]}}}};";
    const ChatPageInfo e = parseChatPage(ended);
    CHECK(e.continuation.empty());
    CHECK(e.noChat);

    const std::string moved =
        "var ytInitialData = {\"contents\":{\"liveChatRenderer\":{\"somethingNew\":{}}}};";
    const ChatPageInfo m = parseChatPage(moved);
    CHECK(m.continuation.empty());
    CHECK_FALSE(m.noChat);
}

TEST_CASE("youtube chat: not live, upcoming, and no such channel are told apart") {
    CHECK(parseLivePage(fixture("not_live_page.html")).result == PageResult::NotLive);
    // A scheduled stream has a video and even a waiting-room chat, but is not live.
    CHECK(parseLivePage(fixture("upcoming_page.html")).result == PageResult::NotLive);
    CHECK(parseLivePage(fixture("home_page.html")).result == PageResult::NotFound);
}

TEST_CASE("youtube chat: a page this build does not recognise is Unrecognized, not 'not live'") {
    CHECK(parseLivePage(fixture("changed_page.html")).result == PageResult::Unrecognized);
    CHECK(parseLivePage("").result == PageResult::Unrecognized);
    CHECK(parseLivePage("<html>Before you continue to YouTube</html>").result == PageResult::Unrecognized);
    // Cut off mid-object (a dropped connection): unbalanced braces, no crash.
    const std::string live = fixture("live_page.html");
    CHECK(parseLivePage(live.substr(0, live.find("\"conversationBar\""))).result == PageResult::Unrecognized);
}

TEST_CASE("youtube chat: the JSON finder matches braces outside strings only") {
    const std::string html = "x var ytInitialData = {\"a\":\"}{\\\"\",\"b\":{\"c\":1}}; y";
    size_t s = 0, e = 0;
    REQUIRE(findJsonObject(html, "ytInitialData", s, e));
    CHECK(html.substr(s, e - s) == "{\"a\":\"}{\\\"\",\"b\":{\"c\":1}}");
    // A mention in running script is not an assignment.
    CHECK_FALSE(findJsonObject("if (ytInitialData.x) {}", "ytInitialData", s, e));
}

TEST_CASE("youtube chat: a poll gives the next continuation and YouTube's delay, clamped") {
    const PollInfo p = parseChatResponse(fixture("chat_response.json"));
    REQUIRE(p.result == PollResult::Ok);
    CHECK(p.continuation == "NEXT_TOKEN_1");
    CHECK(p.delayMs == 5234);
    CHECK(clampPollDelayMs(20000) == MAX_POLL_DELAY_MS);
    CHECK(clampPollDelayMs(0) == MIN_POLL_DELAY_MS);
}

TEST_CASE("youtube chat: messages carry their author, roles, text and time") {
    const PollInfo p = parseChatResponse(fixture("chat_response.json"));
    REQUIRE(p.result == PollResult::Ok);

    const Chat::Event* owner = byId(p, "M-OWNER");
    REQUIRE(owner);
    CHECK(owner->platform == Chat::Platform::YouTube);
    CHECK(owner->display == "@TestChannel");
    CHECK(owner->login == "UCowner0000000000000000a");
    CHECK(owner->roles == Chat::ROLE_BROADCASTER);
    CHECK(owner->sentMs == 1790000000000LL);
    // Custom emoji: kept as its shortcut, or dropped -- the Twitch emote rule.
    CHECK(owner->text == "Welcome to the race :_mxbWheelie: go go");
    CHECK(owner->textNoEmotes == "Welcome to the race go go");

    const Chat::Event* mod = byId(p, "M-MOD");
    REQUIRE(mod);
    CHECK(mod->roles == Chat::ROLE_MODERATOR);
    CHECK(mod->text == "Keep it clean");  // a standard emoji cannot be drawn: dropped

    const Chat::Event* member = byId(p, "M-MEMBER");
    REQUIRE(member);
    CHECK(member->roles == Chat::ROLE_MEMBER);

    const Chat::Event* verified = byId(p, "M-VERIFIED");
    REQUIRE(verified);
    CHECK(verified->roles == Chat::ROLE_VERIFIED);

    const Chat::Event* plain = byId(p, "M-PLAIN");
    REQUIRE(plain);
    CHECK(plain->roles == Chat::ROLE_NONE);
    CHECK(plain->text == "Caf\xE9 racer! \xE5\xE4\xF6");  // Latin-1 survives as CP1252

    const Chat::Event* emojiOnly = byId(p, "M-EMOJIONLY");
    REQUIRE(emojiOnly);
    CHECK(emojiOnly->text == ":_mxbWheelie:");
    CHECK(emojiOnly->textNoEmotes.empty());  // the HUD filters it, like Twitch's emote-only lines
}

TEST_CASE("youtube chat: super chats, stickers and memberships read as lines with their amount") {
    const PollInfo p = parseChatResponse(fixture("chat_response.json"));
    const Chat::Event* super = byId(p, "M-SUPER");
    REQUIRE(super);
    CHECK(super->text == "$5.00 - Go number 42!");
    CHECK(super->display == "@GenerousFan");

    const Chat::Event* quiet = byId(p, "M-SUPER-NOTEXT");
    REQUIRE(quiet);
    CHECK(quiet->text == "\x80" "2.00");  // the euro sign is CP1252 0x80
    CHECK(quiet->textNoEmotes == quiet->text);

    const Chat::Event* sticker = byId(p, "M-STICKER");
    REQUIRE(sticker);
    CHECK(sticker->text == "$1.99 sticker");

    const Chat::Event* joined = byId(p, "M-NEWMEMBER");
    REQUIRE(joined);
    CHECK(joined->text == "Welcome to Test Channel!");
    CHECK((joined->roles & Chat::ROLE_MEMBER) != 0);
}

TEST_CASE("youtube chat: moderation becomes delete events; other kinds are skipped") {
    const PollInfo p = parseChatResponse(fixture("chat_response.json"));
    int messages = 0, clearMsg = 0, clearUser = 0;
    for (const auto& e : p.events) {
        CHECK(e.platform == Chat::Platform::YouTube);
        if (e.kind == Chat::EventKind::Message) ++messages;
        if (e.kind == Chat::EventKind::ClearMsg) { ++clearMsg; CHECK(e.id == "M-OLD1"); }
        if (e.kind == Chat::EventKind::ClearUser) { ++clearUser; CHECK(e.login == "UCspammer000000000000000"); }
    }
    // 6 text + 2 super chats + 1 sticker + 1 membership; the engagement banner,
    // the gift announcement and the ticker are not chat lines.
    CHECK(messages == 10);
    CHECK(clearMsg == 1);
    CHECK(clearUser == 1);
    CHECK(byId(p, "M-ENGAGE") == nullptr);
    CHECK(byId(p, "M-GIFT") == nullptr);
}

TEST_CASE("youtube chat: a response with nothing to continue means the stream ended") {
    CHECK(parseChatResponse(fixture("chat_ended.json")).result == PollResult::Ended);
    // Only a replay seek continuation: over, too.
    const PollInfo seek = parseChatResponse(
        "{\"responseContext\":{},\"continuationContents\":{\"liveChatContinuation\":"
        "{\"continuations\":[{\"playerSeekContinuationData\":{\"continuation\":\"x\"}}]}}}");
    CHECK(seek.result == PollResult::Ended);
    // Nothing new since the last poll is normal: no actions, still Ok.
    const PollInfo quiet = parseChatResponse(
        "{\"responseContext\":{},\"continuationContents\":{\"liveChatContinuation\":"
        "{\"continuations\":[{\"timedContinuationData\":{\"continuation\":\"n\",\"timeoutMs\":\"3000\"}}]}}}");
    CHECK(quiet.result == PollResult::Ok);
    CHECK(quiet.delayMs == 3000);
    CHECK(quiet.events.empty());
}

TEST_CASE("youtube chat: a changed response format is Unrecognized, not silence or a crash") {
    // Messages arrived in a shape the parser does not know.
    const PollInfo moved = parseChatResponse(fixture("chat_changed.json"));
    CHECK(moved.result == PollResult::Unrecognized);
    CHECK(moved.events.empty());
    // Not JSON, not an object, a renamed container.
    CHECK(parseChatResponse("").result == PollResult::Unrecognized);
    CHECK(parseChatResponse("<html>error</html>").result == PollResult::Unrecognized);
    CHECK(parseChatResponse("[1,2,3]").result == PollResult::Unrecognized);
    CHECK(parseChatResponse("{\"responseContext\":{},\"continuationContents\":{\"liveChatStream\":{}}}").result ==
          PollResult::Unrecognized);
    // Wrong types where objects are expected are skipped, never thrown on.
    const PollInfo odd = parseChatResponse(
        "{\"continuationContents\":{\"liveChatContinuation\":{\"continuations\":7,\"actions\":\"x\"}}}");
    CHECK(odd.result == PollResult::Ended);
}

TEST_CASE("youtube chat: the poll body carries the continuation and the client") {
    const std::string body = buildPollBody("TOKEN", "2.20260925.01.00", "VD");
    const Json j = Json::parse(body);
    CHECK(j["continuation"] == "TOKEN");
    CHECK(j["context"]["client"]["clientName"] == "WEB");
    CHECK(j["context"]["client"]["clientVersion"] == "2.20260925.01.00");
    CHECK(j["context"]["client"]["visitorData"] == "VD");
    CHECK_FALSE(Json::parse(buildPollBody("T", "v", ""))["context"]["client"].contains("visitorData"));
}

TEST_CASE("youtube chat: names are coloured by role, and bots are matched without the @") {
    CHECK(nameColor(Chat::ROLE_BROADCASTER | Chat::ROLE_MODERATOR) == 0xFFD600u);
    CHECK(nameColor(Chat::ROLE_MODERATOR) == 0x5E84F1u);
    CHECK(nameColor(Chat::ROLE_MEMBER) == 0x2BA640u);
    CHECK(nameColor(Chat::ROLE_NONE) == 0xAAAAAAu);
    CHECK(botKey("@Nightbot") == "nightbot");
    CHECK(ChatText::isKnownBot(botKey("@StreamElements")));
    CHECK(Chat::primaryRole(Chat::ROLE_MEMBER | Chat::ROLE_MODERATOR, 0xFFFF) == Chat::ROLE_MODERATOR);
    CHECK(Chat::primaryRole(Chat::ROLE_MEMBER | Chat::ROLE_VERIFIED, 0xFFFF) == Chat::ROLE_VERIFIED);
}

TEST_CASE("youtube chat: a refused request's body reaches the log as one printable line") {
    const std::string body = "{\n  \"error\": {\n    \"code\": 400,\n    \"message\": \"Precondition check failed.\"\xE2\x80\x94\n  }\n}\n";
    CHECK(logExcerpt(body, 300) == "{ \"error\": { \"code\": 400, \"message\": \"Precondition check failed.\"??? } }");
    CHECK(logExcerpt(body, 10) == "{ \"error\":");
    CHECK(logExcerpt(body, 10).size() == 10);
    CHECK(logExcerpt("", 300).empty());
    CHECK(logExcerpt(" \n ", 300).empty());
}

TEST_CASE("youtube chat: a response reaches the log as its shape, never its values") {
    // Logs get shared: the error's own message stays (it tells a field report
    // apart), the channel, video id, visitor data and chat text do not.
    const std::string refused = "{\n  \"error\": {\n    \"code\": 400,\n    \"message\": \"Precondition check failed.\",\n"
                                "    \"status\": \"FAILED_PRECONDITION\"\n  }\n}\n";
    CHECK(logShape(refused) == std::to_string(refused.size()) + " bytes, keys: error; error: Precondition check failed.");

    const std::string poll = R"({"responseContext":{"visitorData":"CgtTZWNyZXRWaXNpdG9y"},)"
                             R"("continuationContents":{"liveChatContinuation":{"actions":[)"
                             R"({"addChatItemAction":{"item":{"text":"hello from @PrivHandle4242","videoId":"dQw4w9WgXcQ"}}}]}}})";
    const std::string shape = logShape(poll);
    CHECK(shape == std::to_string(poll.size()) + " bytes, keys: continuationContents responseContext");
    CHECK(shape.find("PrivHandle4242") == std::string::npos);
    CHECK(shape.find("CgtTZWNyZXRWaXNpdG9y") == std::string::npos);

    CHECK(logShape("<!DOCTYPE html><title>@PrivHandle4242 - YouTube</title>") == "55 bytes, not a JSON object");
    CHECK(logShape("") == "0 bytes, not a JSON object");
}

TEST_CASE("youtube chat: an ended chat is never re-read without a wait") {
    // A watch page still reading "live" after the stream, or a members-only
    // chat, ends every attempt at once. Each attempt is ~1 MB of pages, so a
    // zero wait would be a request loop against youtube.com.
    RecheckPolicy policy;
    Recheck r = policy.next(SessionEnd::Ended, false);
    CHECK(r.show == RecheckShow::Keep);
    CHECK(r.waitS == ENDED_RECHECK_S);
    CHECK(r.waitS > 0);
    // Twice in a row without reading anything: treated as not live.
    r = policy.next(SessionEnd::Ended, false);
    CHECK(r.show == RecheckShow::NotLive);
    CHECK(r.waitS == NOT_LIVE_RECHECK_S);
    r = policy.next(SessionEnd::Ended, false);
    CHECK(r.show == RecheckShow::NotLive);

    // A chat that was read and then ended starts the count over.
    r = policy.next(SessionEnd::Ended, true);
    CHECK(r.show == RecheckShow::Keep);
    CHECK(r.waitS == ENDED_RECHECK_S);
    // So does any other outcome between two ends.
    policy.next(SessionEnd::NotLive, false);
    CHECK(policy.next(SessionEnd::Ended, false).show == RecheckShow::Keep);
}

TEST_CASE("youtube chat: failures back off, and every other outcome waits its fixed time") {
    RecheckPolicy policy;
    CHECK(policy.next(SessionEnd::NetworkError, false).waitS == FIRST_BACKOFF_S);
    CHECK(policy.next(SessionEnd::NetworkError, false).waitS == FIRST_BACKOFF_S * 2);
    for (int i = 0; i < 10; ++i) policy.next(SessionEnd::NetworkError, false);
    const Recheck capped = policy.next(SessionEnd::NetworkError, false);
    CHECK(capped.show == RecheckShow::Retrying);
    CHECK(capped.waitS == MAX_BACKOFF_S);
    // Anything that is not a failure resets the backoff.
    CHECK(policy.next(SessionEnd::NotLive, false).waitS == NOT_LIVE_RECHECK_S);
    CHECK(policy.next(SessionEnd::NetworkError, false).waitS == FIRST_BACKOFF_S);

    CHECK(policy.next(SessionEnd::NotFound, false).show == RecheckShow::NotFound);
    CHECK(policy.next(SessionEnd::Unrecognized, false).show == RecheckShow::Unavailable);
    CHECK(policy.next(SessionEnd::Unrecognized, false).waitS == SLOW_RECHECK_S);
    policy.reset();
    CHECK(policy.backoffS == 0);
    CHECK(policy.endedInRow == 0);
}
