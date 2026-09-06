// ============================================================================
// core/exploration_stats.cpp
// See the header. The startup scans (user packs, custom.css, crash dumps) are
// one directory listing each, once per process; nothing here runs per frame
// but tick(), which is one compare per signal it carries.
// ============================================================================
#include "exploration_stats.h"
#include "achievement_manager.h"
#include "asset_manager.h"
#include "color_config.h"
#include "font_config.h"
#include "hotkey_manager.h"
#include "hud_manager.h"
#include "plugin_constants.h"
#include "settings_manager.h"
#include "ui_config.h"
#include "companion_window.h"
#include "../hud/achievement_widget.h"
#include "../diagnostics/logger.h"
#include "../hud/gl_confirm_hud.h"
#include "../hud/version_widget.h"
#include "../hud/benchmark_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/settings_hud.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

using Exploration::Signal;

namespace {

constexpr int NIGHT_OWL_FROM_HOUR = 2;
constexpr int NIGHT_OWL_TO_HOUR = 5;
constexpr int PHOTO_FINISH_MS = 100;
constexpr int METRONOME_SPREAD_MS = 100;

// Subfolders under <savePath>\mxbmrp3\<subdir>\: each is one user pack.
int countSubfolders(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            std::strcmp(fd.cFileName, ".") != 0 && std::strcmp(fd.cFileName, "..") != 0) {
            ++n;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

int countFiles(const std::string& dir, const char* pattern) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

// The YYYYMMDD after a YYYYMMDD, through days-since-epoch (Howard Hinnant's
// civil algorithms), so a run of days survives a month or year boundary.
long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int dayAfter(int yyyymmdd) {
    const long long z = daysFromCivil(yyyymmdd / 10000, (yyyymmdd / 100) % 100, yyyymmdd % 100) + 1 + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int y = static_cast<int>(yoe + era * 400 + (m <= 2));
    return y * 10000 + m * 100 + d;
}

// A custom.css counts as styling once it has anything that is not a comment
// or whitespace: the shipped file is comments only, so a user who kept it
// untouched is not a stylist.
bool cssHasRule(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    bool inComment = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (inComment) {
            if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') { inComment = false; ++i; }
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') { inComment = true; ++i; continue; }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(text[i]))) return true;
    }
    return false;
}

#ifdef MXBMRP3_TEST_BUILD
// A fixed noon in June unless a test says otherwise (see the header).
ExplorationStats::LocalTime s_localTimeOverride = { 2026, 6, 15, 12 };
#endif

}  // namespace

ExplorationStats::ExplorationStats() = default;

// ---- primitives ---------------------------------------------------------------

bool ExplorationStats::mark(Signal s) {
    double& v = m_values[static_cast<int>(s)];
    if (v >= 1.0) return false;
    v = 1.0;
    return true;
}

void ExplorationStats::add(Signal s, double d) {
    m_values[static_cast<int>(s)] += d;
}

bool ExplorationStats::raise(Signal s, double v) {
    double& cur = m_values[static_cast<int>(s)];
    if (v <= cur) return false;
    cur = v;
    return true;
}

void ExplorationStats::changed() {
    m_dirty = true;
    AchievementManager::getInstance().onStatsChanged();
}

void ExplorationStats::restoreValue(int index, double v) {
    if (index <= 0 || index >= Exploration::SIGNAL_COUNT) return;
    m_values[index] = v;
}

const std::set<std::string>& ExplorationStats::names(int which) const {
    return which == 0 ? m_tabs : which == 1 ? m_versions : which == 2 ? m_huds : m_servers;
}

void ExplorationStats::restoreName(int which, const std::string& name) {
    (which == 0 ? m_tabs : which == 1 ? m_versions : which == 2 ? m_huds : m_servers).insert(name);
}

void ExplorationStats::restoreScalars(const std::string& firstRunDate, int lastDay, int crashDumpsSeen, int dayStreak) {
    m_firstRunDate = firstRunDate;
    m_lastDay = lastDay;
    m_crashDumpsSeen = crashDumpsSeen;
    m_dayStreak = dayStreak;
}

void ExplorationStats::clear() {
    for (double& v : m_values) v = 0.0;
    m_tabs.clear();
    m_versions.clear();
    m_huds.clear();
    m_servers.clear();
    m_firstRunDate.clear();
    m_lastDay = 0;
    m_dayStreak = 0;
    m_crashDumpsSeen = -1;
    m_dirty = false;
}

// ---- the clock ------------------------------------------------------------------

ExplorationStats::LocalTime ExplorationStats::localNow() {
#ifdef MXBMRP3_TEST_BUILD
    return s_localTimeOverride;
#else
    std::time_t now = std::time(nullptr);
    std::tm t{};
    // localtime_s writes t (output param); cppcheck can't model that.
    // cppcheck-suppress uninitvar
    localtime_s(&t, &now);
    return { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour };
#endif
}

#ifdef MXBMRP3_TEST_BUILD
void ExplorationStats::setLocalTimeOverride(int year, int month, int day, int hour) {
    s_localTimeOverride = { year, month, day, hour };
}
#endif

// ---- fingerprints --------------------------------------------------------------

uint64_t ExplorationStats::fnv1a(const std::string& text) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;   // 0 means "never written"
}

std::string ExplorationStats::fingerprintTrailer(const std::string& fileText) {
    char trailer[64];
    snprintf(trailer, sizeof(trailer), "[Fingerprint]\nsettings=%016llx\n",
             static_cast<unsigned long long>(fnv1a(fileText)));
    return trailer;
}

uint64_t ExplorationStats::hashFileAboveTrailer(const std::string& path) {
    std::ifstream raw(path, std::ios::binary);
    if (!raw.is_open()) return 0;
    std::stringstream ss;
    ss << raw.rdbuf();
    std::string text = ss.str();
    const size_t cut = text.rfind("[Fingerprint]");
    if (cut != std::string::npos) text.resize(cut);
    return fnv1a(text);
}

void ExplorationStats::noteStatsLoadedHash(uint64_t loadedHash, uint64_t expected) {
    if (expected == 0 || loadedHash == expected) return;
    if (mark(Signal::StatsEdited)) changed();
}

// ---- feeds ---------------------------------------------------------------------

void ExplorationStats::restoreAtLeast(Signal s, double v) {
    if (raise(s, v)) m_dirty = true;
}

bool ExplorationStats::checkClock(bool riding) {
    bool moved = false;
    const LocalTime now = localNow();
    // Regular: a new local day, at a startup or partway through a long night.
    const int today = now.year * 10000 + now.month * 100 + now.day;
    if (today != m_lastDay) {
        // On a Roll: the day after the last counted one extends the run; any
        // other new day starts one. The row keeps the best run ever.
        m_dayStreak = (m_lastDay != 0 && today == dayAfter(m_lastDay)) ? m_dayStreak + 1 : 1;
        m_lastDay = today;
        add(Signal::DaysUsed, 1.0);
        raise(Signal::DayStreak, m_dayStreak);
        moved = true;
    }
    if (!riding) return moved;
    if (now.hour >= NIGHT_OWL_FROM_HOUR && now.hour < NIGHT_OWL_TO_HOUR) moved |= mark(Signal::NightOwl);
    if (m_firstRunDate.size() == 10) {
        const int firstYear = std::atoi(m_firstRunDate.c_str());
        char monthDay[8];
        snprintf(monthDay, sizeof(monthDay), "%02d-%02d", now.month, now.day);
        if (now.year > firstYear && m_firstRunDate.compare(5, 5, monthDay) == 0) moved |= mark(Signal::Anniversary);
    }
    return moved;
}

bool ExplorationStats::scanUserFiles() {
    if (m_savePath.empty()) return false;
    bool moved = false;
    // User packs: one listing per pack type.
    std::string user = m_savePath;
    if (user.back() != '\\' && user.back() != '/') user += '\\';
    user += "mxbmrp3";
    int packs = 0;
    for (const AssetManager::PackType& t : AssetManager::PACK_TYPES) {
        const int n = countSubfolders(user + "\\" + t.subdir);
        packs += n;
        if (n <= 0) continue;
        if (std::strcmp(t.subdir, AssetManager::GAMEPADS_SUBDIR) == 0)  moved |= mark(Signal::CustomGamepad);
        if (std::strcmp(t.subdir, AssetManager::SPOTTERS_SUBDIR) == 0)  moved |= mark(Signal::CustomSpotter);
        if (std::strcmp(t.subdir, AssetManager::PITBOARDS_SUBDIR) == 0 ||
            std::strcmp(t.subdir, AssetManager::GAUGES_SUBDIR) == 0)    moved |= mark(Signal::CustomBoard);
        if (std::strcmp(t.subdir, AssetManager::THEMES_SUBDIR) == 0)    moved |= mark(Signal::CustomTheme);
    }
    moved |= raise(Signal::CustomPacks, packs);
    // The stylesheet the overlay serves is the one in the plugin's own web
    // folder (CWD-relative, as HttpServer reads it): a custom.css written there,
    // next to the bundled sample as the docs describe, counts the same as one in
    // Documents, which the asset sync copies into that folder at startup.
    {
        const std::string userCss = user + "\\web\\custom.css";
        const std::string servedCss = std::string(AssetManager::DISCOVERY_DIR) + "\\web\\custom.css";
        const bool userRule = cssHasRule(userCss);
        const bool servedRule = userRule || cssHasRule(servedCss);
        DEBUG_INFO_F("[Exploration] Stylist: %s (%s), %s (%s)",
                     userCss.c_str(), userRule ? "styled" : "no rule or absent",
                     servedCss.c_str(), servedRule ? "styled" : "no rule or absent");
        if (servedRule) moved |= mark(Signal::Stylist);
    }
    return moved;
}

void ExplorationStats::onStartup(const std::string& savePath, const char* version) {
    bool moved = false;
    m_savePath = savePath;
    const LocalTime now = localNow();
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", now.year, now.month, now.day);
    if (m_firstRunDate.empty()) { m_firstRunDate = date; m_dirty = true; }
    moved |= checkClock(/*riding=*/false);

    // Version Hopper: x.y.z, never the build number.
    {
        std::string v = version ? version : "";
        size_t dots = 0, cut = std::string::npos;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '.' && ++dots == 3) { cut = i; break; }
        }
        if (cut != std::string::npos) v.resize(cut);
        if (!v.empty() && m_versions.insert(v).second) {
            moved |= raise(Signal::VersionsRun, static_cast<double>(m_versions.size()));
            m_dirty = true;
        }
    }

    moved |= scanUserFiles();
    const std::string user = savePath + "\\mxbmrp3";

    // Phoenix: a dump the crash handler wrote since the last startup. The first
    // count ever (a file from before this block) is the baseline, silently: an
    // old dump is not a comeback.
    {
        const int dumps = countFiles(user + "\\crashes", "*.dmp");
        if (m_crashDumpsSeen >= 0 && dumps > m_crashDumpsSeen) moved |= mark(Signal::Phoenix);
        if (dumps != m_crashDumpsSeen) { m_crashDumpsSeen = dumps; m_dirty = true; }
    }

    // The build itself. A test build is not a home build.
#if !defined(MXBMRP3_APTABASE_KEY) && !defined(MXBMRP3_TEST_BUILD)
    moved |= mark(Signal::Homebrew);
#endif
    if (std::strcmp(PluginConstants::PLUGIN_NAME, "mxbmrp3") != 0) moved |= mark(Signal::SignedCopy);

    // What the settings file, loaded before this, already decided.
    const SettingsManager& settings = SettingsManager::getInstance();
    if (settings.isDeveloperMode()) moved |= mark(Signal::Developer);
    if (!AchievementManager::getInstance().isToastsEnabled()) moved |= mark(Signal::Ungrateful);
    moved |= checkSettingsEdited();
    if (SettingsManager::getInstance().consumeUpdateInstalled()) {
        add(Signal::UpdatesInstalled, 1.0);
        moved = true;
    }
    // The companion window the display target opened during that load: its
    // own feed fired before this block existed and was wiped with it.
    if (CompanionWindow::getInstance().isEnabled()) {
        add(Signal::CompanionOpens, 1.0);
        moved = true;
    }

    if (moved) changed();
}

bool ExplorationStats::checkSettingsEdited() {
    const SettingsManager& settings = SettingsManager::getInstance();
    if (settings.expectedFileHash() == 0 || settings.loadedFileHash() == settings.expectedFileHash()) return false;
    return mark(Signal::SettingsEdited);
}

void ExplorationStats::onSettingsReloaded() {
    bool moved = checkSettingsEdited();
    moved |= scanUserFiles();   // a pack dropped in, a stylesheet written, since the start
    // What the reloaded file decided, the same reads as the startup's: a
    // developer mode switched on by hand lands at the reload, not the restart.
    if (SettingsManager::getInstance().isDeveloperMode()) moved |= mark(Signal::Developer);
    if (!AchievementManager::getInstance().isToastsEnabled()) moved |= mark(Signal::Ungrateful);
    if (moved) changed();
}

void ExplorationStats::onSessionStart() {
    m_firstLapPosition = 0;
    m_ledEveryLap = true;
    m_lapCount = 0;
    m_sessionLapTimes.clear();
    m_crashFreeMs = 0.0;
    if (checkClock(/*riding=*/true)) changed();
}

void ExplorationStats::onRunStart() {
    if (checkClock(/*riding=*/true)) changed();
}

// What a save says about the setup, for the Plugin-page achievements: which
// HUDs have been on (Tyre Kicker), how customised the look is (Interior
// Decorator), how many hotkeys are bound (Keymaster), whether an experimental
// setting is on (Test Pilot: the two that ship off). At every settings edit
// and at a save, never per frame. The tried set only grows, and the percent counts only HUDs this
// build has, so a renamed or game-gated HUD neither inflates nor strands it.
void ExplorationStats::observeSettings(const HudManager& hudManager) {
    int hudsTotal = 0, hudsTried = 0;
    bool setGrew = false;
    int customisations = UiConfig::getInstance().getThemeName().empty() ? 0 : 1;
    for (const auto& hud : hudManager.getHuds()) {
        if (!hud) continue;
        const BaseHud* h = hud.get();
        // Not user-facing switches: the menu, its button, the pointer, the
        // developer's benchmark, the achievement toast itself -- and the two
        // that show themselves: the Direct GL confirmation (armed by that
        // setting's prompt) and the version widget (an update notice, the
        // donation nudge). Counted, 100% would need both events, not every HUD.
        const bool chrome = h == &hudManager.getSettingsHud() || h == &hudManager.getSettingsButtonWidget() ||
                            h == &hudManager.getPointerWidget() || h == hudManager.getBenchmarkWidget() ||
                            h == hudManager.getAchievementWidget() || h == &hudManager.getGlConfirmHud() ||
                            h == &hudManager.getVersionWidget();
        if (!chrome) {
            ++hudsTotal;
            if (h->isVisibleAnySurface()) setGrew |= m_huds.insert(h->getHarnessId()).second;
            if (m_huds.count(h->getHarnessId())) ++hudsTried;
        }
        if (!h->getThemeOverride().empty()) ++customisations;
        bool colour = false, font = false;
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) colour |= h->hasColorOverride(static_cast<ColorSlot>(i));
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) font |= h->hasFontOverride(static_cast<FontCategory>(i));
        customisations += (colour ? 1 : 0) + (font ? 1 : 0);
    }
    for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
        if (ColorConfig::getInstance().isOverridden(static_cast<ColorSlot>(i))) ++customisations;
    }
    for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
        if (FontConfig::getInstance().isOverridden(static_cast<FontCategory>(i))) ++customisations;
    }
    int hotkeysBound = 0;
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        const HotkeyBinding& b = HotkeyManager::getInstance().getBinding(static_cast<HotkeyAction>(i));
        if (b.keyboard.keyCode != 0 || b.controller != ControllerButton::NONE) ++hotkeysBound;
    }
    if (setGrew) m_dirty = true;
    const bool experimental = UiConfig::getInstance().getPluginThread() || UiConfig::getInstance().getGlInGame();
    onSettingsSaved(hudsTotal > 0 ? hudsTried * 100 / hudsTotal : 0, customisations, hotkeysBound, experimental);
}

void ExplorationStats::onSettingsSaved(int hudsTriedPercent, int customisations, int hotkeysBound, bool experimental) {
    bool moved = false;
    if (experimental) moved |= mark(Signal::TestPilot);
    moved |= raise(Signal::HudsTried, hudsTriedPercent);
    moved |= raise(Signal::Customisations, customisations);
    moved |= raise(Signal::HotkeysBound, hotkeysBound);
    if (moved) changed();
}

void ExplorationStats::onTabOpened(const char* tabName) {
    if (!tabName || !tabName[0]) return;
    if (!m_tabs.insert(tabName).second) return;
    raise(Signal::TabsVisited, static_cast<double>(m_tabs.size()));
    changed();
}

void ExplorationStats::onCompanionOpened() { add(Signal::CompanionOpens, 1.0);   changed(); }
void ExplorationStats::onDirectorCut()     { add(Signal::DirectorCuts, 1.0);     changed(); }
void ExplorationStats::onSpotterCallout()  { add(Signal::SpotterCallouts, 1.0);  changed(); }
void ExplorationStats::onProfileSwitched() { add(Signal::ProfileSwitches, 1.0);  changed(); }
void ExplorationStats::onSegmentCompleted(){ add(Signal::Segments, 1.0);         changed(); }
void ExplorationStats::onFactoryReset()    { if (mark(Signal::FactoryFresh)) changed(); }
void ExplorationStats::onToastsDisabled()  { if (mark(Signal::Ungrateful)) changed(); }
void ExplorationStats::onSandbag()         { if (mark(Signal::Sandbagger)) changed(); }
void ExplorationStats::onChainCrash()      { if (mark(Signal::ChainCrash)) changed(); }
void ExplorationStats::onChainLost(int points) { if (points > 0 && raise(Signal::ChainLost, points)) changed(); }
void ExplorationStats::onRaceLeft()        { add(Signal::RageQuits, 1.0);        changed(); }
void ExplorationStats::onRiderTracked()    { add(Signal::RidersTracked, 1.0);    changed(); }

void ExplorationStats::onServerJoined(const char* serverName) {
    if (!serverName || !serverName[0]) return;              // offline, or a game without one
    if (!m_servers.insert(serverName).second) return;
    raise(Signal::Servers, static_cast<double>(m_servers.size()));
    changed();
}

void ExplorationStats::onRaceLapPosition(int lapNum, int position) {
    if (position <= 0) return;
    if (lapNum == 1 || m_firstLapPosition == 0) m_firstLapPosition = position;
    if (position != 1) m_ledEveryLap = false;
}

void ExplorationStats::onRaceFinished(int position, int gapToSecondMs, bool everyOtherRiderLapped, int ownGapLaps) {
    bool moved = false;
    if (m_firstLapPosition > 0 && position > 0) {
        moved |= raise(Signal::PositionsGained, m_firstLapPosition - position);
    }
    if (position == 1) {
        if (m_ledEveryLap && m_firstLapPosition == 1) { add(Signal::WireToWire, 1.0); moved = true; }
        if (everyOtherRiderLapped) { add(Signal::LappedField, 1.0); moved = true; }
        if (gapToSecondMs > 0 && gapToSecondMs < PHOTO_FINISH_MS) moved |= mark(Signal::PhotoFinish);
    }
    if (ownGapLaps > 0) moved |= raise(Signal::BackMarker, ownGapLaps);   // the most laps down, on the way to three
    if (moved) changed();
}

void ExplorationStats::onLapTime(int lapTimeMs) {
    if (lapTimeMs <= 0) return;
    bool moved = false;

    // Palindrome: the digits of m:ss.mmm, punctuation dropped.
    {
        char digits[24];
        snprintf(digits, sizeof(digits), "%d%02d%03d",
                 lapTimeMs / 60000, (lapTimeMs / 1000) % 60, lapTimeMs % 1000);
        const size_t n = std::strlen(digits);
        bool pal = n >= 6;
        for (size_t i = 0; pal && i < n / 2; ++i) pal = digits[i] == digits[n - 1 - i];
        if (pal) moved |= mark(Signal::Palindrome);
    }

    // Deja vu: the same time twice in one session.
    if (!m_sessionLapTimes.insert(lapTimeMs).second) moved |= mark(Signal::DejaVu);

    // Metronome: five in a row within a tenth. A hit closes its window, so
    // ten such laps are two runs, not six overlapping ones.
    m_lastLaps[m_lapCount % 5] = lapTimeMs;
    ++m_lapCount;
    if (m_lapCount >= 5) {
        int lo = m_lastLaps[0], hi = m_lastLaps[0];
        for (int t : m_lastLaps) { lo = std::min(lo, t); hi = std::max(hi, t); }
        if (hi - lo <= METRONOME_SPREAD_MS) { add(Signal::Metronome, 1.0); moved = true; m_lapCount = 0; }
    }
    if (moved) changed();
}

void ExplorationStats::onCrash(int sessionCrashes, int crashTally) {
    m_crashFreeMs = 0.0;
    bool moved = false;
    // The numbers themselves, as running maxima: the rows show "2 / 13" and
    // "47 / 99" on the way, not a flag that flips at the end.
    moved |= raise(Signal::BakersDozen, sessionCrashes);
    moved |= raise(Signal::CrashTally99, crashTally);
    if (moved) changed();
}

void ExplorationStats::onTrickTime(bool airborne, bool shred, float seconds) {
    if (!(seconds > 0.0f) || !(seconds < 3600.0f)) return;   // finite, sane
    if (!airborne && !shred) return;
    if (airborne) add(Signal::AirtimeSec, seconds);
    if (shred) add(Signal::ShredSec, seconds);
    changed();
}

void ExplorationStats::tick(bool spectating, bool rumbleLive, bool onTrack, int framesThisSecond,
                            uint32_t overlayConnectionsTotal) {
    bool moved = false;
    if (spectating) { add(Signal::SpectateHours, 1.0 / 3600.0); moved = true; }
    if (rumbleLive && onTrack) { add(Signal::RumbleHours, 1.0 / 3600.0); moved = true; }   // riding, with it on
    moved |= raise(Signal::FramePerfect, framesThisSecond);   // the best second, on the way to 480
    // The clock, once a minute: a ride that runs into the small hours, a
    // session across midnight, the anniversary arriving mid-session.
    if (++m_clockTicks >= 60) {
        m_clockTicks = 0;
        moved |= checkClock(onTrack);
    }
    // Riding time since the last crash (or the session's start): a trip to the
    // pits pauses the clock rather than restarting it, so the row reads as it
    // says -- an hour of riding without a crash, not an hour of it in one go.
    if (onTrack) {
        m_crashFreeMs += 1000.0;
        moved |= raise(Signal::SteadyHands, m_crashFreeMs / 1000.0);   // the longest run, in seconds
    }
    // The overlay's total restarts with the process: count what it grew by.
    if (overlayConnectionsTotal > m_overlaySeen) {
        add(Signal::OverlayConnections, static_cast<double>(overlayConnectionsTotal - m_overlaySeen));
        m_overlaySeen = overlayConnectionsTotal;
        moved = true;
    }
    if (moved) changed();
}
