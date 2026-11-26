// ============================================================================
// tests/integration/harness/deck_ids.h
// Every deck (hud/deck_hud.h), by harness id and INI section, in
// DeckHud::Kind order, then the carousel (it shows Timing first): one list
// for every test that walks the decks.
// Combo and Wheelie are the bike games' only; a game without one reports
// hudVisible() < 0 for it.
// ============================================================================
#pragma once

#include <iterator>

inline constexpr const char* kDeckIds[] = {
    "speed_deck", "race_deck", "timing_deck", "wheelie_deck", "slide_deck", "combo_deck",
    "gap_deck", "rival_deck", "spin_deck", "inputs_deck", "standings_deck", "session_deck",
    "notice_deck", "fuel_deck", "media_deck", "laplog_deck", "carousel_deck"
};
// Their INI sections, in the same order
inline constexpr const char* kDeckSections[] = {
    "SpeedDeck", "RaceDeck", "TimingDeck", "WheelieDeck", "SlideDeck", "ComboDeck",
    "GapDeck", "RivalDeck", "SpinDeck", "InputsDeck", "StandingsDeck", "SessionDeck",
    "NoticeDeck", "FuelDeck", "MediaDeck", "LapLogDeck", "CarouselDeck"
};
static_assert(std::size(kDeckSections) == std::size(kDeckIds), "one section per deck");
