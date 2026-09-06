// ============================================================================
// hud/settings/settings_tab_achievements.cpp
// Tab renderer for Achievements: the lifetime numbers the plugin keeps, shown
// as tiered progress toward the next Bronze/Silver/Gold/Platinum threshold --
// one entry is both the number and the achievement it feeds. Plus the toast
// card's controls (global, like the Director's status button). The on-track
// Stats HUD is not here: it keeps its own per-profile tab (settings_tab_stats).
//
// Global manager tab, like Spotter/Director: drives AchievementManager
// directly and returns nullptr (no backing HUD).
//
// TWO ROWS PER ENTRY, PAGED, ONE ANCHOR EACH. The marker icon spans BOTH rows
// (the Steam card's shape): it is what makes two lines read as one entry, and
// the first cut drew it on the title row alone, which left every entry looking
// like two unrelated lines. Row 1 is identity -- title, and the tier earned at
// the right edge; row 2 is the task -- what the NEXT tier asks in words
// ("Finish 10 races", the toast's own sentence), and the numbers at the right
// edge. The progress is the BAND behind both rows, filled from the left: the
// same tint the hover band uses, and the entry is no hover region at all --
// one band per entry, not two. The summary block
// above the list takes the same shape, so the eye learns it once.
// Every tab shares one panel height set by the tallest (settings_fit_test /
// theme_geometry_test); ENTRIES_PER_PAGE keeps this tab under that ceiling.
// A PAGE IS A GROUP (Achievements::Group, the section heading), or an
// ENTRIES_PER_PAGE chunk of one ("Plugin 2/4"): related rows read together. A group with no row
// on this game (a kart has no Freestyle) is no page, and a hidden row is not a
// row until it is earned (Achievements::Entry::hidden). The
// pager is the Riders tab's "< Page x/y >". The row says what the next tier
// asks; it has no hover of its own (no tooltip, no tint) -- nothing to find
// by moving the mouse.
//
// ORDER within a page: the catalogue's, always. Rows used to re-sort on every
// rebuild (earned first, then the closest locked), so the row being watched
// jumped the moment it moved; a fixed order is one you can learn.
// No unlock dates: none are collected (see AchievementManager::State).
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../achievement_widget.h"
#include "../../core/achievement_manager.h"
#include "../../core/achievements.h"
#include "../../core/asset_manager.h"
#include "../../core/color_config.h"
#include "../../core/hud_manager.h"
#include "../../core/plugin_constants.h"
#include "../../core/plugin_utils.h"
#include "../../core/ui_config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace PluginConstants;

namespace {

constexpr int ENTRIES_PER_PAGE = 8;

// The metal each tier is drawn in; locked rows take the muted text colour.
unsigned long tierColor(int tier, const ColorConfig& colors) {
    switch (tier) {
        case 1: return PodiumColors::BRONZE;
        case 2: return PodiumColors::SILVER;
        case 3: return PodiumColors::GOLD;
        case 4: return PluginUtils::makeColor(229, 228, 226);
        default: return colors.getMuted();
    }
}

}  // namespace

bool SettingsHud::handleClickTabAchievements(const ClickRegion& region) {
    // ACHIEVEMENTS_TOASTS_TOGGLE is deliberately NOT here: the tab list carries
    // that same toggle as its row checkbox, and a tab-scoped handler only runs
    // while its own tab is open -- so it lives in dispatchRegion's common switch
    // (settings_hud_input.cpp), like the spotter's.
    switch (region.type) {
        case ClickRegion::ACHIEVEMENTS_PAGE_PREV:
            if (m_achievementsPage > 0) {
                m_achievementsPage--;
                rebuildRenderData();
            }
            return true;
        case ClickRegion::ACHIEVEMENTS_PAGE_NEXT:
            m_achievementsPage++;   // clamped against the page count at render
            rebuildRenderData();
            return true;
        default:
            return false;
    }
}

BaseHud* SettingsHud::renderTabAchievements(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("achievements");

    AchievementManager& ach = AchievementManager::getInstance();
    AchievementWidget* widget = HudManager::getInstance().getAchievementWidget();
    ColorConfig& colors = ColorConfig::getInstance();
    const bool toastsOn = ach.isToastsEnabled();
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    const float rowRight = ctx.labelX + ctx.rowSpanWidth();

    char buf[64];

    // === TOASTS: the card's own rows ===
    // Cell geometry: "< value >" with a 7-character value, in the control column.
    constexpr int CELL_VALUE_CHARS = 7;
    const float colX = ctx.labelX + PluginUtils::calculateMonospaceTextWidth(11, ctx.fontSize);

    ctx.addSectionHeading("Toasts");

    // One row: its label and a row-wide tooltip region.
    auto beginRow = [&](const char* label, const char* tooltipId) {
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            ctx.labelX, ctx.currentY, ctx.rowSpanWidth(), ctx.lineHeightNormal, tooltipId));
        ctx.parent->addString(label, ctx.labelX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
    };
    // "< value >" with two arrow regions of the given types, like addWidgetRow's
    // inline cycle. Returns the index of the first region pushed (the down arrow)
    // so a data-driven caller can stamp its descriptor index onto both.
    auto addCell = [&](const char* value,
                       SettingsHud::ClickRegion::Type downType,
                       SettingsHud::ClickRegion::Type upType,
                       BaseHud* target, bool enabled, bool isOff) -> size_t {
        const size_t first = ctx.parent->m_clickRegions.size();
        float cx = colX;
        const unsigned long valueColor = (enabled && !isOff) ? colors.getPrimary() : colors.getMuted();
        if (enabled) {
            ctx.parent->addString("<", cx, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                cx, ctx.currentY, cw * 2, ctx.lineHeightNormal, downType, target, 0, false, 0));
        }
        cx += cw * 2;
        const std::string formatted = SettingsLayoutContext::formatValue(value, CELL_VALUE_CHARS, false);
        ctx.parent->addString(formatted.c_str(), cx, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, ctx.fontSize);
        cx += PluginUtils::calculateMonospaceTextWidth(CELL_VALUE_CHARS, ctx.fontSize);
        if (enabled) {
            ctx.parent->addString(" >", cx, ctx.currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), ctx.fontSize);
            ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                cx, ctx.currentY, cw * 2, ctx.lineHeightNormal, upType, target, 0, false, 0));
        }
        return first;
    };
    auto addToggleCell = [&](bool on, SettingsHud::ClickRegion::Type type, BaseHud* target, bool enabled) {
        addCell(on ? "On" : "Off", type, type, target, enabled, !on);
    };
    // Every row but Visible greys out with the toasts off: live arrows under an
    // Off switch read as a control that does nothing.

    // Visible: the toast master (common handler, shared with the tab-list checkbox).
    beginRow("Visible", "achievements.toasts");
    addToggleCell(toastsOn, SettingsHud::ClickRegion::ACHIEVEMENTS_TOASTS_TOGGLE, nullptr, true);
    ctx.nextLine();

    beginRow("Title", "common.title");
    if (widget) addToggleCell(widget->getShowTitle(), SettingsHud::ClickRegion::TITLE_TOGGLE, widget, toastsOn);
    ctx.nextLine();

    // Texture: the same rule addWidgetRow applies -- a HUD with texture variants
    // cycles them, everything else cycles its panel theme.
    beginRow("Texture", "common.texture");
    if (widget) {
        const bool hasTextures = !widget->getAvailableTextureVariants().empty();
        if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
            const std::string& ov = widget->getThemeOverride();
            std::string themeValue = "Default";
            if (ov == BaseHud::THEME_NONE) {
                themeValue = "None";
            } else if (!ov.empty()) {
                if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
                    themeValue = t->displayName;
                }
            }
            addCell(themeValue.c_str(), SettingsHud::ClickRegion::HUD_THEME_DOWN,
                    SettingsHud::ClickRegion::HUD_THEME_UP, widget, toastsOn, false);
        } else {
            char texValue[8];
            const int variant = widget->getTextureVariant();
            snprintf(texValue, sizeof(texValue), (!hasTextures || variant == 0) ? "Off" : "%d", variant);
            addCell(texValue, SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN,
                    SettingsHud::ClickRegion::TEXTURE_VARIANT_UP, widget, toastsOn && hasTextures,
                    !hasTextures || variant == 0);
        }
    }
    ctx.nextLine();

    beginRow("Opacity", "common.opacity");
    if (widget) {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(widget->getBackgroundOpacity() * 100.0f + 0.5f));
        addCell(buf, SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
                SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP, widget, toastsOn, false);
    }
    ctx.nextLine();

    beginRow("Scale", "common.scale");
    if (widget) {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(widget->getScale() * 100.0f + 0.5f));
        addCell(buf, SettingsHud::ClickRegion::SCALE_DOWN,
                SettingsHud::ClickRegion::SCALE_UP, widget, toastsOn, false);
    }
    ctx.nextLine();

    // Duration: a data-driven stepped control in a cell: register the
    // descriptor as addSteppedControl does, then stamp its index on the two
    // arrow regions the cell pushed.
    beginRow("Duration", "achievements.toast_duration");
    {
        auto sc = SettingsHud::SteppedControl::clampInt(
            ach.toastDurationMsPtr(), AchievementManager::TOAST_DURATION_STEP_MS,
            AchievementManager::MIN_TOAST_DURATION_MS, AchievementManager::MAX_TOAST_DURATION_MS,
            widget);
        const int steppedIndex = static_cast<int>(ctx.parent->m_steppedControls.size());
        ctx.parent->m_steppedControls.push_back(sc);
        snprintf(buf, sizeof(buf), "%ds", ach.getToastDurationMs() / 1000);
        const size_t first = addCell(buf, SettingsHud::ClickRegion::STEPPED_DOWN,
                                     SettingsHud::ClickRegion::STEPPED_UP, nullptr, toastsOn, false);
        for (size_t r = first; r < ctx.parent->m_clickRegions.size(); ++r) {
            ctx.parent->m_clickRegions[r].steppedIndex = steppedIndex;
        }
    }
    ctx.nextLine();

    // === ENTRY GEOMETRY, shared by the summary block and the list ===
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const AssetManager& assets = AssetManager::getInstance();
    // The tile behind a marker: the badge textures (textures/badge_N.tga, one
    // per tier and a step fancier each -- greyscale, so the metal tints them),
    // or a flat quad when a tier's texture is missing. Looked up once per
    // rebuild, not per row. A locked row takes the plainest; an earned one-shot
    // the richest, since "earned" is its top.
    int badgeSprite[Achievements::TIER_COUNT + 1] = {};
    if (useIcons) {
        for (int t = 1; t <= Achievements::TIER_COUNT; ++t) badgeSprite[t] = assets.getSpriteIndex("badge", t);
    }
    // The band's breathing room: a sliver off each end so two entries' bands
    // read as two, not one tall one.
    const float bandGap = ctx.lineHeightNormal * 0.1f;
    const float entryH = ctx.lineHeightNormal * 2.0f - 2.0f * bandGap;
    // The icon sits on a TILE in the tier's metal, square on screen and as tall
    // as the entry, and the band starts to its right: the tile says the tier at
    // a glance, the band says how far to the next. Without icons the tile is
    // gone and the band starts at labelX like every other row.
    const float tileW = useIcons ? entryH / UI_ASPECT_RATIO : 0.0f;
    const float bandX = ctx.labelX + tileW + (useIcons ? cw * 0.5f : 0.0f);
    const float textX = bandX + cw * 0.5f;

    // One two-row entry on a band: [icon] title ..... tag / task ..... numbers.
    // `tagStrong` draws the tag in the STRONG face (an earned tier); a locked
    // row's "Locked" and the summary's percentage stay in the normal face, so
    // only metal is bold. The task is cut to the room the numbers leave (a net:
    // the unit gate holds every shipped row under Achievements::TAB_ROW_CHARS,
    // so only a count grown far past Platinum ever reaches it).
    auto addEntry = [&](int sprite, int badge, unsigned long tileColor, const char* title,
                        const char* tag, unsigned long tagColor, bool tagStrong,
                        float fraction, unsigned long fillColor,
                        const char* task, const char* numbers) {
        const float top = ctx.currentY + bandGap;
        ctx.addProgressBand(bandX, top, rowRight - bandX, entryH, fraction, fillColor);
        if (sprite > 0) {
            // The glyph in the tile's own hue, lifted off it in luma the way the gap
            // bar's and the notices' captions are lifted off their slabs
            // (legibleOnFill): tone on tone, never the same tone.
            const float tileCx = ctx.labelX + tileW * 0.5f;
            const float tileCy = ctx.currentY + ctx.lineHeightNormal;
            if (badge > 0) ctx.parent->addIcon(tileCx, tileCy, badge, tileColor, entryH);
            else           ctx.addSolidQuad(ctx.labelX, top, tileW, entryH, tileColor);
            ctx.parent->addIcon(tileCx, tileCy, sprite,
                                ctx.parent->legibleOnFill(tileColor, tileColor), entryH * 0.72f);
        }
        ctx.parent->addString(title, textX, ctx.currentY, Justify::LEFT,
            Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
        ctx.parent->addString(tag, rowRight, ctx.currentY, Justify::RIGHT,
            tagStrong ? Fonts::getStrong() : Fonts::getNormal(), tagColor, ctx.fontSize);
        ctx.nextLine();
        const int numbersChars = numbers[0] ? static_cast<int>(std::strlen(numbers)) + 1 : 0;
        const int room = static_cast<int>((rowRight - textX) / cw) - numbersChars;
        char cut[64];
        if (static_cast<int>(std::strlen(task)) > room && room > 3) {
            snprintf(cut, sizeof(cut), "%.*s...", room - 3, task);
            task = cut;
        }
        ctx.parent->addString(task, textX, ctx.currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        if (numbers[0]) {
            ctx.parent->addString(numbers, rowRight, ctx.currentY, Justify::RIGHT,
                Fonts::getNormal(), colors.getSecondary(), ctx.fontSize);
        }
        ctx.nextLine();
    };

    // === PROGRESS: achievements earned, which is what a player counts (the
    // tier total is the Completionist row's business) ===
    const int earned = ach.earnedAchievements();
    const int total = ach.listedAchievements();
    const int pct = total > 0 ? (earned * 100) / total : 0;
    ctx.addSectionHeading("Progress");
    {
        char pctText[16];
        snprintf(pctText, sizeof(pctText), "%d%%", pct);
        snprintf(buf, sizeof(buf), "%d / %d", earned, total);
        // A hidden one earned counts on top of a total it is not in, so the text
        // can pass 100% while the band stays full.
        addEntry(useIcons ? assets.getIconSpriteIndex("award") : 0, badgeSprite[1], colors.getAccent(),
                 "Unlocked", pctText, colors.getSecondary(), /*tagStrong=*/false,
                 total > 0 ? std::min(1.0f, static_cast<float>(earned) / static_cast<float>(total)) : 0.0f,
                 colors.getAccent(), "Achievements earned, at any tier", buf);
    }

    // The rows this game can move, and the groups they fall in: a page per
    // group that has one. Short vectors per rebuild: the panel rebuilds on a
    // click or the 1 Hz tick, never per frame.
    // A hidden row is not a row until it is earned.
    std::vector<AchievementManager::Row> rows;
    rows.reserve(static_cast<size_t>(ach.rowCount()));
    int groupRows[static_cast<int>(Achievements::Group::COUNT)] = {};
    for (int i = 0; i < ach.rowCount(); ++i) {
        AchievementManager::Row r = ach.row(i);
        if (r.entry->hidden && r.tier == 0) continue;
        rows.push_back(r);
        groupRows[static_cast<int>(r.entry->group)]++;
    }
    // Pages: each group in chunks of ENTRIES_PER_PAGE, in group order.
    struct Page { Achievements::Group group; int first; int chunk; int chunks; };
    std::vector<Page> pages;
    for (int g = 0; g < static_cast<int>(Achievements::Group::COUNT); ++g) {
        const int chunks = (groupRows[g] + ENTRIES_PER_PAGE - 1) / ENTRIES_PER_PAGE;
        for (int c = 0; c < chunks; ++c) {
            pages.push_back({ static_cast<Achievements::Group>(g), c * ENTRIES_PER_PAGE, c + 1, chunks });
        }
    }
    const int pageCount = std::max(1, static_cast<int>(pages.size()));
    int& page = ctx.parent->m_achievementsPage;
    // A toast's click: the page holding its row, found the way a page is
    // drawn -- the row's place in its group, in chunks -- and then forgotten.
    // A row not listed (hidden, unearned) leaves the page.
    if (ctx.parent->m_achievementsJumpTo >= 0 && ctx.parent->m_achievementsJumpTo < Achievements::COUNT) {
        const Achievements::Entry& target = Achievements::kCatalogue[ctx.parent->m_achievementsJumpTo];
        std::vector<AchievementManager::Row> inGroup;
        for (const AchievementManager::Row& r : rows) {
            if (r.entry->group == target.group) inGroup.push_back(r);
        }
        // By id, not address: kCatalogue is a header constexpr, so each
        // translation unit holds its own copy and the manager's pointers are
        // not this file's.
        for (size_t i = 0; i < inGroup.size(); ++i) {
            if (std::strcmp(inGroup[i].entry->id, target.id) != 0) continue;
            const int first = static_cast<int>(i / ENTRIES_PER_PAGE) * ENTRIES_PER_PAGE;
            for (size_t p = 0; p < pages.size(); ++p) {
                if (pages[p].group == target.group && pages[p].first == first) { page = static_cast<int>(p); break; }
            }
            break;
        }
    }
    ctx.parent->m_achievementsJumpTo = -1;
    if (page >= pageCount) page = pageCount - 1;
    if (page < 0) page = 0;
    const Page current = pages.empty() ? Page{ Achievements::Group::Mileage, 0, 1, 1 }
                                       : pages[static_cast<size_t>(page)];
    ctx.parent->m_achievementsPageGroup = Achievements::groupName(current.group);
    std::vector<AchievementManager::Row> groupSorted;
    groupSorted.reserve(rows.size());
    for (const AchievementManager::Row& r : rows) {
        if (r.entry->group == current.group) groupSorted.push_back(r);
    }
    const size_t firstRow = static_cast<size_t>(current.first);
    const size_t lastRow = std::min(groupSorted.size(), firstRow + ENTRIES_PER_PAGE);

    // === THIS PAGE'S GROUP ===
    if (current.chunks > 1) {
        snprintf(buf, sizeof(buf), "%s %d/%d", Achievements::groupName(current.group), current.chunk, current.chunks);
        ctx.addSectionHeading(buf);
    } else {
        ctx.addSectionHeading(Achievements::groupName(current.group));
    }
    for (size_t i = firstRow; i < lastRow; ++i) {
        const AchievementManager::Row& r = groupSorted[i];
        const Achievements::Entry& e = *r.entry;
        const bool earnedRow = r.tier > 0;
        // A one-shot has no metal: earned in the accent, like the summary.
        const unsigned long metal = Achievements::isOneShot(e)
            ? (earnedRow ? colors.getAccent() : colors.getMuted())
            : tierColor(r.tier, colors);

        Achievements::formatProgress(e, r.value, buf, sizeof(buf));
        // The task: the next tier's sentence, or the top tier's once complete.
        char task[64];
        Achievements::formatDescription(e, std::min(r.tier + 1, e.tierCount), task, sizeof(task));
        // A complete row's band takes its metal; every other band is the accent,
        // so the column reads as one instrument with the finished ones lit.
        const int badgeTier = !earnedRow ? 1 : (Achievements::isOneShot(e) ? Achievements::TIER_COUNT : r.tier);
        addEntry(useIcons && e.icon ? assets.getIconSpriteIndex(e.icon) : 0, badgeSprite[badgeTier],
                 earnedRow ? metal : colors.getMuted(),
                 e.title, Achievements::tierLabel(e, r.tier), metal, earnedRow,
                 Achievements::progressFraction(e, r.value),
                 r.tier >= e.tierCount ? metal : colors.getAccent(), task, buf);
    }

    // The pager, at the foot of a FULL page whatever this page holds: a short
    // page (a group's last chunk) would otherwise lift it, and a button that
    // moves between pages has to be chased. Only when there is more than one.
    if (pageCount > 1) {
        const int shown = static_cast<int>(lastRow - firstRow);
        ctx.currentY += static_cast<float>(ENTRIES_PER_PAGE - shown) * ctx.lineHeightNormal * 2.0f;
        ctx.addSpacing();   // the junction above a button row, like every in-tab button
    }
    ctx.addPager(page, pageCount, SettingsHud::ClickRegion::ACHIEVEMENTS_PAGE_PREV,
                 SettingsHud::ClickRegion::ACHIEVEMENTS_PAGE_NEXT);

    // No active HUD for a global manager tab
    return nullptr;
}
