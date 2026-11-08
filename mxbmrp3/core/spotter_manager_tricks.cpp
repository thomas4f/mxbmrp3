// ============================================================================
// core/spotter_manager_tricks.cpp
// SpotterManager's two opt-in, for-fun categories: your freestyle tricks
// (reported by FmxManager) and the achievements you earn (reported by
// AchievementManager). Tricks ships off. Both only ever speak about YOU, so
// neither touches the race-event path in spotter_manager_events.cpp.
//
// The wording is all in the shipped pack, under its TRICKS and ACHIEVEMENTS
// headings, and those two break the pack's "flat" rule on purpose: they are
// the arcade announcer, not the race engineer.
// ============================================================================
#include "spotter_manager.h"

#include "plugin_data.h"
#include "spotter_tricks.h"

#include <string>

// Every trick cue's gate: the category, and NEVER a race session. FMX detection
// runs on every ride, and on a motocross track a scrub or a whip is how you
// take a jump fast rather than a trick you are showing off - so a race with
// this on would be a running commentary on your jumping. Freeriding, practice
// and warm-up are where tricks are the point.
bool SpotterManager::tricksAllowed() const {
    if (!m_enabled && !m_subtitles) return false;
    if (!isCategoryEnabled(SpotterPhrase::Category::Tricks)) return false;
    return !PluginData::getInstance().isRaceSession();
}

// The NAME is said only once the trick is committed (trick_landed). Before
// that it is a guess: a trick classified half-way can still turn into another
// one ("going for the scrub" over what became a whip), and one that never
// landed was never really that trick. So the start and the crash cues carry no
// {trick} - the type here only decides WHETHER there is something to call.
void SpotterManager::onTrickStarted(Fmx::TrickType type) {
    if (!tricksAllowed() || !SpotterTricks::isCallable(type)) return;
    emitCue("trick_started", SpotterPhrase::Category::Tricks, SpotterVars::Vars{},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::onTrickLanded(Fmx::TrickType type, int multiplier, int level) {
    if (!tricksAllowed() || !SpotterTricks::isCallable(type)) return;
    SpotterVars::Vars v;
    v.trick = SpotterTricks::spokenTrick(type, multiplier, level);
    emitCue("trick_landed", SpotterPhrase::Category::Tricks, std::move(v),
            PluginData::getInstance().getSessionElapsedTime());
}

// The caller only reports a crash that took something with it - a named trick
// or a live chain - so a plain crash off a jump stays crashed_you's business.
void SpotterManager::onTrickCrashed() {
    if (!tricksAllowed()) return;
    emitCue("trick_crashed", SpotterPhrase::Category::Tricks, SpotterVars::Vars{},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::onTrickCombo(int trickCount) {
    if (!tricksAllowed() || trickCount < SpotterTricks::kComboMin) return;
    SpotterVars::Vars v;
    v.comboCount = SpotterPhrase::numberWords(trickCount);
    emitCue("trick_combo", SpotterPhrase::Category::Tricks, std::move(v),
            PluginData::getInstance().getSessionElapsedTime());
}

// One cue per catalogue row (the achievement_<id> family in spotter_cue_pack.h),
// so every achievement can have its own joke. Earned anywhere - several rows
// unlock in the settings menu, and the spotter's own thread speaks there too.
void SpotterManager::onAchievement(const char* id, const char* title,
                                   const char* tierName) {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Achievements) || !id) return;
    SpotterVars::Vars v;
    // Titles and tiers are the catalogue's N_() strings: in the spotter's
    // language when it speaks one, the same words the toast shows.
    if (title) v.achievement = spokenText(title);
    if (tierName) {
        // Lowercase: the tier is spoken mid-line ("that's gold"), and the
        // expander only capitalises the start of a line. ASCII only, which
        // leaves a translation's UTF-8 letters as they are.
        v.tier = spokenText(tierName);
        for (char& c : v.tier) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
    }
    const std::string key = std::string(SpotterCuePack::kAchievementCuePrefix) + id;
    emitCue(key.c_str(), SpotterPhrase::Category::Achievements, std::move(v),
            PluginData::getInstance().getSessionElapsedTime());
}
