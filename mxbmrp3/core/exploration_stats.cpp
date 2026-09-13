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
#include "xinput_reader.h"
#include "update_checker.h"
#include "../hud/achievement_widget.h"

#include "../diagnostics/logger.h"
#include "../hud/gl_confirm_hud.h"
#include "../hud/version_widget.h"
#include "../hud/benchmark_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/prestige_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/settings_hud.h"

#include <cmath>
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
// Survivor's floor: below this a single retirement is already a quarter of the
// field, which is a two-rider lobby losing one, not a race of attrition.
constexpr int SURVIVOR_MIN_STARTERS = 6;
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
    return which == 0 ? m_tabs : which == 1 ? m_huds : m_servers;
}

void ExplorationStats::restoreName(int which, const std::string& name) {
    (which == 0 ? m_tabs : which == 1 ? m_huds : m_servers).insert(name);
}

void ExplorationStats::restoreScalars(const std::string& firstRunDate, int lastDay, int crashDumpsSeen, int dayStreak,
                                      int rideDay, double todayRideSec) {
    m_firstRunDate = firstRunDate;
    m_lastDay = lastDay;
    m_crashDumpsSeen = crashDumpsSeen;
    m_dayStreak = dayStreak;
    m_rideDay = rideDay;
    m_todayRideSec = (std::isfinite(todayRideSec) && todayRideSec > 0.0) ? todayRideSec : 0.0;
}

void ExplorationStats::clear() {
    for (double& v : m_values) v = 0.0;
    m_tabs.clear();
    m_huds.clear();
    m_servers.clear();
    m_firstRunDate.clear();
    m_lastDay = 0;
    m_dayStreak = 0;
    m_crashDumpsSeen = -1;
    m_rideDay = 0;
    m_todayRideSec = 0.0;
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
        if (now.year > firstYear && m_firstRunDate.compare(5, 5, monthDay) == 0) {
            moved |= mark(Signal::Anniversary);
        }
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
        packs += countSubfolders(user + "\\" + t.subdir);
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

void ExplorationStats::onStartup(const std::string& savePath) {
    bool moved = false;
    m_savePath = savePath;
    const LocalTime now = localNow();
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", now.year, now.month, now.day);
    if (m_firstRunDate.empty()) { m_firstRunDate = date; m_dirty = true; }
    moved |= checkClock(/*riding=*/false);

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

    // What the settings file, loaded before this, already decided.
    const SettingsManager& settings = SettingsManager::getInstance();
    if (settings.isDeveloperMode()) moved |= mark(Signal::Developer);
    if (!AchievementManager::getInstance().isToastsEnabled()) moved |= mark(Signal::Ungrateful);
    moved |= checkSwitches();
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

bool ExplorationStats::checkSwitches() {
    bool moved = false;
    // Rumble: the SETTING, deliberately - not a live controller. The hours
    // version of this row could not be earned at all by a player whose pad the
    // plugin does not see, which is exactly how it reached a stream and stayed
    // locked all night with rumble switched on the whole time.
    if (XInputReader::getInstance().getGlobalRumbleConfig().enabled) {
        moved |= mark(Signal::RumbleOn);
    }
    if (UpdateChecker::getInstance().isPrereleaseChannel()) {
        moved |= mark(Signal::Prerelease);
    }
    return moved;
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
    moved |= checkSwitches();
    if (moved) changed();
}

void ExplorationStats::onSessionStart() {
    m_firstLapPosition = 0;
    m_ledEveryLap = true;
    m_gotHoleshot = false;
    m_prevLapPosition = 0;
    m_lastLapPosition = 0;
    m_lastPositionLap = 0;
    m_pendingLastGaspPosition = 0;
    m_pendingLastGaspLaps = 0;
    m_holeshotArmed = false;
    m_awaitingOpeningSplit = false;
    m_cleanLapRun = 0;
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
        //
        // The PRESTIGE BADGE is here for a different reason: it is EARNED, not
        // switched on, so it is a row nobody has until they trade for it. It
        // also defaults to visible (a locked one simply draws nothing), so
        // counting it put it in the tried set on every fresh install and handed
        // everyone a free row toward Tyre Kicker. Excluded outright rather than
        // while locked, so the denominator does not change under a player the
        // day they prestige.
        const bool chrome = h == &hudManager.getSettingsHud() || h == &hudManager.getSettingsButtonWidget() ||
                            h == &hudManager.getPointerWidget() || h == hudManager.getBenchmarkWidget() ||
                            h == hudManager.getAchievementWidget() || h == &hudManager.getGlConfirmHud() ||
                            h == hudManager.getPrestigeWidget() ||
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

// A PERCENT of the tabs this build has, like HudsTried, not a raw count: three
// tabs are game-gated (Records, Friends, FMX), so "open them all" as a fixed
// number would be unreachable on karts and short of everything on MX Bikes.
// The caller passes the count, since only SettingsHud knows which are gated.
//
// The set is what is persisted, so a stats file written when this was a raw
// count reads its old number as a percent. That always UNDER-reports (a count
// is never larger than its own percent of a smaller total) and the next tab
// opened recomputes it, so nothing is granted that was not earned.
void ExplorationStats::onTabOpened(const char* tabName, const char* const* listedTabs,
                                   int listedCount) {
    if (!tabName || !tabName[0]) return;
    if (!m_tabs.insert(tabName).second) return;
    // COUNTED OVER THE LIST, not off m_tabs.size(). The set is persisted and
    // keeps every name ever opened, including ones this build does not list -- a
    // game-gated tab from another game, or About, which was recorded until it
    // stopped being. Sized against the listed count those names read as progress
    // toward tabs the player cannot even see, and the row closed early.
    int seen = 0;
    for (int i = 0; i < listedCount; ++i) {
        if (listedTabs[i] && m_tabs.count(listedTabs[i])) ++seen;
    }
    const int pct = listedCount > 0 ? std::min(100, seen * 100 / listedCount) : 0;
    raise(Signal::TabsVisited, static_cast<double>(pct));
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

void ExplorationStats::onRaceLapPosition(int position, int lapNum) {
    if (position <= 0) return;
    // Only if nothing has set it: after a gate drop that is the opening split's
    // position, and overwriting it at lap one would discard the start.
    if (m_firstLapPosition == 0) m_firstLapPosition = position;
    if (position != 1) m_ledEveryLap = false;
    // The rolling pair Last Gasp reads: where the player sat after the lap
    // before the last one, against where they finished.
    m_prevLapPosition = m_lastLapPosition;
    m_lastLapPosition = position;
    // WHICH lap this pair describes, not how many have been seen: a count that
    // misses one sample lags for the rest of the race and takes Last Gasp down
    // with it silently, where the lap number only ever asks about the last one.
    m_lastPositionLap = lapNum;
}

void ExplorationStats::onGateDrop(int starters) {
    m_holeshotArmed = starters >= MIN_FIELD;
    m_awaitingOpeningSplit = true;
    // ...and the race scratch starts over, because A GATE DROP IS A RACE
    // BEGINNING. recordSessionStart() cannot tell a restart from a pit stop -
    // both re-enter the same session type - and it deliberately keeps this trio
    // across the second so a pit visit does not forget where lap one was. Kept
    // across the first, the abandoned race became the new one's history: its
    // start position, its lead, its last-lap places.
    m_firstLapPosition = 0;
    m_ledEveryLap = true;
    m_gotHoleshot = false;
    m_prevLapPosition = 0;
    m_lastLapPosition = 0;
    m_lastPositionLap = 0;
    m_pendingLastGaspPosition = 0;
    m_pendingLastGaspLaps = 0;
}

void ExplorationStats::onPlayerOpeningSplit(int position) {
    if (!m_awaitingOpeningSplit) return;
    // DISARMED WHETHER OR NOT THE POSITION READS, because the opening split has
    // happened either way. Left armed on an unreadable one - a player not yet in
    // the classification order - LAP TWO's split claimed it instead and
    // overwrote the lap-one fallback with a later, better place, which moves
    // Charger's start forward and loses it exactly the places gained in between.
    m_awaitingOpeningSplit = false;
    if (position <= 0) return;
    m_firstLapPosition = position;   // Charger counts from here, not from lap one
}

void ExplorationStats::onFirstSplit(bool isPlayer) {
    if (!m_holeshotArmed) return;
    m_holeshotArmed = false;   // whoever it was, the holeshot is settled
    if (!isPlayer) return;
    m_gotHoleshot = true;
    add(Signal::Holeshots, 1.0);
    changed();
}

void ExplorationStats::onLapCompleted(bool clean) {
    if (!clean) { m_cleanLapRun = 0; return; }
    ++m_cleanLapRun;
    if (raise(Signal::CleanLapStreak, m_cleanLapRun)) changed();
}

bool ExplorationStats::onFinishMargin(int position, int gapToSecondMs, int gapToWinnerMs) {
    bool moved = false;
    if (position == 1) {
        if (gapToSecondMs > 0 && gapToSecondMs < PHOTO_FINISH_MS) moved |= mark(Signal::PhotoFinish);
    } else if (position == 2) {
        // So Close: the same tenth, on the wrong side of it. Second only -- a
        // third place a tenth off the winner is a different race, and one a tenth
        // off SECOND is not what the row is about.
        if (gapToWinnerMs > 0 && gapToWinnerMs < PHOTO_FINISH_MS) moved |= mark(Signal::SoClose);
    }
    if (moved) changed();
    return moved;
}

// The comparison itself, so the live path and the retry cannot drift apart.
bool ExplorationStats::creditLastLap(int position) {
    if (m_prevLapPosition <= 0 || position <= 0) return false;
    bool moved = false;
    // Last Gasp: a place taken on the final lap.
    if (position < m_prevLapPosition) { add(Signal::LastLapPasses, 1.0); moved = true; }
    // Choke: the lead going into it, and not at the flag. The mirror image of
    // the line above and deliberately its neighbour - both ask the same pair
    // the same question, and split apart they would drift the moment one of
    // them learned something about the pair that the other did not.
    if (m_prevLapPosition == 1 && position > 1) moved |= mark(Signal::Choke);
    return moved;
}

bool ExplorationStats::retryLastGasp() {
    const int position = m_pendingLastGaspPosition;
    const int laps = m_pendingLastGaspLaps;
    m_pendingLastGaspPosition = 0;
    m_pendingLastGaspLaps = 0;
    // Only once the lap it was waiting for has actually arrived. One that never
    // does leaves the pair exactly as stale as it was at the flag, and a row
    // that quietly does not count beats one credited on a guess.
    if (position <= 0 || m_lastPositionLap < laps) return false;
    if (!creditLastLap(position)) return false;
    changed();
    return true;
}

void ExplorationStats::onRaceFinished(const RaceFinish& race) {
    bool moved = false;
    if (m_firstLapPosition > 0 && race.position > 0) {
        moved |= raise(Signal::PositionsGained, m_firstLapPosition - race.position);
    }
    // LED EVERY LAP, and there was a lap to lead: m_ledEveryLap starts true and
    // only a lap position can falsify it, so without the second half a win in a
    // race whose lap positions never arrived would read as a lights-to-flag one.
    const bool ledEveryLap = m_ledEveryLap && m_lastPositionLap > 0;
    // Starters, not entries: a rider who never took the start was not a field
    // to lead. The holeshot arm read the grid at the drop for the same reason.
    const bool field = race.starters >= MIN_FIELD;
    if (race.position == 1) {
        // NOT m_firstLapPosition. That field used to BE the lap-one position,
        // which made this pair redundant; since Charger moved it to the opening
        // SPLIT it means the first corner instead, and the conjunct quietly
        // turned "lead every lap" into "take the holeshot and lead every lap" -
        // in every gate-drop race, because the player always crosses that split.
        // Leading from the first corner is Perfect Race's business, below.
        if (field && ledEveryLap) { add(Signal::WireToWire, 1.0); moved = true; }
        // add(), not mark(), though the row is a one-shot: a one-shot draws no
        // progress numbers, so the tally past one is invisible in game and
        // still reaches the stats file, where the usage survey can say how
        // often this really happens. back_marker and ninety_nine accumulate
        // behind a single tier for the same reason.
        if (race.everyOtherRiderLapped) { add(Signal::LappedField, 1.0); moved = true; }
        // Perfect Race: the holeshot, every lap led, the fastest lap and the
        // win. Leading every lap says nothing about the START - you can take
        // the lead in the first corner and still not have led out of the gate -
        // so the holeshot is what pins the beginning, and it is the ONLY row
        // that asks for it.
        if (field && m_gotHoleshot && ledEveryLap && race.hadFastestLap) {
            moved |= mark(Signal::PerfectRace);
        }
    }
    // Consolation Prize: the fastest lap, and off the podium with it.
    if (race.hadFastestLap && race.position > 3) moved |= mark(Signal::ConsolationPrize);
    // The two MARGIN rows, together and on their own: the margin is read from the
    // lap logs, which can still be filling when the classification settles, so
    // this pair is retried at RunDeinit (StatsManager::retryFinishMargin) while
    // everything else here has already counted. Marks only, so a second call is
    // a no-op rather than a double count.
    moved |= onFinishMargin(race.position, race.gapToSecondMs, race.gapToWinnerMs);
    // Last Gasp: a place taken on the final lap. Needs a lap before it, so a
    // one-lap race never counts - there is no last lap to distinguish.
    //
    // AND IT NEEDS THE FINAL LAP TO HAVE LANDED. The pair is fed from RaceLap
    // and the finish from the classification, and when the field settles first
    // the pair is still one lap behind - which reads a place taken on the
    // PENULTIMATE lap as a last-lap pass. Same race between two callbacks the
    // margin rows carry; same answer, deferred to RunDeinit (retryLastGasp).
    if (m_lastPositionLap >= race.lapsAtFinish) {
        moved |= creditLastLap(race.position);
    } else {
        m_pendingLastGaspPosition = race.position;
        m_pendingLastGaspLaps = race.lapsAtFinish;
    }
    // Survivor: a quarter of the STARTERS gone. The floor is what stops a
    // four-rider lobby losing one rider from counting; a DNS is not a
    // retirement, so they are out of both halves of the fraction.
    if (race.starters >= SURVIVOR_MIN_STARTERS && race.retired * 4 >= race.starters) {
        add(Signal::Survivals, 1.0);
        moved = true;
    }
    // Nobody else on the grid. Not a feat, just a moment worth noticing - the
    // server emptied out, or you lined up alone and raced yourself.
    if (race.starters == 1) moved |= mark(Signal::SoloRace);
    if (race.ownGapLaps > 0) moved |= raise(Signal::BackMarker, race.ownGapLaps);   // the most laps down, on the way to three
    if (moved) changed();
}

void ExplorationStats::onFuelBurnt(double litres) {
    if (!(litres > 0.0)) return;
    add(Signal::FuelBurnt, litres);
    changed();
}

void ExplorationStats::onRanDry() {
    if (mark(Signal::RanDry)) changed();
}

void ExplorationStats::onFinishedOnFumes() {
    if (mark(Signal::Fumes)) changed();
}

void ExplorationStats::onProximityTime(double roostSeconds) {
    if (roostSeconds <= 0.0) return;
    add(Signal::RoostSec, roostSeconds);
    changed();
}

void ExplorationStats::onCrashSpotRun(int run) {
    if (run <= 0) return;
    if (raise(Signal::FavoriteSpot, run)) changed();
}

void ExplorationStats::onRidersDown(int down) {
    if (down <= 0) return;
    if (raise(Signal::PileUp, down)) changed();
}

void ExplorationStats::onRodeThrough(int down) {
    if (down <= 0) return;
    if (raise(Signal::PeaceOut, down)) changed();
}

void ExplorationStats::onDiggingTime(double seconds) {
    if (!(seconds > 0.0)) return;
    if (raise(Signal::DiggingSec, seconds)) changed();
}

void ExplorationStats::onGForce(float g) {
    // A landing reads a few g; anything past this is the physics engine losing
    // its footing (a spawn, a wall, a reset), and a lifetime PEAK is exactly the
    // number a single glitch would ruin permanently.
    constexpr float MAX_PLAUSIBLE_G = 50.0f;
    if (!std::isfinite(g) || g <= 0.0f || g > MAX_PLAUSIBLE_G) return;
    if (raise(Signal::PeakG, g)) changed();
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

void ExplorationStats::onTrickTime(bool shred, float seconds) {
    if (!(seconds > 0.0f) || !(seconds < 3600.0f)) return;   // finite, sane
    // Airtime is NOT taken from here any more: a trick has to be classified,
    // committed and landed to arrive, and most jumps are none of those, so Air
    // Miles counted a fraction of the time actually spent in the air.
    // The air rows measure FLIGHTS now (onFlight), so nothing airborne comes
    // through here at all -- the `airborne` parameter this used to take was
    // dead from the day the flight detector landed and went with Hang Time.
    // Shredding has no flight path (it only happens as a trick), so it stays.
    if (!shred) return;
    add(Signal::ShredSec, seconds);
    changed();
}

// THE WHOLE AIR GROUP, from this pair: three measurements read two ways each.
// onFlight() sums every flight it is handed; onFlightLanded(), below, takes the
// best of them. What makes the two differ is the CALLER, not these functions --
// FmxManager banks the sums at touchdown, casing out included, and holds the
// maxima back until the rider has stayed upright, so a max is a jump ridden
// away from. Keep any new air row in this pair: a second entry point is how
// Hang Time ended up measuring tricks while its sentence said jumps.
void ExplorationStats::onFlight(float seconds, float heightM, float distanceM) {
    if (!(seconds > 0.0f) || !(seconds < 3600.0f)) return;
    add(Signal::AirtimeSec, seconds);
    add(Signal::AirDistanceKm, distanceM / 1000.0);
    add(Signal::AirHeightKm, heightM / 1000.0);
    changed();
}

void ExplorationStats::onFlightLanded(float seconds, float heightM, float distanceM) {
    if (!(seconds > 0.0f) || !(seconds < 3600.0f)) return;
    raise(Signal::LongestFlightSec, seconds);
    raise(Signal::JumpHeightM, heightM);
    raise(Signal::JumpDistanceM, distanceM);
    changed();
}

void ExplorationStats::tick(bool spectating, bool rumbleLive, bool onTrack, bool moving, int framesThisSecond,
                            uint32_t overlayConnectionsTotal) {
    bool moved = false;
    if (spectating) { add(Signal::SpectateHours, 1.0 / 3600.0); moved = true; }
    // A pad that is actually buzzing is a pad with rumble switched on, so this
    // catches a player who enabled it mid-session without a settings reload.
    // The settings read in checkSwitches() is the real feed; this is the
    // backstop, and the only remaining use of onTrack in here.
    if (rumbleLive && onTrack) moved |= mark(Signal::RumbleOn);
    // Early Access, same shape and for the same reason: the Updates tab changes
    // the channel with a click and reloads nothing, so the settings read in
    // checkSwitches() would not see it until the next launch. One atomic load a
    // second, and mark() stops caring the moment it is set.
    if (UpdateChecker::getInstance().isPrereleaseChannel()) moved |= mark(Signal::Prerelease);
    moved |= raise(Signal::FramePerfect, framesThisSecond);   // the best second, on the way to 480
    // The clock, once a minute: a ride that runs into the small hours, a
    // session across midnight, the anniversary arriving mid-session.
    if (++m_clockTicks >= 60) {
        m_clockTicks = 0;
        // MOVING, not merely on track: Night Owl and Anniversary sit behind
        // this gate and both say "Ride". The day rollover above is not gated -
        // Regular and On a Roll count showing up, not riding.
        moved |= checkClock(moving);
    }
    if (moving) {
        // RIDING time since the last crash (or the session's start). Parking
        // pauses this clock exactly as a trip to the pits does - it does not
        // restart it - so the row reads as it says: half an hour of riding
        // without a crash, not half an hour of it in one unbroken go, and not
        // half an hour of sitting still, which is not riding and not a feat.
        m_crashFreeMs += 1000.0;
        moved |= raise(Signal::SteadyHands, m_crashFreeMs / 1000.0);   // the longest run, in seconds
        // Iron Butt: hours ridden within one local DAY, not one session. A
        // session ends at a crash, a pit visit or a track exit, so the old
        // per-session figure punished exactly the marathon it was meant to
        // reward. The day is the one checkClock keeps (re-read once a minute),
        // so this shares its granularity rather than reading the clock at 1Hz.
        if (m_rideDay != m_lastDay) {
            m_rideDay = m_lastDay;
            m_todayRideSec = 0.0;
        }
        m_todayRideSec += 1.0;
        moved |= raise(Signal::DayRideHours, m_todayRideSec / 3600.0);
    }
    // The overlay's total restarts with the process: count what it grew by.
    if (overlayConnectionsTotal > m_overlaySeen) {
        add(Signal::OverlayConnections, static_cast<double>(overlayConnectionsTotal - m_overlaySeen));
        m_overlaySeen = overlayConnectionsTotal;
        moved = true;
    }
    if (moved) changed();
}
