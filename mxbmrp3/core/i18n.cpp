// ============================================================================
// core/i18n.cpp
// The language packs on disk: listing mxbmrp3_data\languages\, reading a pack's
// language.ini and strings.po into a Catalog, and switching the active one.
// The lookup itself is header-only (i18n.h / i18n_catalog.h). Runs at startup,
// on a Language pick and on RELOAD_CONFIG -- never per frame.
// ============================================================================
#include "i18n.h"
#include "asset_manager.h"
#include "hud_manager.h"
#include "layout_config.h"
#include "pack_ini_path.h"
#include "../diagnostics/logger.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace I18n {

namespace {

// <DISCOVERY_DIR>\languages\ -- where the Documents copy lands too (AssetManager
// syncs it through PACK_TYPES), so this is the one folder to read.
std::string packRoot() {
    return std::string(AssetManager::DISCOVERY_DIR) + "\\" + AssetManager::LANGUAGES_SUBDIR + "\\";
}

constexpr const char* kStringsFile = "strings.po";
// A base chain longer than this is a loop someone wrote by hand.
constexpr int kMaxBaseDepth = 4;

struct PackIdentity {
    std::string name;   // UTF-8 as written
    std::string base;
    bool pseudo = false;
};

PackIdentity readIdentity(const std::string& folder) {
    PackIdentity id;
    const std::string dir = packRoot() + folder + "\\";
    layoutForEachIniPairRaw(PackIni::canonicalPath(dir, PackIni::kLanguage),
        [](const char* key, float value, const char* raw, bool numeric, void* ctx) -> bool {
            PackIdentity& out = *static_cast<PackIdentity*>(ctx);
            if (std::strcmp(key, PackIni::kKeyName) == 0) out.name = raw;
            else if (std::strcmp(key, PackIni::kKeyBase) == 0) out.base = raw;
            else if (std::strcmp(key, "pack.pseudo") == 0) out.pseudo = numeric && value != 0.0f;
            return true;
        }, &id);
    return id;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::vector<LanguageInfo> s_languages;
bool s_listed = false;

// Base first, then the pack itself, so what the pack states wins.
void loadChain(const std::string& folder, Catalog& cat, int depth) {
    const PackIdentity id = readIdentity(folder);
    const std::string base = baseFolder(id.base, folder, s_languages);
    if (!id.base.empty() && base.empty() && id.base != folder) {
        DEBUG_WARN_F("I18n: language '%s' names base '%s', which is not an installed language - ignored",
                     folder.c_str(), id.base.c_str());
    }
    if (!base.empty()) {
        if (depth < kMaxBaseDepth) loadChain(base, cat, depth + 1);
        else DEBUG_WARN_F("I18n: language '%s' base chain is too deep; '%s' ignored",
                          folder.c_str(), base.c_str());
    }
    std::string text;
    if (!readFile(packRoot() + folder + "\\" + kStringsFile, text)) {
        if (!id.pseudo) DEBUG_WARN_F("I18n: language '%s' has no %s", folder.c_str(), kStringsFile);
        return;
    }
    const LoadStats s = loadPo(text, cat);
    DEBUG_INFO_F("I18n: language '%s': %d strings (%d fuzzy, %d with characters the font cannot draw, "
                 "%d whose %% codes differ from the English, %d longer than their room - all shown in English)",
                 folder.c_str(), s.loaded, s.fuzzy, s.unrenderable, s.badFormat, s.tooLong);
}

// The shipped template's "#. max N" notes, read with the pack list.
std::unordered_map<std::string, int> s_limits;
std::string s_folder = kSystemLanguage;             // what the INI stores
bool s_loaded = false;                              // s_folder has been resolved
std::string s_activeDisplayName = "English";
std::string s_activeFolder;                         // the pack in effect, "" = English
// Every catalogue ever activated, kept for the session: a pointer one handed out
// may still sit in a slot or in the Find index until they next re-resolve.
std::vector<std::unique_ptr<Catalog>> s_catalogs;

void listPacks() {
    s_languages.clear();
    s_listed = true;
    std::string pot;
    s_limits = readFile(packRoot() + "template.pot", pot) ? parseLimits(pot) : std::unordered_map<std::string, int>();
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((packRoot() + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        LanguageInfo info;
        info.folder = fd.cFileName;
        const PackIdentity id = readIdentity(info.folder);
        const std::string shown = ChatText::utf8ToCp1252(id.name);
        info.displayName = shown.empty() ? info.folder : shown;
        info.pseudo = id.pseudo;
        s_languages.push_back(std::move(info));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(s_languages.begin(), s_languages.end(),
              [](const LanguageInfo& a, const LanguageInfo& b) { return a.folder < b.folder; });
}

const LanguageInfo* findPack(const std::string& folder) {
    for (const LanguageInfo& l : s_languages) {
        if (l.folder == folder) return &l;
    }
    return nullptr;
}

// Windows' display language as a locale name ("sv-SE"), "" when unknown.
std::string systemLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) {
        return std::string();
    }
    std::string out;
    for (const wchar_t* c = name; *c; ++c) out += static_cast<char>(*c < 128 ? *c : '?');
    return out;
}

void loadActive() {
    if (!s_listed) listPacks();
    s_loaded = true;
    std::string folder = s_folder;
    if (folder == kSystemLanguage) {
        const std::string locale = systemLocale();
        folder = matchSystemLanguage(locale, s_languages);
        DEBUG_INFO_F("I18n: system language '%s' -> %s", locale.c_str(),
                     folder.empty() ? "English" : folder.c_str());
    }
    const LanguageInfo* info = folder.empty() ? nullptr : findPack(folder);
    if (!info) {
        if (!folder.empty()) {
            DEBUG_WARN_F("I18n: language '%s' is not installed - showing English", folder.c_str());
        }
        s_activeDisplayName = "English";
        s_activeFolder.clear();
        activate(nullptr);
    } else {
        auto cat = std::make_unique<Catalog>(info->pseudo);
        cat->setLimits(s_limits);
        try {
            loadChain(info->folder, *cat, 0);
        } catch (const std::exception& e) {
            DEBUG_WARN_F("I18n: language '%s' failed to load (%s) - showing English",
                         info->folder.c_str(), e.what());
            s_activeDisplayName = "English";
            s_activeFolder.clear();
            activate(nullptr);
            HudManager::getInstance().markAllHudsDirty();
            return;
        }
        s_activeDisplayName = info->displayName;
        s_activeFolder = info->folder;
        activate(cat.get());
        s_catalogs.push_back(std::move(cat));
    }
    HudManager::getInstance().markAllHudsDirty();
}

}  // namespace

const std::vector<LanguageInfo>& languages() {
    if (!s_listed) listPacks();
    return s_languages;
}

void reload() {
    listPacks();
    loadActive();
}

void setLanguage(const std::string& folder) {
    // Unchanged and already loaded: nothing to do (a settings load re-applies the
    // same value on every RELOAD_CONFIG; reload() is what re-reads the files).
    if (folder == s_folder && s_loaded) return;
    s_folder = folder;
    loadActive();
}

void ensureLoaded() {
    if (!s_loaded) loadActive();
}

const std::string& languageFolder() {
    return s_folder;
}

const std::string& activeFolder() {
    return s_activeFolder;
}

const char* languageDisplayName() {
    return s_activeDisplayName.c_str();
}

}  // namespace I18n
