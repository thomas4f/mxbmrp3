// ============================================================================
// hud/settings/text_wrap.h
// Greedy word wrap into a bounded number of fixed-width lines — the settings
// panel's tooltip/description box. Pure: strings in, strings out.
//
// WHAT THIS IS. The settings tooltip box is two lines of a monospace font, so
// wrapping is character-counted rather than measured. It is a pure function
// outside SettingsHud::rebuildRenderData() so that it is reachable from a test
// that does not build the DLL: tests/unit/test_tooltip_length.cpp exists to stop
// a shipped tooltip from rendering cut off, and it runs every shipped tooltip
// through THIS function and asserts nothing truncates — an exact check against
// the real algorithm instead of a hardcoded character ceiling that drifts
// whenever the panel width or font changes.
//
// TRUNCATION IS NOT THE SAME AS AN ELLIPSIS. When text overflows the last line
// the tail is replaced with "..." — but only if that line has more than 3
// characters to give up, so a very narrow box can truncate with NO ellipsis at
// all. `Wrapped::truncated` reports the fact directly rather than making callers
// sniff for a trailing "...", which is why the tooltip guard can be exact.
//
// wrapNotes() lays a release's markdown notes out for the Updates tab's
// scrolling list: every item whole, wrapped, never cut (rules at its own block).
//
// Pinned by tests/unit/test_text_wrap.cpp.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../../core/chat_text.h"

namespace TextWrap {

// How many lines the settings panel's tooltip/description box renders. Shared
// with tests/unit/test_tooltip_length.cpp so the shipped-tooltip guard is
// checked against the box the renderer actually draws.
constexpr int TOOLTIP_LINES = 2;

struct Wrapped {
    std::vector<std::string> lines;
    // True when the text did not fit and characters were dropped — independent
    // of whether an ellipsis was appended (see the header note).
    bool truncated = false;
};

// Wraps `text` to at most `maxLines` lines of at most `maxCharsPerLine`
// characters, breaking on spaces where possible and hard-breaking a word that is
// longer than a line. `maxCharsPerLine <= 0` yields empty lines rather than
// looping forever; `maxLines` bounds the work in every case.
inline Wrapped wrap(const std::string& text, int maxCharsPerLine, int maxLines) {
    Wrapped out;
    if (maxLines <= 0) {
        out.truncated = !text.empty();
        return out;
    }

    const size_t width = maxCharsPerLine > 0 ? static_cast<size_t>(maxCharsPerLine) : 0;
    size_t lineStart = 0;
    int lineCount = 0;

    while (lineStart < text.length() && lineCount < maxLines) {
        std::string line;
        const size_t lineEnd = lineStart + width;

        if (lineEnd >= text.length()) {
            // Everything that is left fits on this line.
            line = text.substr(lineStart);
            lineStart = text.length();
        } else {
            // rfind is inclusive of lineEnd, so a space sitting exactly one past
            // the last visible character still counts as a clean break — that is
            // what lets a line use its full width.
            // Searched down to lineStart only: rfind would run on to the string's
            // start, making a long text with no spaces quadratic.
            size_t lastSpace = std::string::npos;
            for (size_t k = lineEnd; k > lineStart; --k) {
                if (text[k] == ' ') { lastSpace = k; break; }
            }
            if (lastSpace != std::string::npos) {
                line = text.substr(lineStart, lastSpace - lineStart);
                lineStart = lastSpace + 1;  // consume the space itself
            } else {
                // A single word longer than the line (or a leading space): break it.
                line = text.substr(lineStart, width);
                lineStart += width;
            }

            // On the final line, mark the overflow. Only a line with something to
            // spare can carry the ellipsis; a shorter one is truncated silently,
            // which is why `truncated` is reported separately.
            if (lineCount == maxLines - 1 && lineStart < text.length()) {
                if (line.length() > 3) {
                    line.resize(line.length() - 3);
                    line += "...";
                }
            }
        }

        out.lines.push_back(line);
        lineCount++;
    }

    out.truncated = (lineStart < text.length());
    return out;
}

// ---------------------------------------------------------------------------
// Release notes: a release's markdown body laid out for the Updates tab.
// ---------------------------------------------------------------------------
// The body is the CHANGELOG.md section plus what release.yml appends (a
// Downloads table, the build and attestation lines), fetched from GitHub, so it
// is treated as untrusted text: one pass over at most NOTES_MAX_BYTES, no
// recursion, every search bounded, at most NOTES_MAX_LINES lines out.
//
// What it does with markdown, and nothing more:
// - The changelog is hard-wrapped at 74 columns, so the lines of one paragraph
//   or list item are joined before wrapping to the panel's width; a blank
//   line, a header, a table row or a new item ends it.
// - "# ..." to "###### ..." become heading lines (Added, Fixed, ...), with an
//   empty line before each but the first. "### Downloads" ends the notes:
//   from there on the body is for the GitHub page only.
// - Table rows ("|...") and rules ("---") are dropped, and so is a line that
//   starts with an image ("![" or a linked image "[!["): the download buttons
//   release.yml puts at the top are pictures, with nothing to read in game.
// - "**" and "`" are removed and a link "[text](url)" keeps its text; anything
//   that does not close (a lone "[", "](" without ")") stays as typed.
// - The text is turned into the font's code page (ChatText::utf8ToCp1252), so
//   an accent draws and an emoji, which the font cannot draw, is dropped.

constexpr size_t NOTES_MAX_BYTES = 64 * 1024;
constexpr size_t NOTES_MAX_LINES = 600;
// How far a link's "](" and ")" are looked for: a link longer than this is
// left as typed, which keeps a body full of "[" linear.
constexpr size_t NOTES_LINK_SPAN = 512;

struct NoteLine {
    std::string text;
    bool heading = false;
};

// "**bold**", "`code`" and "[text](url)" down to their text.
inline std::string stripNoteMarkup(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '`') { ++i; continue; }
        if (c == '*' && i + 1 < s.size() && s[i + 1] == '*') { i += 2; continue; }
        if (c == '[') {
            // Each search stops NOTES_LINK_SPAN on, so a body of lone "[" stays linear.
            size_t mid = std::string::npos, close = std::string::npos;
            for (size_t k = i + 1; k + 1 < s.size() && k < i + NOTES_LINK_SPAN; ++k) {
                if (s[k] == ']' && s[k + 1] == '(') { mid = k; break; }
            }
            if (mid != std::string::npos) {
                for (size_t k = mid + 2; k < s.size() && k < mid + 2 + NOTES_LINK_SPAN; ++k) {
                    if (s[k] == ')') { close = k; break; }
                }
            }
            if (close != std::string::npos) {
                for (size_t k = i + 1; k < mid; ++k)
                    if (s[k] != '`' && s[k] != '*') out += s[k];
                i = close + 1;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// A release's notes as display lines `maxCharsPerLine` wide. A list item
// ("- " / "* ") wraps under its own text, two spaces in, so a long item still
// reads as one; nothing is cut.
inline std::vector<NoteLine> wrapNotes(const std::string& notes, int maxCharsPerLine) {
    std::vector<NoteLine> out;
    if (maxCharsPerLine <= 2) return out;
    const size_t end = std::min(notes.size(), NOTES_MAX_BYTES);

    std::string block;        // the paragraph or item being joined
    bool blockItem = false;
    auto emit = [&](std::string text, bool heading) {
        if (out.size() < NOTES_MAX_LINES) out.push_back({std::move(text), heading});
    };
    auto flush = [&]() {
        if (block.empty()) return;
        const std::string text = ChatText::utf8ToCp1252(stripNoteMarkup(block));
        block.clear();
        const std::string prefix = blockItem ? "- " : "";
        const int w = maxCharsPerLine - static_cast<int>(prefix.size());
        // A line holds at least one char, so the text's own length bounds the lines.
        const Wrapped wr = wrap(text, w, static_cast<int>(text.size()) + 1);
        for (size_t i = 0; i < wr.lines.size(); ++i)
            emit((i == 0 ? prefix : std::string(prefix.size(), ' ')) + wr.lines[i], false);
    };

    size_t at = 0;
    while (at < end && out.size() < NOTES_MAX_LINES) {
        size_t nl = notes.find('\n', at);
        if (nl == std::string::npos || nl > end) nl = end;
        size_t a = at, b = nl;
        at = nl + 1;
        while (a < b && (notes[a] == ' ' || notes[a] == '\t')) ++a;
        while (b > a && (notes[b - 1] == '\r' || notes[b - 1] == ' ' || notes[b - 1] == '\t')) --b;
        if (a == b) { flush(); continue; }
        const std::string line = notes.substr(a, b - a);

        if (line[0] == '#') {
            flush();
            size_t h = line.find_first_not_of('#');
            h = (h == std::string::npos) ? line.size() : line.find_first_not_of(' ', h);
            const std::string title = (h == std::string::npos) ? std::string() : line.substr(h);
            if (title == "Downloads") break;
            const std::string text = ChatText::utf8ToCp1252(stripNoteMarkup(title));
            if (text.empty()) continue;
            if (!out.empty()) emit(std::string(), false);
            const Wrapped wr = wrap(text, maxCharsPerLine, static_cast<int>(text.size()) + 1);
            for (const std::string& l : wr.lines) emit(l, true);
            continue;
        }
        if (line[0] == '|' || line.find_first_not_of("-*_ ") == std::string::npos) {
            flush();   // a table row, or a rule ("---") -- three or more by markdown's rule, any here
            continue;
        }
        if (line.compare(0, 2, "![") == 0 || line.compare(0, 3, "[![") == 0) {
            flush();   // a line of images (the download buttons): no text to show
            continue;
        }
        if (line.size() > 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
            flush();
            block = line.substr(2);
            blockItem = true;
            continue;
        }
        if (block.empty()) blockItem = false;
        else block += ' ';
        block += line;
    }
    flush();
    return out;
}

// The Updates tab's notes as laid out, kept until the checker stores new notes
// (its generation moves) or the width changes: one parse per release, never
// one per rebuild -- a scroll glide rebuilds the tab every frame. `fetch`
// returns the body and runs only on a miss, so the body is not copied either.
class NotesCache {
public:
    template <class Fetch>
    const std::vector<NoteLine>& lines(uint32_t generation, int chars, Fetch&& fetch) {
        if (!m_valid || generation != m_generation || chars != m_chars) {
            m_lines = wrapNotes(fetch(), chars);
            m_generation = generation;
            m_chars = chars;
            m_valid = true;
            ++m_parses;
        }
        return m_lines;
    }
    int parses() const { return m_parses; }   // for the tests

private:
    std::vector<NoteLine> m_lines;
    uint32_t m_generation = 0;
    int m_chars = 0;
    bool m_valid = false;
    int m_parses = 0;
};

}  // namespace TextWrap
