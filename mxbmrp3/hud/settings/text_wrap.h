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
// Pinned by tests/unit/test_text_wrap.cpp.
// ============================================================================
#pragma once

#include <string>
#include <vector>

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
            const size_t lastSpace = text.rfind(' ', lineEnd);
            if (lastSpace != std::string::npos && lastSpace > lineStart) {
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

}  // namespace TextWrap
