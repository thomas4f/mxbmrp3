// ============================================================================
// core/text_edit.h
// The buffer and cursor behind the plugin's text fields (the chat channel
// boxes): what each editing key does to them, and which part of a long text a
// fixed-width field shows. HotkeyManager maps key presses onto these calls; the
// key polling itself (GetAsyncKeyState) is not reachable headless, so this is
// the part that is tested.
//
// Pinned by tests/unit/test_text_edit.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace TextEdit {

struct Buffer {
    std::string text;
    size_t cursor = 0;   // insertion point, 0..text.size()
    size_t maxLen = 0;   // typed characters stop here (a paste is capped by its caller)

    // Start editing `initial`, cursor at the end (where typing continues).
    void reset(const std::string& initial, size_t max) {
        maxLen = max;
        text = initial.substr(0, max);
        cursor = text.size();
    }
    // Replace everything (a paste is a whole name or URL), cursor at the end.
    void replaceAll(const std::string& s) {
        text = s;
        cursor = text.size();
    }

    void insert(char c) {
        if (text.size() >= maxLen) return;
        text.insert(cursor, 1, c);
        ++cursor;
    }
    void backspace() {
        if (cursor == 0) return;
        text.erase(--cursor, 1);
    }
    void deleteForward() {
        if (cursor < text.size()) text.erase(cursor, 1);
    }
    // Ctrl+Backspace / Ctrl+Delete. A channel name is one word, so "a word" is
    // everything on that side of the cursor.
    void deleteToStart() {
        text.erase(0, cursor);
        cursor = 0;
    }
    void deleteToEnd() { text.erase(cursor); }

    void left() { if (cursor > 0) --cursor; }
    void right() { if (cursor < text.size()) ++cursor; }
    void home() { cursor = 0; }
    void end() { cursor = text.size(); }
};

// First character shown in a field `width` columns wide. The cursor needs a
// column of its own at the end, so the text fits when length + 1 <= width;
// otherwise the tail is shown (what is being typed stays in view), scrolled
// left just far enough to keep the cursor inside the field.
inline size_t viewStart(size_t length, size_t cursor, size_t width) {
    if (width == 0 || length + 1 <= width) return 0;
    const size_t tailStart = length + 1 - width;
    return std::min(tailStart, cursor);
}

}  // namespace TextEdit
