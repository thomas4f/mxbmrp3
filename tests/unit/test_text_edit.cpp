// ============================================================================
// tests/unit/test_text_edit.cpp
// Unit tests for core/text_edit.h -- the buffer and cursor behind the chat
// channel fields: what each editing key does, and which part of a long text a
// fixed-width field shows so the cursor stays in view. The key polling that
// drives it (HotkeyManager, GetAsyncKeyState) is not reachable headless.
// Header-only, no game engine.
// ============================================================================
#include "doctest.h"

#include "core/text_edit.h"

using TextEdit::Buffer;
using TextEdit::viewStart;

TEST_CASE("text edit: typing inserts at the cursor, not at the end") {
    Buffer b;
    b.reset("rider", 25);
    CHECK(b.cursor == 5);  // editing continues where the name ends
    b.home();
    b.insert('x');
    CHECK(b.text == "xrider");
    CHECK(b.cursor == 1);
    b.right();
    b.right();
    b.insert('_');
    CHECK(b.text == "xri_der");
    b.end();
    b.insert('9');
    CHECK(b.text == "xri_der9");
}

TEST_CASE("text edit: typing stops at the length cap, wherever the cursor is") {
    Buffer b;
    b.reset("abcdefgh", 4);
    CHECK(b.text == "abcd");  // a longer start is cut to the cap
    b.home();
    b.insert('x');
    CHECK(b.text == "abcd");
    CHECK(b.cursor == 0);
}

TEST_CASE("text edit: backspace and delete remove either side of the cursor") {
    Buffer b;
    b.reset("abcd", 25);
    b.left();
    b.left();          // ab|cd
    b.backspace();
    CHECK(b.text == "acd");
    CHECK(b.cursor == 1);
    b.deleteForward();
    CHECK(b.text == "ad");
    CHECK(b.cursor == 1);

    // Nothing to remove at either end.
    b.home();
    b.backspace();
    CHECK(b.text == "ad");
    b.end();
    b.deleteForward();
    CHECK(b.text == "ad");
}

TEST_CASE("text edit: ctrl+backspace/delete clear everything before/after the cursor") {
    Buffer b;
    b.reset("oldname", 25);
    b.left();
    b.left();          // oldna|me
    b.deleteToEnd();
    CHECK(b.text == "oldna");
    CHECK(b.cursor == 5);
    b.left();          // oldn|a
    b.deleteToStart();
    CHECK(b.text == "a");
    CHECK(b.cursor == 0);
}

TEST_CASE("text edit: the cursor stays inside the text") {
    Buffer b;
    b.reset("ab", 25);
    b.right();
    CHECK(b.cursor == 2);
    b.left();
    b.left();
    b.left();
    CHECK(b.cursor == 0);
    b.end();
    CHECK(b.cursor == 2);
}

TEST_CASE("text edit: a paste replaces the whole text and ends at its end") {
    Buffer b;
    b.reset("oldname", 25);
    b.home();
    // Spliced in at the cursor, a URL would normalize to neither name.
    b.replaceAll("https://www.twitch.tv/someone");
    CHECK(b.text == "https://www.twitch.tv/someone");
    CHECK(b.cursor == b.text.size());
}

TEST_CASE("text edit: the field shows the tail, scrolled back to keep the cursor in view") {
    // Fits, with a column left for the cursor after the last character.
    CHECK(viewStart(0, 0, 10) == 0);
    CHECK(viewStart(9, 9, 10) == 0);
    // One too long: the tail, so what is being typed stays in view.
    CHECK(viewStart(10, 10, 10) == 1);
    CHECK(viewStart(30, 30, 10) == 21);
    // Walking left past the first visible character scrolls with the cursor...
    CHECK(viewStart(30, 25, 10) == 21);
    CHECK(viewStart(30, 5, 10) == 5);
    CHECK(viewStart(30, 0, 10) == 0);
    // ...and every position keeps the cursor inside the field.
    for (size_t cursor = 0; cursor <= 30; ++cursor) {
        const size_t from = viewStart(30, cursor, 10);
        CHECK(cursor >= from);
        CHECK(cursor - from < 10);
    }
}
