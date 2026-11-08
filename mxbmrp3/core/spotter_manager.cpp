// ============================================================================
// core/spotter_manager.cpp
// See the header for the design. This file keeps the singleton, the hazard
// tuning setters, cue-pack loading and the cue-log getters; the event cues,
// cue composition, proximity engine and audio worker live in the
// spotter_manager_*.cpp TUs beside it.
// ============================================================================
#include "spotter_manager.h"
#include "plugin_names.h"

#include "asset_manager.h"   // userAssetDir(), for the shadow warning below
#include "chat_text.h"       // cp1252ToUtf8, for spokenText
#include "i18n.h"
#include "pack_ini_path.h"
#include "../diagnostics/logger.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

// Packs live next to the other user asset packs; file I/O is relative to the
// game working directory like the web root and the temp-hook wav path.
constexpr const char* kPackRoot = "plugins\\" MXB_DATA_DIR_NAME "\\spotters\\";
// The shipped pack, which is the ONLY place the spotter's wording lives —
// every other pack is an overlay on it. See reloadCuePack.
constexpr const char* kBasePackName = "default";
// A language pack's spotter lines: languages\<folder>\spotter.ini, beside its
// strings.po, so a translator ships one folder (see reloadCuePack).
constexpr const char* kLanguageSpotterFile = "spotter.ini";

// Default join between stitched chunks, for a pack that does not state one.
// The low end of the 60-90ms range reads best once the radio compression
// flattens the joins. A pack tunes this — including negative, for
// overlapping joins — with `[Mix] gap_ms`; see spotter_cue_pack.h.
constexpr int kMixGapMs = 60;

}  // namespace


SpotterManager& SpotterManager::getInstance() {
    static SpotterManager instance;
    return instance;
}


// The eight hazard setters below all clamp, and three of them clamp to the
// same cooldown range.
namespace {
constexpr int kCooldownMinMs = 5000, kCooldownMaxMs = 300000;
}

void SpotterManager::setBehindOnMeters(float m) {
    // The SAME range the stepper enforces, which is what spotter_manager.h
    // promises: otherwise a hand-edited behind_on_m=90 loads as 90 and then
    // snaps to 60 on the first click, so the INI and the UI disagree about what
    // a valid value is.
    m_hazardCfg.behindOnMeters = std::clamp(m, 2.0f, 60.0f);
    if (m_hazardCfg.clearMeters < m_hazardCfg.behindOnMeters + 5.0f) {
        m_hazardCfg.clearMeters = m_hazardCfg.behindOnMeters + 5.0f;
    }
}

void SpotterManager::setClearMeters(float m) {
    // The hysteresis band is the contract: clear must sit above behind-on.
    m_hazardCfg.clearMeters =
        std::clamp(m, m_hazardCfg.behindOnMeters + 5.0f, 200.0f);
}

void SpotterManager::setAlongsideOnMeters(float m) {
    m_hazardCfg.alongsideOnMeters = std::clamp(m, 1.0f, 20.0f);
    if (m_hazardCfg.alongsideClearMeters <
        m_hazardCfg.alongsideOnMeters + 2.0f) {
        m_hazardCfg.alongsideClearMeters =
            m_hazardCfg.alongsideOnMeters + 2.0f;
    }
}

void SpotterManager::setAlongsideAheadMeters(float m) {
    // 0 is meaningful and is the point of the knob: call nobody you could see
    // by turning your head. The ceiling matches the alongside-on range, so a
    // symmetric window stays reachable for anyone who prefers it.
    m_hazardCfg.alongsideAheadMeters = std::clamp(m, 0.0f, 20.0f);
}

void SpotterManager::setAlongsideClearMeters(float m) {
    // Same hysteresis contract as behind/clear, at alongside scale.
    m_hazardCfg.alongsideClearMeters =
        std::clamp(m, m_hazardCfg.alongsideOnMeters + 2.0f, 40.0f);
}

void SpotterManager::setLateralMeters(float m) {
    // Down to 3m is "same rut only"; 60 is most of a start straight. Wider
    // than that is along-track-only detection, which this gate exists to
    // prevent.
    m_hazardCfg.lateralMeters = std::clamp(m, 3.0f, 60.0f);
}

void SpotterManager::setBehindRepeatMs(int ms) {
    m_hazardCfg.behindRepeatMs = std::clamp(ms, 2000, 120000);
}

void SpotterManager::setClearMinEpisodeMs(int ms) {
    // 0 is meaningful: voice every clear, however short the episode.
    m_hazardCfg.clearMinEpisodeMs = std::clamp(ms, 0, 60000);
}

void SpotterManager::setBlueFlagCooldownMs(int ms) {
    m_hazardCfg.blueFlagCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setLappingCooldownMs(int ms) {
    m_hazardCfg.lappingCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setHazardCooldownMs(int ms) {
    m_hazardCfg.hazardCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setPackName(const std::string& name) {
    m_packName = name;
    reloadCuePack();
}

namespace {
// Read one pack folder's ini. Returns false when the folder or file is not
// there, which is not an error for an OVERLAY (a stored name whose folder was
// removed) and is close to fatal for the base (see reloadCuePack).
bool readPackFile(const std::string& name, SpotterCuePack::Pack& out,
                  std::string& dirOut) {
    // Reject names that could escape the pack root — the name reaches us from
    // the INI, which is hand-editable.
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos) {
        DEBUG_WARN_F("Spotter: invalid pack name '%s'", name.c_str());
        return false;
    }
    const std::string dir = std::string(kPackRoot) + name;
    // spotter.ini, falling back to the pre-rename <name>.ini -- see
    // core/pack_ini_path.h. A recorded pack is a forum download that no upgrade
    // step can reach, so the old name is read forever.
    // The user's own copy of this pack, so the shadow warning below fires only
    // for a duplicate a PERSON made -- not for one an older Setup left in the
    // game folder. See PackIni::resolve.
    const std::string& userRoot = AssetManager::getInstance().userAssetDir();
    const std::string userDir =
        userRoot.empty() ? std::string()
                         : userRoot + "\\" + AssetManager::SPOTTERS_SUBDIR + "\\" + name + "\\";
    const PackIni::Resolved ini =
        PackIni::resolve(dir + "\\", name, PackIni::kSpotter, userDir);
    if (ini.shadowed) {
        DEBUG_WARN_F("Spotter: pack '%s' has both spotter.ini and %s.ini - reading "
                     "spotter.ini; the other is ignored and can be deleted",
                     name.c_str(), name.c_str());
    }
    const std::string& iniPath = ini.path;
    try {
        std::ifstream file(iniPath, std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = SpotterCuePack::parse(buffer.str());
        dirOut = dir;
        return true;
    } catch (...) {
        DEBUG_WARN_F("Spotter: failed to read pack '%s'", name.c_str());
        return false;
    }
}
// The active language's spotter.ini, if it has one. False (silently) when it
// does not: most languages translate the menus only, and the spotter then
// speaks English.
bool readLanguageFile(const std::string& folder, SpotterCuePack::Pack& out) {
    if (folder.empty() || folder.find('/') != std::string::npos ||
        folder.find('\\') != std::string::npos ||
        folder.find("..") != std::string::npos) {
        return false;
    }
    const std::string path = std::string(AssetManager::DISCOVERY_DIR) + "\\" +
                             AssetManager::LANGUAGES_SUBDIR + "\\" + folder +
                             "\\" + kLanguageSpotterFile;
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = SpotterCuePack::parse(buffer.str());
        return true;
    } catch (...) {
        DEBUG_WARN_F("Spotter: failed to read language file '%s'", path.c_str());
        return false;
    }
}
}  // namespace

void SpotterManager::onLanguageChanged() {
    if (I18n::activeFolder() == m_languageLoadedFor) return;
    reloadCuePack();
}

std::string SpotterManager::spokenText(const char* english) const {
    if (!english) return std::string();
    if (m_spokenLanguage.empty()) return english;
    return ChatText::cp1252ToUtf8(I18n::tr(english));
}

// The active vocabulary: the SHIPPED pack's words, with the selected pack's
// laid over them.
//
// There is no third copy in C++. A built-in phrase per cue, with a pack line
// merely overriding it, means commenting a row out of a pack changes nothing
// (the hidden copy still speaks), a "None" picker entry whose only meaning is
// "use the copy you cannot edit", and a census to keep the two in step.
// mxbmrp3_data/spotters/default/spotter.ini is the only place the spotter's
// words exist, and an absent row is silence.
//
// WHAT LAYERS AND WHAT DOES NOT. Phrases layer, so a recorded pack covering
// twenty cues still speaks the other forty in the shipped wording rather than
// falling mute. Audio does NOT: wavs, mixes and the join gap come from the
// selected pack alone, because a clip is only findable relative to the folder
// it shipped in — the same reason a _2 variant plays only its own audio.
//
// THE LANGUAGE LAYER goes over the SHIPPED pack only: the active language's
// spotter.ini (languages\<folder>\), when no other voice pack is selected. A
// cue the translation leaves out is still spoken (in English), and a
// translated cue owns its alternates exactly as a selected pack's does. Its
// [Words] replace the words the plugin assembles (SpotterPhrase::say); a
// voice pack's [Words] are ignored. With a voice pack selected the spotter
// speaks that pack, in English - the block below says why.
void SpotterManager::reloadCuePack() {
    m_pack = SpotterCuePack::Pack{};
    m_packDir.clear();
    m_packDisplayName.clear();
    // Reset the worker's copy up front so every failure path below leaves the
    // default join rather than the previous pack's.
    m_mixGapPublished.store(kMixGapMs, std::memory_order_relaxed);

    // An empty name is an older settings file, where "" means the built-in
    // wording; the shipped pack is that wording, so read it as such.
    if (m_packName.empty()) m_packName = kBasePackName;

    std::string baseDir;
    SpotterCuePack::Pack base;
    if (!readPackFile(kBasePackName, base, baseDir)) {
        // Nothing else can supply words. Startup re-copies the shipped assets,
        // so this is a deleted or unreadable file rather than a fresh install,
        // and saying so beats a spotter that is silently mute.
        DEBUG_WARN_F("Spotter: shipped pack '%s' missing - the spotter has no "
                     "wording and will stay silent", kBasePackName);
    }

    // OVERLAID MEANS "AND IT LOADED", not merely "a different name is stored".
    // On the name comparison alone a missing folder still takes the audio
    // branch below with an EMPTY pack and an empty directory -- the log says
    // "using the shipped wording" while the shipped pack's wavs, mixes and join
    // gap are silently dropped. Inert only while `default` is text only; the
    // first shipped pack with clips would land a user whose folder went missing
    // on TTS, i.e. silence under Wine, which is the exact case the layering
    // exists to prevent.
    bool overlaid = m_packName != kBasePackName;
    SpotterCuePack::Pack sel;
    std::string selDir;
    if (overlaid && !readPackFile(m_packName, sel, selDir)) {
        // Keep the stored name (see the header): the folder may come back, and
        // rewriting the setting would lose the user's choice permanently.
        DEBUG_WARN_F("Spotter: pack '%s' not found - using the shipped wording",
                     m_packName.c_str());
        overlaid = false;
    }

    // The language layer, for the SHIPPED pack only. A voice pack you picked
    // speaks its own words -- a recorded one has English clips, and even a
    // text one was written in one language -- and the [Words] are global, so
    // laying them under it would fill its English lines with Swedish numbers
    // ("Rider sju behind"). One sentence, one language: with a voice pack
    // selected the spotter stays English.
    const std::string previousLanguage = m_spokenLanguage;
    m_languageLoadedFor = I18n::activeFolder();
    m_spokenLanguage.clear();
    SpotterPhrase::spokenWords().clear();
    SpotterCuePack::Pack lang;
    if (!overlaid && readLanguageFile(m_languageLoadedFor, lang)) {
        m_spokenLanguage = m_languageLoadedFor;
        base.phrases = SpotterCuePack::mergePhrases(base, lang);
        for (const auto& kv : lang.words) SpotterPhrase::spokenWords()[kv.first] = kv.second;
        DEBUG_INFO_F("Spotter: language '%s' (%zu lines, %zu words)",
                     m_spokenLanguage.c_str(), lang.phrases.size(), lang.words.size());
    } else if (overlaid && !m_languageLoadedFor.empty()) {
        DEBUG_INFO_F("Spotter: voice pack '%s' selected - speaking its own "
                     "words, not language '%s'", m_packName.c_str(),
                     m_languageLoadedFor.c_str());
    }
    if (m_spokenLanguage != previousLanguage) publishTtsVoice();

    // The picker's LABEL only. Cycling and the stored setting stay FOLDER names
    // (a pack is stored by name, never by index or title -- the asset-pack
    // invariant), so a pack that renames itself can never reassign anyone's
    // choice. Falls back to the folder name.
    // Only the pack the player actually SELECTED may name itself. `overlaid` is
    // cleared when the chosen folder is missing (see above), and reading
    // base.displayName then would put the SHIPPED pack's title on a selection
    // that is not it -- the picker would claim the folder came back. The stored
    // name is the honest answer there. Latent only because no shipped pack
    // declares a name; the docs invite one.
    const bool selfNamed = (m_packName == kBasePackName) || overlaid;
    m_packDisplayName = overlaid ? sel.displayName
                                 : (selfNamed ? base.displayName : std::string());
    if (m_packDisplayName.empty()) m_packDisplayName = m_packName;

    // Words: shipped first, selected over the top, with a selected cue owning
    // its alternates — see SpotterCuePack::mergePhrases, which is where that
    // rule and its reason live.
    m_pack.phrases = overlaid ? SpotterCuePack::mergePhrases(base, sel)
                              : base.phrases;
    // Audio: the selected pack's own, whichever that is.
    const SpotterCuePack::Pack& audio = overlaid ? sel : base;
    m_pack.wavs = audio.wavs;
    m_pack.mixes = audio.mixes;
    m_pack.interruptFillers = audio.interruptFillers;
    m_pack.leadIns = audio.leadIns;
    m_pack.leadInChance = audio.leadInChance;
    m_pack.hasPositionRows = SpotterCuePack::anyPositionRows(m_pack);
    m_packDir = overlaid ? selDir : baseDir;
    if (audio.hasGapMs) {
        m_mixGapPublished.store(audio.gapMs, std::memory_order_relaxed);
    }

    DEBUG_INFO_F("Spotter: pack '%s' (%zu phrases, %zu wavs, join %dms)",
                 m_packName.c_str(), m_pack.phrases.size(), m_pack.wavs.size(),
                 m_mixGapPublished.load(std::memory_order_relaxed));

    // A row the plugin cannot act on is a TYPO nine times out of ten (or a key
    // that moved between versions). parse() is a FORMAT parser and stays
    // tolerant — it keeps the row, which is harmless because nothing ever
    // looks it up — but staying quiet about it is what leaves an author
    // waiting for a line that will never play.
    // NAMED BY THE PACK THE ROW CAME FROM, not by the selected one: these maps
    // are MERGED (shipped words with the selection over them), so attributing
    // every unknown key to m_packName reports a typo in the shipped spotter.ini
    // against the user's own pack -- and sends them looking in a file they did
    // not write.
    auto reportUnknown = [&](const std::string& key, const char* from) {
        if (SpotterCuePack::isCueKey(SpotterCuePack::cueRoot(key))) {
            return;
        }
        DEBUG_WARN_F("Spotter: pack '%s' has no cue named '%s' - that line "
                     "will never be spoken", from, key.c_str());
    };
    const char* selName = m_packName.c_str();
    const std::string langName = "languages\\" + m_spokenLanguage;
    auto whose = [&](const std::string& key) {
        if (overlaid && sel.phrases.count(key)) return selName;
        return lang.phrases.count(key) ? langName.c_str() : kBasePackName;
    };
    // Audio comes from ONE pack, so its rows need no per-key lookup -- but which
    // pack that is depends on whether the overlay loaded, and `overlaid` is
    // false when the folder is missing. Naming the selection unconditionally is
    // the misattribution above again: base rows blamed on a pack that is not
    // there.
    const char* audioName = overlaid ? selName : kBasePackName;
    for (const auto& kv : m_pack.phrases) reportUnknown(kv.first, whose(kv.first));
    for (const auto& kv : m_pack.wavs) reportUnknown(kv.first, audioName);
    for (const auto& kv : m_pack.mixes) reportUnknown(kv.first, audioName);

    // A mix recipe naming a placeholder the mixer does not know is dropped by
    // parse(), and dropping it is invisible from the outside: the cue still
    // speaks, through TTS, which on Wine is silence. Say so.
    for (const std::string& row : base.rejectedMixes) {
        DEBUG_WARN_F("Spotter: pack '%s' mix %s names something the mixer "
                     "cannot resolve - that recipe is dropped and the cue "
                     "falls back to speech", kBasePackName, row.c_str());
    }
    for (const std::string& row : sel.rejectedMixes) {
        DEBUG_WARN_F("Spotter: pack '%s' mix %s names something the mixer "
                     "cannot resolve - that recipe is dropped and the cue "
                     "falls back to speech", m_packName.c_str(), row.c_str());
    }
}

std::vector<std::string> SpotterManager::listAvailablePacks() const {
    std::vector<std::string> packs;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(kPackRoot) + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return packs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        packs.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(packs.begin(), packs.end());
    return packs;
}

void SpotterManager::setCategoryEnabled(SpotterPhrase::Category cat, bool on) {
    const uint32_t bit = 1u << static_cast<unsigned>(cat);
    if (on) m_categoryMask |= bit;
    else    m_categoryMask &= ~bit;
}

void SpotterManager::setVolume(int v) {
    m_volume = v;
    publishAudioSettings();
}

void SpotterManager::setSpeed(float sp) {
    m_speed = sp;
    publishAudioSettings();
}


std::deque<SpotterLogEntry> SpotterManager::getCueLog() const {
    MutexLock lock(m_mutex);
    return m_cueLog;
}

std::string SpotterManager::getLatestCueText() const {
    MutexLock lock(m_mutex);
    return m_cueLog.empty() ? std::string() : m_cueLog.back().text;
}

