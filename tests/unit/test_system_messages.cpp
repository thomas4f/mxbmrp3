// ============================================================================
// tests/unit/test_system_messages.cpp
// SystemMessages (core/system_messages.h): the toast queue the achievement card
// draws as its second feed, and the bookkeeping behind the startup messages
// the Dash tells.
//
// The queue rules each guard a visible failure: a key mashed five times must be
// ONE card, a HUD switched back on must withdraw its "hidden" card, and the
// hide-all hotkey's own card must sit at the front, the only place the widget
// can take it from while everything else is hidden. The message rules guard the
// two ways a startup message goes wrong: told twice, or told where it means
// nothing (an "Updated to" on a fresh install, a welcome on an old one).
// ============================================================================
#include "doctest.h"

#define MXBMRP3_TEST_BUILD 1
#include "core/system_messages.h"

#include <cstdio>
#include <cstring>

namespace {
using Startup = SystemMessages::Startup;

SystemMessages& fresh() {
    SystemMessages& m = SystemMessages::getInstance();
    m.testReset();
    return m;
}

SystemMessages::Toast toast(int key, const char* title, bool throughHideAll = false) {
    SystemMessages::Toast t;
    std::snprintf(t.title, sizeof(t.title), "%s", title);
    t.key = key;
    t.throughHideAll = throughHideAll;
    return t;
}
}  // namespace

TEST_CASE("system messages: the same key replaces its queued card in place") {
    SystemMessages& m = fresh();
    m.post(toast(1001, "Standings hidden"));
    m.post(toast(1002, "Map hidden"));
    m.post(toast(1001, "Standings hidden again"));
    CHECK(m.posted() == 3);

    SystemMessages::Toast out;
    REQUIRE(m.take(out));
    CHECK(std::strcmp(out.title, "Standings hidden again") == 0);   // kept its place
    REQUIRE(m.take(out));
    CHECK(out.key == 1002);
    CHECK_FALSE(m.take(out));
}

TEST_CASE("system messages: a card showing a key is replaced by a newer post of it") {
    SystemMessages& m = fresh();
    m.post(toast(1001, "first"));
    SystemMessages::Toast showing;
    REQUIRE(m.take(showing));
    m.post(toast(1001, "second"));
    CHECK(m.takeReplacement(1001, showing));
    CHECK(std::strcmp(showing.title, "second") == 0);
    CHECK_FALSE(m.hasPending());
    CHECK_FALSE(m.takeReplacement(SystemMessages::KEY_NONE, showing));
}

TEST_CASE("system messages: cancel drops a queued card and ends a showing one once") {
    SystemMessages& m = fresh();
    m.post(toast(1001, "Standings hidden"));
    m.cancel(1001);
    CHECK_FALSE(m.hasPending());
    CHECK(m.consumeCancel(1001));
    CHECK_FALSE(m.consumeCancel(1001));   // once per cancel

    // A cancel aimed at the card before must not end the card that follows it.
    m.cancel(1001);
    m.post(toast(1001, "Standings hidden"));
    CHECK_FALSE(m.consumeCancel(1001));
    CHECK(m.hasPending());
}

TEST_CASE("system messages: the hide-all card goes to the front") {
    SystemMessages& m = fresh();
    m.post(toast(1001, "Standings hidden"));
    CHECK_FALSE(m.nextIsThroughHideAll());
    m.post(toast(1100, "All HUDs hidden", true));
    CHECK(m.nextIsThroughHideAll());
    SystemMessages::Toast out;
    REQUIRE(m.take(out));
    CHECK(out.key == 1100);
}

TEST_CASE("system messages: switched off drops posts and clears the queue") {
    SystemMessages& m = fresh();
    m.post(toast(1001, "queued"));
    m.setEnabled(false);
    CHECK_FALSE(m.hasPending());
    m.post(toast(1002, "dropped"));
    CHECK_FALSE(m.hasPending());
    CHECK(m.posted() == 2);   // still counted: the tests observe the post, not the card
    m.setEnabled(true);
}

TEST_CASE("system messages: the queue is bounded, oldest out") {
    SystemMessages& m = fresh();
    for (int i = 0; i < 20; ++i) m.post(toast(1000 + i, "x"));
    int n = 0;
    SystemMessages::Toast out;
    int first = -1;
    while (m.take(out)) { if (first < 0) first = out.key; ++n; }
    CHECK(n == 8);
    CHECK(first == 1012);
}

TEST_CASE("startup messages: a fresh install owes the welcome and nothing else") {
    SystemMessages& m = fresh();
    m.onStartup(true, "1.32");
    CHECK(m.pendingStartup() == Startup::Welcome);
    CHECK(m.welcomeState() == 0);
    CHECK(m.lineToStore("1.32") == "1.32");

    // Timed out: not answered, so it comes back next launch, but not again now.
    m.expireStartup(Startup::Welcome);
    CHECK(m.pendingStartup() == Startup::None);
    CHECK(m.welcomeState() == 0);

    m.resolveStartup(Startup::Welcome);
    CHECK(m.welcomeState() == 1);
}

TEST_CASE("startup messages: an upgrade from before this feature owes 'Updated' only") {
    SystemMessages& m = fresh();
    // Existing file, no [Messages] keys: lastLine empty, welcome unknown.
    m.onStartup(false, "1.32");
    CHECK(m.pendingStartup() == Startup::Updated);
    CHECK(m.welcomeState() == 1);           // no welcome for a player who already knows
    CHECK(m.lineToStore("1.32") == "-");    // closed before it drew: owed again

    m.expireStartup(Startup::Updated);          // told once, even unanswered
    CHECK(m.pendingStartup() == Startup::None);
    CHECK(m.lineToStore("1.32") == "1.32");
}

TEST_CASE("startup messages: 'Updated' outranks a still-owed welcome, and is told once") {
    SystemMessages& m = fresh();
    m.setStoredLine("1.31");
    m.setWelcomeState(0);   // a welcome that timed out last launch
    m.onStartup(false, "1.32");
    CHECK(m.pendingStartup() == Startup::Updated);
    m.resolveStartup(Startup::Updated);
    CHECK(m.pendingStartup() == Startup::Welcome);
    m.resolveStartup(Startup::Welcome);
    CHECK(m.pendingStartup() == Startup::None);

    // Next launch, same line: nothing.
    m.onStartup(false, "1.32");
    CHECK(m.pendingStartup() == Startup::None);
}

TEST_CASE("startup messages: an offered update outranks the rest, and comes back each launch") {
    SystemMessages& m = fresh();
    m.setStoredLine("1.31");
    m.onStartup(false, "1.32");
    m.offerUpdate();
    CHECK(m.pendingStartup() == Startup::Update);
    m.expireStartup(Startup::Update);       // this launch only: nothing stored
    CHECK(m.pendingStartup() == Startup::Updated);
    m.offerUpdate();                        // the next launch's check
    CHECK(m.pendingStartup() == Startup::Update);
}

TEST_CASE("startup messages: the settings menu opening answers every one") {
    SystemMessages& m = fresh();
    m.setStoredLine("1.31");
    m.setWelcomeState(0);
    m.onStartup(false, "1.32");
    m.offerUpdate();
    m.onSettingsOpened();
    CHECK(m.pendingStartup() == Startup::None);
    CHECK(m.welcomeState() == 1);
    CHECK(m.lineToStore("1.32") == "1.32");

    // Opened with nothing owed: the stored line stays as it was, so a later
    // startup on a new line still owes "Updated to".
    m.testReset();
    m.setStoredLine("1.31");
    m.onSettingsOpened();
    m.onStartup(false, "1.32");
    CHECK(m.pendingStartup() == Startup::Updated);
}
