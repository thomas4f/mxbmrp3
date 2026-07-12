// ============================================================================
// tests/integration/harness/ini.h
// Tiny INI read/parse/perturb helper for the settings tests (reset). Just enough
// to (a) read the plugin's saved settings.ini, (b) flip a few known anchor keys
// section-aware while preserving every other line, and (c) parse a file into a
// (section,key)->value map for assertions. Not a general INI library.
// ============================================================================
#pragma once
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ini {

using Key = std::pair<std::string, std::string>;   // (section, key)
using Map = std::map<Key, std::string>;            // -> trimmed value (comment stripped)

// A section+key we deliberately mutate and then assert on.
struct Anchor { std::string section, key; };

inline std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
inline void writeFile(const std::string& path, const std::string& s) {
    std::ofstream f(path, std::ios::binary); f << s;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// Parse into (section,key)->value; values are trimmed and have any `; comment`
// stripped. Blank lines and comment lines are ignored.
inline Map parse(const std::string& text) {
    Map d; std::string sec; std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') { size_t e = t.find(']'); if (e != std::string::npos) sec = t.substr(1, e - 1); continue; }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(t.substr(0, eq));
        std::string v = t.substr(eq + 1);
        size_t sc = v.find(';'); if (sc != std::string::npos) v = v.substr(0, sc);
        d[{sec, k}] = trim(v);
    }
    return d;
}

// Return a copy of `text` with each anchor's value flipped (0<->1, else suffixed
// so it differs), section-aware, preserving every other line and any comment.
inline std::string perturb(const std::string& text, const std::vector<Anchor>& anchors) {
    std::istringstream in(text); std::ostringstream out; std::string sec, line;
    while (std::getline(in, line)) {
        std::string raw = line;
        std::string t = line; if (!t.empty() && t.back() == '\r') t.pop_back();
        std::string tl = trim(t);
        if (!tl.empty() && tl[0] == '[') {
            size_t e = tl.find(']'); if (e != std::string::npos) sec = tl.substr(1, e - 1);
            out << raw << "\n"; continue;
        }
        bool flipped = false;
        if (!tl.empty() && tl[0] != ';' && tl[0] != '#') {
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                std::string k = trim(t.substr(0, eq));
                for (const auto& a : anchors) {
                    if (a.section != sec || a.key != k) continue;
                    std::string rest = t.substr(eq + 1), val = rest, cmt;
                    size_t sc = rest.find(';'); if (sc != std::string::npos) { val = rest.substr(0, sc); cmt = rest.substr(sc); }
                    std::string v = trim(val);
                    std::string nv = (v == "0") ? "1" : (v == "1") ? "0" : v + "9";
                    out << t.substr(0, eq + 1) << nv;
                    if (!cmt.empty()) out << " " << cmt;
                    out << "\n"; flipped = true; break;
                }
            }
        }
        if (!flipped) out << raw << "\n";
    }
    return out.str();
}

// Return a copy of `text` with the top-level `version=...` line (the
// [Settings] header the plugin writes) removed — simulates an old or
// hand-edited settings.ini that has lost its version header.
inline std::string stripVersionLine(const std::string& text) {
    std::istringstream in(text); std::ostringstream out; std::string line;
    while (std::getline(in, line)) {
        std::string t = line; if (!t.empty() && t.back() == '\r') t.pop_back();
        std::string tl = trim(t);
        size_t eq = tl.find('=');
        if (eq != std::string::npos && trim(tl.substr(0, eq)) == "version") continue;
        out << line << "\n";
    }
    return out.str();
}

// Return a copy of `text` with the `version=` value replaced by `n`.
inline std::string setVersionLine(const std::string& text, int n) {
    std::istringstream in(text); std::ostringstream out; std::string line;
    while (std::getline(in, line)) {
        std::string t = line; if (!t.empty() && t.back() == '\r') t.pop_back();
        std::string tl = trim(t);
        size_t eq = tl.find('=');
        if (eq != std::string::npos && trim(tl.substr(0, eq)) == "version") {
            out << "version=" << n << "\n";
        } else {
            out << line << "\n";
        }
    }
    return out.str();
}

}  // namespace ini
