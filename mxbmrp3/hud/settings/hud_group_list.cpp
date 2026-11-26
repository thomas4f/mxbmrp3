// ============================================================================
// hud/settings/hud_group_list.cpp
// THE GROUPED HUD LIST (settings_layout.h, HudGroupList): the Widgets and Decks
// tabs' one drawing and one landing. Each tab states its rows and groups as data
// in its own settings_tab_*.cpp; nothing here knows which tab it is drawing.
//
// THE SHAPE, the Achievements tab's: the column labels ride on a fixed card's
// heading -- no label over the name column, the list's title stands there --
// and the groups scroll under it, each in its own card, its name a row of the
// list that shares its first entry's view (addListHeading). A list ends its
// tab, so it fills down to the footer (planFilled). A switchesOnly list (the
// Decks tab's) draws only each row's Visible switch, two to a line in the
// Style card's cells -- and so has no labels and no labels' card: a heading
// over nothing would be the one empty card in the panel. Its groups start
// where that card stood, and the list takes its line and seam, so the tab
// keeps its height.
//
// THE LANDING is decided when the tab opens rather than when it is drawn: a
// drawing that edits what it draws would also pull the list back every time it
// was rebuilt. A Find result names its row (landTooltipId, set just before the
// tab opens); otherwise a "New" row the player has not hovered yet, whose band
// would be no use scrolled out of sight. Neither: the list stays where the
// player left it.
// ============================================================================
#include "settings_layout.h"
#include "whats_new.h"
#include "../settings_hud.h"
#include "../../core/i18n.h"

#include <cstring>

const HudGroupList::List* HudGroupList::of(int tabId) {
    if (tabId == widgets().tabId) return &widgets();
    if (tabId == decks().tabId) return &decks();
    return nullptr;
}

void SettingsHud::landListRow(int tabId) {
    m_listJumpTo = nullptr;
    const HudGroupList::List* list = HudGroupList::of(tabId);
    if (!list) return;
    for (int i = 0; i < list->rowCount; ++i) {
        if (m_find.landTooltipId == list->rows[i].tooltipId) m_listJumpTo = list->rows[i].tooltipId;
    }
    for (int i = 0; !m_listJumpTo && i < list->rowCount; ++i) {
        if (WhatsNew::liveForRow(tabId, list->rows[i].tooltipId)) m_listJumpTo = list->rows[i].tooltipId;
    }
}

void SettingsLayoutContext::addHudGroupList(const HudGroupList::List& list, float viewLines) {
    SettingsHud& hud = *parent;

    if (list.switchesOnly) {
        // No labels' card: the list takes its heading line and the seam under it
        const float seam = hud.cardPadBotY() + hud.contentGapY() + hud.cardPadTopY();
        viewLines += 1.0f + seam / lineHeightNormal;
    } else {
        const WidgetColumns cols = widgetColumns();
        const float headingY = addSectionHeading(I18n::tr(list.title));
        const auto columnLabel = [&](const char* text, float x) {
            hud.addString(text, x, headingY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), fontSize);
        };
        columnLabel(TR("Visible"), cols.visX);
        columnLabel(TR("Title"), cols.titleX);
        columnLabel(TR("Texture"), cols.texX);
        columnLabel(TR("Opacity"), cols.opacityX);
        columnLabel(TR("Scale"), cols.scaleX);
    }

    // The labels' row is a card of its own, fixed; the groups scroll under it
    beginUntitledSection();
    beginScroll(list.scroll, viewLines);
    const char* jumpTo = hud.m_listJumpTo;
    hud.m_listJumpTo = nullptr;
    const auto lands = [jumpTo](const HudGroupList::Row* row) {
        return jumpTo && row && std::strcmp(jumpTo, row->tooltipId) == 0;
    };
    // A Visible switch as a cell of a two-cell line, "Name  < On >" as the
    // Style card's switches draw, on the surface being edited (as addWidgetRow)
    const auto addSwitch = [&](const HudGroupList::Row& row, BaseHud* target) {
        addToggleControl(I18n::tr(row.name), target->isVisibleOnActiveSurface(),
                         SettingsHud::ClickRegion::HUD_TOGGLE, target, static_cast<uint32_t*>(nullptr), 0u,
                         true, row.tooltipId);
    };
    for (int g = 0; g < list.groupCount; ++g) {
        bool first = true;   // the group's name shares its first row; a group with none is left out
        // switchesOnly: the line's left cell, waiting for its right one
        const HudGroupList::Row* left = nullptr;
        BaseHud* leftTarget = nullptr;
        // Opens the next line of the list; false when it is out of view
        const auto openLine = [&](bool landsHere) {
            if (first) scrollGroup();
            if (landsHere) scrollToHere();
            const bool drawn = scrollRow(first ? 2.0f : 1.0f);
            if (drawn && first) addListHeading(I18n::tr(list.groups[g]));
            first = false;
            return drawn;
        };
        const auto addSwitchLine = [&](const HudGroupList::Row* right, BaseHud* rightTarget) {
            if (!openLine(lands(left) || lands(right))) return;
            beginColumns(2, right ? 2 : 1);
            addSwitch(*left, leftTarget);
            if (right) addSwitch(*right, rightTarget);
            endColumns();
        };
        for (int i = 0; i < list.rowCount; ++i) {
            const HudGroupList::Row& row = list.rows[i];
            if (row.group != g) continue;
            BaseHud* target = row.get(hud);
            if (!target) continue;
            if (list.switchesOnly) {
                if (!left) {
                    left = &row;
                    leftTarget = target;
                    continue;
                }
                addSwitchLine(&row, target);
                left = nullptr;
                continue;
            }
            if (!openLine(lands(&row))) continue;
            addWidgetRow(I18n::tr(row.name), target, !row.pointer, true, !row.pointer, true,
                         row.tooltipId, row.pointer);
        }
        if (left) addSwitchLine(nullptr, nullptr);
    }
    endScroll();
}
