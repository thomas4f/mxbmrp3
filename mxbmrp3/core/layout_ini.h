// ============================================================================
// core/layout_ini.h
// One line of a layout/theme ini -> a section header, a key/value pair, or
// nothing. Pure and header-only so the unit suite can drive the FORMAT without
// a filesystem; layout_config.cpp owns the file loop around it.
//
// Split out when the format gained sections. The line rules (';' comments,
// whitespace anywhere, a value that is not a number) were only ever exercised
// through a real file under Wine, which is the wrong place to find out that
// "[panel ]" or "padding-x=;2" does the wrong thing.
// ============================================================================
#pragma once

#include <cstdlib>
#include <string>

enum class LayoutIniLine { Blank, Section, Pair };

// Parse one line. On Section, `section` is replaced. On Pair, `key` is the
// SCOPED key ("panel.padding-x" -- the caller's current section, then a dot) and
// `value` the parsed number.
//
// A key before any section header is passed through bare, so a one-off file that
// states a single value need not invent a header for it.
// Numeric-only wrapper over layoutParseIniLineRaw() below: a value that is not a
// number reports Blank, which is right for the layout file, where every key IS a
// number. A WRAPPER and not a second implementation -- it was a near-verbatim copy,
// and two copies of a hand-edited file's line rules is precisely the pair that
// drifts once one side is fixed.
inline LayoutIniLine layoutParseIniLineRaw(const std::string& line, std::string& section,
                                           std::string& key, float& value,
                                           std::string& raw, bool& numeric);

inline LayoutIniLine layoutParseIniLine(const std::string& line, std::string& section,
                                        std::string& key, float& value) {
    std::string raw;
    bool numeric = false;
    const LayoutIniLine kind = layoutParseIniLineRaw(line, section, key, value, raw, numeric);
    return (kind == LayoutIniLine::Pair && !numeric) ? LayoutIniLine::Blank : kind;
}

// Same line rules, but the value is handed back BOTH ways: `value`/`numeric` for a
// number, and `raw` always, holding whatever was written after the '='.
//
// The numeric-only overload above returns Blank for a value that is not a number,
// which is right for the layout file (every key there IS a number) and wrong for a
// theme's, which also carries colours (`#ff8800`) and font names. Silently dropping
// those would give a theme author an ignored line and no warning -- the exact
// failure the unknown-key logging exists to prevent.
inline LayoutIniLine layoutParseIniLineRaw(const std::string& line, std::string& section,
                                           std::string& key, float& value,
                                           std::string& raw, bool& numeric) {
    auto trimmed = [](std::string s) {
        const size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) return std::string();
        s.erase(0, b);
        s.erase(s.find_last_not_of(" \t\r") + 1);
        return s;
    };

    const std::string head = trimmed(line);
    if (head.empty() || head[0] == ';') return LayoutIniLine::Blank;

    if (head[0] == '[') {
        const size_t close = head.find(']');
        if (close == std::string::npos) return LayoutIniLine::Blank;
        section = trimmed(head.substr(1, close - 1));
        return LayoutIniLine::Section;
    }

    const size_t eq = head.find('=');
    if (eq == std::string::npos) return LayoutIniLine::Blank;
    const std::string name = trimmed(head.substr(0, eq));
    if (name.empty()) return LayoutIniLine::Blank;

    // A trailing `; comment` is stripped here and not in the numeric overload,
    // because strtof stops at the ';' by itself and a string value would otherwise
    // swallow it. `title = EnterSansman ; the display face` must not name a font
    // called "EnterSansman ; the display face".
    std::string rhs = trimmed(head.substr(eq + 1));
    const size_t semi = rhs.find(';');
    if (semi != std::string::npos) rhs = trimmed(rhs.substr(0, semi));

    const char* start = rhs.c_str();
    char* end = nullptr;
    const float parsed = std::strtof(start, &end);
    numeric = (end != start);
    value = numeric ? parsed : 0.0f;
    raw = rhs;
    key = section.empty() ? name : (section + "." + name);
    return LayoutIniLine::Pair;
}
