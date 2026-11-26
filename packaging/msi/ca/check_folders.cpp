// packaging/msi/ca/check_folders.cpp - the MSI's custom actions: the Games
// page's folder check, and ReadEarlierOptOut at the end of this file.
//
// NSIS Setup warned "This does not appear to be the <game> plugins folder.
// Continue anyway?" when the game's exe was not next to the picked plugins
// folder. Windows Installer cannot test for a file at a folder picked in the
// wizard (AppSearch runs once, before the pages), so this does it: called from
// the Games page's Next (ui.wxs), it looks for each selected game's exe in the
// parent of the folder it is about to install to. For each game where it is
// missing, FOLDERWARNTEXT gets the statement half of NSIS's own sentence for
// that game (the localized FOLDERWARN_TMPL, "This does not appear to be the
// [GAMELABEL] plugins folder. Continue anyway?", up to its last ". ") and the
// folder below it; the question half follows ONCE after the list, since the
// page has one answer. gen_wxl.py fails the build for a language whose
// sentence has no ". " to split at. FOLDERWARN=1 sends the Games page to the
// warning page.
//
// It only reads properties and asks the file system whether one file exists.
// It writes nothing, starts nothing, touches no network. Any failure leaves
// FOLDERWARN unset: a check that cannot run must never block an install.
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <stdlib.h>
#include <wchar.h>

namespace {

struct Game {
    const wchar_t* key;   // property prefix, as in game.wxi
    const wchar_t* name;  // shown in the warning, as in ui.wxs
    const wchar_t* exe;   // as game.wxi's Exe
};
constexpr Game kGames[] = {
    { L"MXB", L"MX Bikes",        L"mxbikes.exe" },
    { L"GPB", L"GP Bikes",        L"gpbikes.exe" },
    { L"KRP", L"Kart Racing Pro", L"kart.exe" },
};

// Reads a property into buf; empty on any failure or overflow.
void getProp(MSIHANDLE h, const wchar_t* name, wchar_t* buf, DWORD cap) {
    DWORD len = cap;
    if (MsiGetPropertyW(h, name, buf, &len) != ERROR_SUCCESS) buf[0] = L'\0';
}

void append(wchar_t* dst, size_t cap, const wchar_t* src) {
    size_t n = wcslen(dst);
    while (*src && n + 1 < cap) dst[n++] = *src++;
    dst[n] = L'\0';
}

// True when <parent of the plugins folder>\exe exists. dir is G_DIR, the
// folder the Games page settled on ("...\plugins\", trailing slash optional).
bool exeBesidePlugins(const wchar_t* dir, const wchar_t* exe) {
    wchar_t path[MAX_PATH * 2] = {};
    append(path, _countof(path), dir);
    size_t n = wcslen(path);
    while (n > 0 && path[n - 1] == L'\\') path[--n] = L'\0';
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    append(path, _countof(path), exe);
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Appends tmpl[0, end) to dst with every [GAMELABEL] replaced by name.
void appendFilled(wchar_t* dst, size_t cap, const wchar_t* tmpl, const wchar_t* end, const wchar_t* name) {
    static const wchar_t kTag[] = L"[GAMELABEL]";
    constexpr size_t kTagLen = _countof(kTag) - 1;
    while (tmpl < end) {
        if (static_cast<size_t>(end - tmpl) >= kTagLen && wcsncmp(tmpl, kTag, kTagLen) == 0) {
            append(dst, cap, name);
            tmpl += kTagLen;
        } else {
            const wchar_t one[2] = { *tmpl++, L'\0' };
            append(dst, cap, one);
        }
    }
}

}  // namespace

extern "C" UINT __stdcall CheckGameFolders(MSIHANDLE h) {
    wchar_t text[2048] = {};
    wchar_t tmpl[512] = {};
    wchar_t installed[16] = {};
    getProp(h, L"FOLDERWARN_TMPL", tmpl, _countof(tmpl));
    // Statement [tmpl, cut] (through the full stop), question after it.
    const wchar_t* question = L"";
    const wchar_t* cut = tmpl + wcslen(tmpl);
    for (const wchar_t* p = tmpl; *p; ++p) {
        if (p[0] == L'.' && p[1] == L' ') { cut = p + 1; question = p + 2; }
    }
    getProp(h, L"Installed", installed, _countof(installed));
    for (const Game& g : kGames) {
        wchar_t prop[32] = {};
        wchar_t value[MAX_PATH * 2] = {};
        append(prop, _countof(prop), g.key);
        append(prop, _countof(prop), L"_SEL");
        getProp(h, prop, value, _countof(value));
        if (wcscmp(value, L"1") != 0) continue;
        // A game already installed (Change mode) keeps its folder: not asked.
        prop[0] = L'\0';
        append(prop, _countof(prop), g.key);
        append(prop, _countof(prop), L"_MSI");
        getProp(h, prop, value, _countof(value));
        if (installed[0] && value[0]) continue;
        prop[0] = L'\0';
        append(prop, _countof(prop), g.key);
        append(prop, _countof(prop), L"_DIR");
        getProp(h, prop, value, _countof(value));
        if (!value[0] || exeBesidePlugins(value, g.exe)) continue;
        if (text[0]) append(text, _countof(text), L"\r\n\r\n");
        appendFilled(text, _countof(text), tmpl, cut, g.name);
        append(text, _countof(text), L"\r\n");
        append(text, _countof(text), value);
    }
    if (text[0] && question[0]) {
        append(text, _countof(text), L"\r\n\r\n");
        append(text, _countof(text), question);
    }
    MsiSetPropertyW(h, L"FOLDERWARNTEXT", text);
    MsiSetPropertyW(h, L"FOLDERWARN", text[0] ? L"1" : L"");
    return ERROR_SUCCESS;
}

// An upgrade skips the Privacy page, so an analytics opt-out an earlier Setup
// left in a game's install_prefs.ini (core/install_prefs.h) must survive this
// install rewriting the file, as the .exe Setup keeps it. For each game with an
// earlier install (as G_PREV finds its plugins folder) whose file says
// analyticsOptOut=1, sets G_KEEPOPTOUT=1 and G_STAMP to that line's OLD stamp:
// one the plugin has honoured stays ignored, one it has not yet seen (game
// never launched) still applies. Sequenced only when the player did not answer
// the Privacy page and did not pick Fresh (mxbmrp3.wxs). Reads one file per
// game; writes nothing but properties; any failure carries nothing.
extern "C" UINT __stdcall ReadEarlierOptOut(MSIHANDLE h) {
    for (const Game& g : kGames) {
        // G_PREV's own sources, in its order (game.wxi): G_PREV itself is set
        // in the UI sequence only, while AppSearch runs in this one too.
        static const wchar_t* const kSources[] = { L"_MSI", L"_NSISLM", L"_NSISCU" };
        wchar_t prop[32] = {};
        wchar_t path[MAX_PATH * 2] = {};
        for (const wchar_t* src : kSources) {
            prop[0] = L'\0';
            append(prop, _countof(prop), g.key);
            append(prop, _countof(prop), src);
            getProp(h, prop, path, _countof(path));
            if (path[0]) break;
        }
        if (!path[0]) continue;
        const size_t n = wcslen(path);
        if (path[n - 1] != L'\\') append(path, _countof(path), L"\\");
        append(path, _countof(path), L"mxbmrp3_data\\install_prefs.ini");
        wchar_t optOut[8] = {};
        GetPrivateProfileStringW(L"Install", L"analyticsOptOut", L"", optOut, _countof(optOut), path);
        if (wcscmp(optOut, L"1") != 0) continue;
        prop[0] = L'\0';
        append(prop, _countof(prop), g.key);
        append(prop, _countof(prop), L"_KEEPOPTOUT");
        MsiSetPropertyW(h, prop, L"1");
        wchar_t stamp[64] = {};
        GetPrivateProfileStringW(L"Install", L"stamp", L"", stamp, _countof(stamp), path);
        if (!stamp[0]) continue;  // unstamped: this install's stamp stands
        prop[0] = L'\0';
        append(prop, _countof(prop), g.key);
        append(prop, _countof(prop), L"_STAMP");
        MsiSetPropertyW(h, prop, stamp);
    }
    return ERROR_SUCCESS;
}
