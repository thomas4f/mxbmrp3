// ============================================================================
// core/pack_ini_path.h
// Where a pack's .ini lives -- one rule for every pack type.
//
// THE RULE: <root>\<name>\<type>.ini -- theme.ini, gamepad.ini, pitboard.ini,
// spotter.ini, gauge.ini. A FIXED name per type, never the pack's own name.
//
// WHY A FIXED NAME. The obvious way to author a pack is to copy a shipped one
// and rename the folder. An ini named after the pack would then carry the OLD
// name inside, producing a pack the plugin cannot see -- silently, because a
// folder with no readable ini is indistinguishable from a folder that is not a
// pack. With a fixed name, renaming the folder is the whole job.
//
// THE FALLBACK IS PERMANENT. <name>.ini is read when <type>.ini is absent, so
// a pack authored as <name>\<name>.ini keeps working untouched. It costs one
// existence check on a path the caller was about to open anyway. Do not "clean
// it up": packs are downloaded from forum posts and Discord attachments that
// outlive any release, and there is no upgrade step that can reach them.
//
// The canonical name WINS when both are present, and the caller is told so it
// can warn. An upgrade leaves the old file sitting next to the new one (the
// user-folder sync copies in, it never deletes), and without that warning the
// stale <name>.ini is an edit that silently does nothing -- the same class of
// silent failure the fixed name exists to remove.
//
// BUT ONLY FOR A PACK THE USER OWNS, which is what `userDir` decides. The
// duplicate has two very different origins and they need opposite treatment:
//
//   - the user edited a pack they authored, or copied a shipped one into their
//     own folder. Their <name>.ini is being ignored. Telling them is the entire
//     point of the warning.
//   - an older Setup shipped <name>.ini into the game's plugins folder and
//     nothing deletes it, so it sits there for every shipped pack -- and warning
//     is then noise twice per launch per pack, telling people to delete files
//     they never created.
//
// Only the first has a copy in the user's own asset tree, because that tree is
// the only place a person puts files (the same rule the gauge-art migration
// uses). Deciding it here is one rule in one place, and no install-time file
// deletion at all -- an NSIS macro re-deriving the `legacy != canonical` rule
// would delete the canonical ini of any pack named after its own type.
//
// Existence is tested by opening rather than through GetFileAttributes, so this
// header stays free of <windows.h> and compiles into the Linux unit tests. The
// calls happen at discovery and on RELOAD_CONFIG, never per frame.
// ============================================================================
#pragma once

#include <fstream>
#include <string>

namespace PackIni {

// The canonical stem per pack type. AssetManager::PACK_TYPES uses these as its
// `label` too, so the name in the sync log and the name on disk are the same
// object and cannot drift apart.
constexpr const char* kTheme    = "theme";
constexpr const char* kGamepad  = "gamepad";
constexpr const char* kPitboard = "pitboard";
constexpr const char* kSpotter  = "spotter";
constexpr const char* kGauges   = "gauge";

// The section EVERY pack type opens with -- `[pack]`, carrying `name` and `base`.
//
// Named here rather than spelled at each site because the readers match
// "<section>.<key>": a writer that spells the section differently is not an
// error, it is silently ignored -- a generated pack whose `base` line sits under
// the wrong section loses its base, fails the completeness check and vanishes.
// Anything that EMITS a pack ini builds the header from this.
constexpr const char* kSection = "pack";

// What resolve() found. `path` is always the file to read -- when NEITHER
// candidate exists it is the canonical one, so a caller's "cannot read" warning
// names the file the author should create rather than the one they no longer
// should.
struct Resolved {
    std::string path;
    bool legacy = false;    // read from <name>.ini (a pack authored pre-rename)
    bool shadowed = false;  // both exist; the <name>.ini is being ignored
};

// `dir` must already end with a separator.
inline std::string canonicalPath(const std::string& dir, const char* stem) {
    return dir + stem + ".ini";
}
inline std::string legacyPath(const std::string& dir, const std::string& packName) {
    return dir + packName + ".ini";
}

// The predicate is injected so the resolution rule itself is testable without a
// filesystem; the overload below is what callers use.
//
// `userDir` is THIS pack's folder inside the user's own asset tree, and it only
// affects `shadowed` -- which file gets read never depends on it. Empty means
// "cannot tell", and reports a shadow: a real one going unmentioned is the bug
// the flag exists for, so the unknown case takes the noisy side.
template <typename ExistsFn>
inline Resolved resolve(const std::string& dir, const std::string& packName,
                        const char* stem, ExistsFn exists,
                        const std::string& userDir = std::string()) {
    Resolved r;
    const std::string canonical = canonicalPath(dir, stem);
    const std::string legacy = legacyPath(dir, packName);

    const bool hasCanonical = exists(canonical);
    // A pack literally NAMED after its type (spotters\spotter\) resolves to one
    // path by both rules -- not a shadow, just the same file twice.
    const bool hasLegacy = (legacy != canonical) && exists(legacy);

    if (hasCanonical) {
        r.path = canonical;
        r.shadowed = hasLegacy &&
                     (userDir.empty() || exists(legacyPath(userDir, packName)));
    } else if (hasLegacy) {
        r.path = legacy;
        r.legacy = true;
    } else {
        r.path = canonical;
    }
    return r;
}

inline bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.is_open();
}

// The filesystem overload. It differs from the template above only in its 4th
// parameter -- an ExistsFn there, a userDir here -- which is sharp enough to
// state: a `const char*` in that slot binds the TEMPLATE and fails to compile
// (a const char* is not callable), rather than silently taking the other
// overload. Sharp, then, but not dangerous; pass a std::string.
inline Resolved resolve(const std::string& dir, const std::string& packName,
                        const char* stem,
                        const std::string& userDir = std::string()) {
    return resolve(dir, packName, stem, fileExists, userDir);
}

}  // namespace PackIni
