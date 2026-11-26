// ============================================================================
// hud/settings/settings_tab_decks.cpp
// Tab renderer for the decks (hud/deck_hud.h): the settings every deck reads
// (Slanted, Title, Bars, Stamps, Opacity, Scale) and how the carousel turns,
// in fixed cards; then each deck's Visible switch, two to a line, in groups --
// the Widgets tab's grouped HUD list (hud_group_list.cpp), drawn by the same
// code. A deck has no look of its own: a set reads as one graphic, and lines
// up on its cell grid at any size (DeckHud::applyShared).
//
// GROUPS by what a deck shows: where you are in the race, your times against
// the others', the bike, how it moves, and the rest. The names are the Widgets
// tab's own where the two mean the same thing. The carousel's row sits with the
// extras rather than in its own card: it is a deck you switch on like the
// others, and its card above holds how it turns.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../deck_hud.h"
#include "../../core/asset_manager.h"
#include "../../core/hud_manager.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
DeckHud* deck(DeckHud::Kind kind) {
    return HudManager::getInstance().getDeckHud(static_cast<int>(kind));
}

// A row's deck (HudGroupList::Row::get), by kind; nullptr on a game without it
// (Wheelie and Combo read a bike's signals)
template <DeckHud::Kind K>
BaseHud* deckRow(const SettingsHud&) { return deck(K); }
BaseHud* carouselRow(const SettingsHud&) {
    return HudManager::getInstance().getDeckHud(DeckHud::CAROUSEL_SLOT);
}

enum Group { RACE, TIMING, BIKE, MOVEMENT, EXTRAS };

const char* const kGroups[] = { N_("Race"), N_("Timing"), N_("Bike"), N_("Movement"), N_("Extras") };
constexpr int GROUP_COUNT = static_cast<int>(sizeof(kGroups) / sizeof(kGroups[0]));

using K = DeckHud::Kind;
const HudGroupList::Row kRows[] = {
    { RACE,     N_("Race"),        "decks.race",      &deckRow<K::Race> },
    { RACE,     N_("Standings"),   "decks.standings", &deckRow<K::Standings> },
    { RACE,     N_("Session"),     "decks.session",   &deckRow<K::Session> },
    { RACE,     N_("Notices"),     "decks.notice",    &deckRow<K::Notice> },
    { TIMING,   N_("Timing"),      "decks.timing",    &deckRow<K::Timing> },
    { TIMING,   N_("Gap"),         "decks.gap",       &deckRow<K::Gap> },
    { TIMING,   N_("Rival"),       "decks.rival",     &deckRow<K::Rival> },
    { TIMING,   N_("Lap Log"),     "decks.laplog",    &deckRow<K::LapLog> },
    { BIKE,     N_("Speed"),       "decks.speed",     &deckRow<K::Speed> },
    { BIKE,     N_("Fuel"),        "decks.fuel",      &deckRow<K::Fuel> },
    { MOVEMENT, N_("Wheelie"),     "decks.wheelie",   &deckRow<K::Wheelie> },
    { MOVEMENT, N_("Slide"),       "decks.slide",     &deckRow<K::Slide> },
    { MOVEMENT, N_("Spin"),        "decks.spin",      &deckRow<K::Spin> },
    { MOVEMENT, N_("Combo"),       "decks.combo",     &deckRow<K::Combo> },
    { EXTRAS,   N_("Inputs"),      "decks.inputs",    &deckRow<K::Inputs> },
    { EXTRAS,   N_("Now Playing"), "decks.media",     &deckRow<K::Media> },
    { EXTRAS,   N_("Carousel"),    "decks.carousel",  &carouselRow },
};
constexpr int ROW_COUNT = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

// The first deck this build has: the Style switches' dirty target.
DeckHud* firstDeck() {
    for (int i = 0; i < HudManager::DECK_HUD_COUNT; ++i) {
        if (DeckHud* d = HudManager::getInstance().getDeckHud(i)) return d;
    }
    return nullptr;
}

// Off / On as a two-step cycle, for a switch every deck reads (Slanted, Title,
// Bars, Stamps): it draws as every toggle row does, with no click type of its own.
SettingsHud::CycleControl deckSwitch(bool (*get)(), void (*set)(bool)) {
    SettingsHud::CycleControl c;
    c.count = 2;
    c.get = [get]() { return get() ? 1 : 0; };
    c.set = [set](int i) { set(i == 1); };
    c.nameOf = [](int i) { return std::string(i == 1 ? TR("On") : TR("Off")); };
    c.dirtyHud = firstDeck();
    c.repeat = false;
    return c;
}

// A percentage every deck reads (Opacity, Scale), a slider like a HUD's own
// (SettingsLayoutContext::addOpacityControl), 1% a step
SettingsHud::SteppedControl deckPercent(float (*get)(), void (*set)(float), float min, float max) {
    return SettingsHud::SteppedControl::accessor(get, set, 0.01f, min, max, firstDeck());
}

void addDeckPercent(SettingsLayoutContext& ctx, const char* label, const SettingsHud::SteppedControl& control,
                    float value, bool enabled, const char* tooltipId) {
    char text[16];
    snprintf(text, sizeof(text), "%d%%", static_cast<int>(std::round(value * 100.0f)));
    ctx.addSteppedControl(label, text, control, control.dirtyHud, enabled, false, tooltipId);
}
}  // namespace

const HudGroupList::List& HudGroupList::decks() {
    static const List list = { SettingsHud::TAB_DECKS, ScrollView::DECKS, nullptr,
                               kGroups, GROUP_COUNT, kRows, ROW_COUNT, true };
    return list;
}

BaseHud* SettingsHud::renderTabDecks(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("decks");

    ctx.addSectionHeading(TR("Style"));
    {
        // Slanted: each deck's theme's Regular or Slanted layout (AssetManager::getThemeDeck)
        SettingsHud::CycleControl slanted = deckSwitch(&DeckHud::slanted, &DeckHud::setSlanted);
        // A layout sizes its decks its own way: keep each one on screen.
        slanted.postStep = []() { HudManager::getInstance().requestPositionValidation(); };
        const SettingsHud::CycleControl title = deckSwitch(&DeckHud::sharedTitle, &DeckHud::setSharedTitle);
        const SettingsHud::CycleControl bars = deckSwitch(&DeckHud::bars, &DeckHud::setBars);
        const SettingsHud::CycleControl stamps = deckSwitch(&DeckHud::stamps, &DeckHud::setStamps);
        const SettingsHud::SteppedControl opacity =
            deckPercent(&DeckHud::sharedOpacity, &DeckHud::setSharedOpacity, 0.0f, 1.0f);
        const SettingsHud::SteppedControl scale =
            deckPercent(&DeckHud::sharedScale, &DeckHud::setSharedScale, DeckHud::MIN_SCALE, DeckHud::MAX_SCALE);
        const bool any = slanted.dirtyHud != nullptr;
        ctx.beginColumns(2, 6);
        ctx.addCycleControl(TRC("deck style", "Slanted"), slanted.nameOf(slanted.get()).c_str(), slanted,
                            slanted.dirtyHud, any, !DeckHud::slanted(), "decks.slanted");
        ctx.addCycleControl(TR("Title"), title.nameOf(title.get()).c_str(), title,
                            title.dirtyHud, any, !DeckHud::sharedTitle(), "decks.title");
        ctx.addCycleControl(TR("Bars"), bars.nameOf(bars.get()).c_str(), bars,
                            bars.dirtyHud, any, !DeckHud::bars(), "decks.bars");
        ctx.addCycleControl(TR("Stamps"), stamps.nameOf(stamps.get()).c_str(), stamps,
                            stamps.dirtyHud, any, !DeckHud::stamps(), "decks.stamps");
        addDeckPercent(ctx, TR("Opacity"), opacity, DeckHud::sharedOpacity(), any, "decks.opacity");
        addDeckPercent(ctx, TR("Scale"), scale, DeckHud::sharedScale(), any, "decks.scale");
        ctx.endColumns();
    }

    // The carousel (DeckHud, THE CAROUSEL): how it turns. Two rows of two,
    // so the tab stays within the panel's height (the tallest tab sets every
    // tab's); which decks it turns through is INI-only for want of room
    // ([CarouselDeck] deckTiming=0 ...). Its own row is in the list below.
    DeckHud* carousel = HudManager::getInstance().getDeckHud(DeckHud::CAROUSEL_SLOT);
    if (carousel) {
        ctx.addSectionHeading(TR("Carousel"));
        ctx.beginColumns(2, 4, 18);
        {
            SettingsHud::CycleControl c = SettingsHud::CycleControl::enumMember(
                carousel, &DeckHud::m_carAuto, DeckHud::AUTO_STEP_COUNT, carousel);
            c.nameOf = [](int i) {
                const int s = DeckHud::AUTO_STEPS_S[i];
                if (s <= 0) return std::string(TR("Off"));
                char text[16];
                snprintf(text, sizeof(text), "%d s", s);
                return std::string(text);
            };
            ctx.addCycleControl(TR("Auto"), c.nameOf(carousel->m_carAuto).c_str(), c, carousel, true,
                                DeckHud::AUTO_STEPS_S[carousel->m_carAuto] <= 0, "decks.carousel_auto");
        }
        {
            static const char* const kMarkers[] = { N_("Off"), N_("Dots"), N_("Count") };
            ctx.addCycleControl(TR("Marker"), I18n::tr(kMarkers[static_cast<int>(carousel->m_carMarker)]),
                SettingsHud::CycleControl::enumMember(carousel, &DeckHud::m_carMarker,
                                                      static_cast<int>(DeckHud::Marker::COUNT), carousel, kMarkers),
                carousel, true, carousel->m_carMarker == DeckHud::Marker::Off, "decks.carousel_marker");
        }
        ctx.addToggleControl(TR("Deck names"), (carousel->m_carOptions & DeckHud::CAR_NAMES) != 0,
                             SettingsHud::ClickRegion::CHECKBOX, carousel, &carousel->m_carOptions, DeckHud::CAR_NAMES,
                             true, "decks.carousel_names");
        ctx.addToggleControl(TR("Notice focus"), (carousel->m_carOptions & DeckHud::CAR_NOTICES) != 0,
                             SettingsHud::ClickRegion::CHECKBOX, carousel, &carousel->m_carOptions, DeckHud::CAR_NOTICES,
                             true, "decks.carousel_notices");
        ctx.endColumns();
    }

    // The decks, grouped. As tall as the flat table they replaced: its rows,
    // plus the second table's heading, less the line the Style card grew by
    // when Title, Opacity and Scale joined it -- and the list itself adds the
    // labels' card it does not draw -- so the panel keeps the height it had.
    // Two decks a line, the list holds them all. A list ends the tab, so a
    // taller tab elsewhere only grows it down to the footer.
    int rows = 0;
    for (const HudGroupList::Row& row : kRows) rows += row.get(*ctx.parent) ? 1 : 0;
    ctx.addHudGroupList(HudGroupList::decks(), static_cast<float>(rows));

    // No single active HUD: a multi-deck tab
    return nullptr;
}
