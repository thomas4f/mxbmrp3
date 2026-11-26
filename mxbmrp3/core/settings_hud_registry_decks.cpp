// ============================================================================
// core/settings_hud_registry_decks.cpp
// Save/load for the decks (hud/deck_hud.h): one capture/apply pair
// for every deck, each found by its section name, which the registry row
// passes in, storing the base HUD settings but those every deck shares (and
// the carousel's own, by NAME);
// and [Decks], the one Style every deck draws in, by NAME, with every
// deck's Title, Opacity and Scale.
// ============================================================================
#include "settings_hud_registry.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde_hud.h"
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/deck_hud.h"

using namespace Settings;

namespace {
DeckHud* deckForSection(const HudManager& hudManager, const char* name) {
    for (int i = 0; i < HudManager::DECK_HUD_COUNT; ++i) {
        if (name && std::strcmp(DeckHud::sectionName(i), name) == 0)
            return hudManager.getDeckHud(i);
    }
    return nullptr;
}

// The carousel's decks, one key each by the deck's name, and its marker
const char* const kCarouselDeckKeys[] = { "deckTiming", "deckGap", "deckRival", "deckSession", "deckInputs",
                                          "deckLapLog",
#if GAME_HAS_FMX
                                          "deckCombo",
#endif
                                          "deckNowPlaying" };
static_assert(std::size(kCarouselDeckKeys) == static_cast<size_t>(DeckHud::CAROUSEL_SIZE), "one key per carousel deck");
const char* const kMarkerKeys[] = { "off", "dots", "count" };
static_assert(std::size(kMarkerKeys) == static_cast<size_t>(DeckHud::Marker::COUNT), "one key per marker");
}  // namespace

void SettingsManager::cap_DeckHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    const DeckHud* deck = deckForSection(hudManager, name);
    if (!deck) return;
    HudSettings settings;
    captureBaseHudSettings(settings, *deck);
    // Every deck's Title, Opacity and Scale are [Decks]' (cap_Decks), and no
    // deck has a Theme of its own (DeckHud::applyShared)
    for (const char* shared : { Keys::Base::SHOW_TITLE, Keys::Base::THEME, Keys::Base::BG_OPACITY, Keys::Base::SCALE })
        settings.erase(shared);
    if (deck->isCarousel()) {
        for (int i = 0; i < DeckHud::CAROUSEL_SIZE; ++i)
            saveBitAsKey(settings, kCarouselDeckKeys[i], deck->m_carDecks, 1u << i);
        settings[Keys::Deck::CAROUSEL_AUTO] = std::to_string(DeckHud::AUTO_STEPS_S[std::clamp(deck->m_carAuto, 0, DeckHud::AUTO_STEP_COUNT - 1)]);
        settings[Keys::Deck::CAROUSEL_MARKER] = kMarkerKeys[static_cast<int>(deck->m_carMarker)];
        saveBitAsKey(settings, Keys::Deck::CAROUSEL_NAMES, deck->m_carOptions, DeckHud::CAR_NAMES);
        saveBitAsKey(settings, Keys::Deck::CAROUSEL_NOTICES, deck->m_carOptions, DeckHud::CAR_NOTICES);
    }
    cache[name] = std::move(settings);
}

void SettingsManager::app_DeckHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    DeckHud* deck = deckForSection(hudManager, name);
    auto it = cache.find(name);
    if (!deck || it == cache.end()) return;
    applyBaseHudSettings(*deck, it->second);
    if (deck->isCarousel()) {
        const HudSettings& in = it->second;
        for (int i = 0; i < DeckHud::CAROUSEL_SIZE; ++i) loadBitFromKey(in, kCarouselDeckKeys[i], deck->m_carDecks, 1u << i);
        if ((deck->m_carDecks & DeckHud::CAROUSEL_ALL) == 0) deck->m_carDecks = DeckHud::CAROUSEL_ALL;
        // Auto by its seconds: the nearest step offered (a hand-edited 6 is 5 s)
        if (const std::optional<int> secs = readInt(in, Keys::Deck::CAROUSEL_AUTO)) {
            int best = 0;
            for (int k = 1; k < DeckHud::AUTO_STEP_COUNT; ++k) {
                if (std::abs(DeckHud::AUTO_STEPS_S[k] - *secs) < std::abs(DeckHud::AUTO_STEPS_S[best] - *secs)) best = k;
            }
            deck->m_carAuto = best;
        }
        // The marker by name; an unknown one keeps the default
        if (const std::string* marker = readStr(in, Keys::Deck::CAROUSEL_MARKER)) {
            for (int k = 0; k < static_cast<int>(DeckHud::Marker::COUNT); ++k) {
                if (*marker == kMarkerKeys[k]) deck->m_carMarker = static_cast<DeckHud::Marker>(k);
            }
        }
        loadBitFromKey(in, Keys::Deck::CAROUSEL_NAMES, deck->m_carOptions, DeckHud::CAR_NAMES);
        loadBitFromKey(in, Keys::Deck::CAROUSEL_NOTICES, deck->m_carOptions, DeckHud::CAR_NOTICES);
    }
    deck->setDataDirty();
}

void SettingsManager::cap_Decks(const HudManager&, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    settings[Keys::Deck::SLANTED] = DeckHud::slanted() ? "1" : "0";
    settings[Keys::Deck::BARS] = DeckHud::bars() ? "1" : "0";
    settings[Keys::Deck::STAMPS] = DeckHud::stamps() ? "1" : "0";
    settings[Keys::Deck::TITLE] = DeckHud::sharedTitle() ? "1" : "0";
    settings[Keys::Deck::OPACITY] = std::to_string(DeckHud::sharedOpacity());
    settings[Keys::Deck::SCALE] = std::to_string(DeckHud::sharedScale());
    cache[name] = std::move(settings);
}

void SettingsManager::app_Decks(HudManager&, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it != cache.end()) {
        try {
            auto slanted = it->second.find(Keys::Deck::SLANTED);
            if (slanted != it->second.end()) DeckHud::setSlanted(slanted->second == "1");
            auto bars = it->second.find(Keys::Deck::BARS);
            if (bars != it->second.end()) DeckHud::setBars(bars->second != "0");
            auto stamps = it->second.find(Keys::Deck::STAMPS);
            if (stamps != it->second.end()) DeckHud::setStamps(stamps->second != "0");
            auto title = it->second.find(Keys::Deck::TITLE);
            if (title != it->second.end()) DeckHud::setSharedTitle(title->second != "0");
            // Finite (readFloat) and clamped (the setters); a bad value keeps the one held
            if (const std::optional<float> opacity = readFloat(it->second, Keys::Deck::OPACITY))
                DeckHud::setSharedOpacity(*opacity);
            if (const std::optional<float> scale = readFloat(it->second, Keys::Deck::SCALE))
                DeckHud::setSharedScale(*scale);
        } catch (const std::exception& e) {
            DEBUG_WARN_F("%s: Failed to parse settings: %s", name, e.what());
        }
    }
    // Applied after every deck's own section (the registry's order), so a
    // hand-edited [XxxDeck] title, opacity, scale or theme is overruled
    DeckHud::applyShared();
}
