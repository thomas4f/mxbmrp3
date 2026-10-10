// ============================================================================
// tests/unit/test_system_messages.cpp
// SystemMessages (core/system_messages.h): the toast queue the achievement card
// draws as its second feed, and the bookkeeping behind the Version widget's
// startup popups.
//
// The queue rules each guard a visible failure: a key mashed five times must be
// ONE card, a HUD switched back on must withdraw its "hidden" card, and the
// hide-all hotkey's own card must sit at the front, the only place the widget
// can take it from while everything else is hidden. The popup rules guard the
// two ways a startup message goes wrong: told twice, or told where it means
// nothing (an "Updated to" on a fresh install, a welcome on an old one).
// ============================================================================
#include "doctest.h"

#define MXBMRP3_TEST_BUILD 1
#include "core/system_messages.h"

#include <cstdio>
#include <cstring>

namespace {
using Popup = SystemMessages::Popup;

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

TEST_CASE("startup popups: a fresh install owes the welcome and nothing else") {
    SystemMessages& m = fresh();
    m.onStartup(true, "1.32");
    CHECK(m.pendingPopup() == Popup::Welcome);
    CHECK(m.welcomeState() == 0);
    CHECK(m.lineToStore("1.32") == "1.32");

    // Timed out: not answered, so it comes back next launch, but not again now.
    m.expirePopup(Popup::Welcome);
    CHECK(m.pendingPopup() == Popup::None);
    CHECK(m.welcomeState() == 0);

    m.resolvePopup(Popup::Welcome);
    CHECK(m.welcomeState() == 1);
}

TEST_CASE("startup popups: an upgrade from before this feature owes 'Updated' only") {
    SystemMessages& m = fresh();
    // Existing file, no [Messages] keys: lastLine empty, welcome unknown.
    m.onStartup(false, "1.32");
    CHECK(m.pendingPopup() == Popup::Updated);
    CHECK(m.welcomeState() == 1);           // no welcome for a player who already knows
    CHECK(m.lineToStore("1.32") == "-");    // closed before it drew: owed again

    m.expirePopup(Popup::Updated);          // told once, even unanswered
    CHECK(m.pendingPopup() == Popup::None);
    CHECK(m.lineToStore("1.32") == "1.32");
}

TEST_CASE("startup popups: 'Updated' outranks a still-owed welcome, and is told once") {
    SystemMessages& m = fresh();
    m.setStoredLine("1.31");
    m.setWelcomeState(0);   // a welcome that timed out last launch
    m.onStartup(false, "1.32");
    CHECK(m.pendingPopup() == Popup::Updated);
    m.resolvePopup(Popup::Updated);
    CHECK(m.pendingPopup() == Popup::Welcome);
    m.resolvePopup(Popup::Welcome);
    CHECK(m.pendingPopup() == Popup::None);

    // Next launch, same line: nothing.
    m.onStartup(false, "1.32");
    CHECK(m.pendingPopup() == Popup::None);
}
