// ============================================================================
// core/install_prefs.h
// The choices Setup can make on the player's behalf before the plugin has ever
// run: opting OUT of analytics, and the language the player picked for Setup.
//
// WHY IT EXISTS. Analytics is on by default and is disclosed in the README, but
// a disclosure the player only sees AFTER the first ping has already gone is not
// a choice. Setup asks on its own page, and this is how the answer reaches a
// plugin whose settings file does not exist yet. The language rides along: a
// player who just read Setup in Swedish should not meet the plugin in English.
//
// WHY A FILE AND NOT THE REGISTRY. Setup relaunches itself elevated when a game
// lives somewhere unwritable, and HKCU under elevation is the ADMIN's hive, not
// the player's -- the classic way an installer preference lands in the wrong
// user's profile. A file beside the plugin is written by exactly the process
// that installs the plugin, so it cannot miss.
//
// WHY A STAMP AND NOT DELETE-ON-CONSUME (the opt-out). The nearest precedent in
// the codebase was the donation-nudge sentinel (removed in 1.31.0), which was
// consumed by deleting it -- but that file lived under savePath, which the player
// always owns. This one sits in the GAME folder, which may be Program Files: a
// plugin running unelevated can be unable to delete it. A marker that cannot be
// consumed would re-apply on every launch and permanently override the in-game
// toggle, so instead the plugin records the stamp it has already honoured in its
// own settings and ignores that marker forever after. Re-running a DIFFERENT
// version's Setup carries a new stamp and is honoured again, which is what a
// player re-running Setup and unticking the box means.
//
// ONE DIRECTION ONLY (the opt-out). The file expresses "opt out", never "opt
// in". An upgrade must not be able to switch analytics back ON for someone who
// turned it off in-game -- Setup cannot read the per-game settings file (its
// location depends on the game's configurable save path), so it cannot present
// the player's current setting and must not overrule it. Leaving the box ticked
// writes no analyticsOptOut line at all.
//
// FIRST LAUNCH ONLY (the language). Setup always opens in the Windows language
// and shows its picker on every run, so on an UPGRADE the picked language says
// nothing about a choice the player made in-game since; applying it would undo
// that choice for anyone whose game language differs from Windows'. It is
// honoured only when the plugin finds no settings file for the game, which also
// makes it self-limiting without a stamp: the first save writes that file.
//
// NO FILE AT ALL (the zip). Both installers ALWAYS write this file, so a first
// launch that finds none is a zip (or hand-copied) install, whose player was
// never asked. The survey then starts OFF, to be turned on in Settings: the
// first ping must not go out before any choice was offered. First launch only,
// like the language, so an upgrade, a file deleted later or an in-game choice
// since is never overruled.
// ============================================================================
#pragma once

#include "plugin_names.h"
#include <string>

namespace InstallPrefs {

// Where Setup leaves the file, relative to the game folder (the plugin's cwd).
constexpr const char* kPath = "plugins\\" MXB_DATA_DIR_NAME "\\install_prefs.ini";

struct Prefs {
    bool optOut = false;     // analyticsOptOut=1 was present
    std::string stamp;       // the installing version, "" when unstated
    bool hasLanguage = false;  // a language= line was present
    std::string language;      // its pack FOLDER ("sv"); "" = English ("en" in the file)
};

// Parse the file's contents. Deliberately forgiving in the same way the settings
// INI is: an unknown key, a missing section header or a comment is skipped
// rather than failing the parse, because this file can be hand-edited and a
// throw here would abort the whole settings load.
inline Prefs parse(const std::string& text) {
    Prefs p;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        const size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line.resize(comment);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto trim = [](std::string s) {
            const size_t b = s.find_first_not_of(" \t\r");
            if (b == std::string::npos) return std::string();
            const size_t e = s.find_last_not_of(" \t\r");
            return s.substr(b, e - b + 1);
        };
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key == "analyticsOptOut") p.optOut = (value == "1");
        else if (key == "stamp")      p.stamp = value;
        else if (key == "language") {
            // "en" is how Setup names English (an NSIS LangString cannot be empty);
            // the plugin's own name for it is "", no pack.
            p.hasLanguage = true;
            p.language = (value == "en") ? std::string() : value;
        }
        if (eol == text.size()) break;
    }
    return p;
}

// What to record once a marker has been honoured. Never empty, so "" can keep
// meaning "no marker has ever been applied" in the settings file.
inline std::string stampToRecord(const Prefs& p) {
    return p.stamp.empty() ? std::string("unstamped") : p.stamp;
}

// Whether this marker still has something to say. False once the stamp it WOULD
// record matches the one already honoured, which is what lets the in-game toggle
// win afterwards.
//
// Compared in recorded form rather than raw, and that is the whole subtlety: an
// opt-out with no stamp records the "unstamped" sentinel, so comparing p.stamp
// directly would both refuse to honour it the first time (""=="") and re-honour
// it on every launch afterwards ("" != "unstamped") -- silently pinning
// analytics off and overriding the in-game toggle, the exact failure this design
// exists to prevent. Pinned by test_install_prefs.cpp.
inline bool shouldApply(const Prefs& p, const std::string& seenStamp) {
    if (!p.optOut) return false;
    return stampToRecord(p) != seenStamp;
}

// Whether Setup's language should be selected. Only on the first launch (see the
// header), only when it names English or a pack that is actually installed (a
// hand-edited folder that does not exist would otherwise be STORED and show
// English for good), and only when it differs from what Windows already selects
// -- then the stored choice stays "follow Windows", which is the better default
// to keep: it follows the player to a new Windows language, and a pack installed
// later. `activeFolder` is what the plugin resolved without the file.
inline bool shouldApplyLanguage(const Prefs& p, bool freshInstall,
                                const std::string& activeFolder, bool packInstalled) {
    if (!freshInstall || !p.hasLanguage) return false;
    if (!p.language.empty() && !packInstalled) return false;
    return p.language != activeFolder;
}

// Whether the survey starts off because no Setup ran (see NO FILE AT ALL above).
inline bool optOutWithoutSetup(bool fileFound, bool freshInstall) {
    return freshInstall && !fileFound;
}

}  // namespace InstallPrefs
