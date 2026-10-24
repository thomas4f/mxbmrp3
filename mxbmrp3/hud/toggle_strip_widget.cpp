// ============================================================================
// hud/toggle_strip_widget.cpp
// Toggles widget - see toggle_strip_widget.h
// ============================================================================
#include "toggle_strip_widget.h"
#include "../core/layout_config.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include <chrono>
#include <cstring>

using namespace PluginConstants;

namespace {
// Default spot: the right edge on 180 cells (0.990, just inside the corner --
// the largest whole cell that keeps a box on the 181.8-cell-wide screen) and
// one cell down, where the settings gear sat before the strip replaced it.
constexpr int RIGHT_CELLS = 180;
constexpr int TOP_CELLS = 1;
// The fold caret: the dropdowns' sprite, turned (unturned it points up), as a
// half-size share of the strip's icon size: 0.5 fills the same box as the
// icons around it.
constexpr const char* CARET = "caret-up";
constexpr float CARET_SCALE = 0.5f;
}  // namespace

ToggleStripWidget::ToggleStripWidget()
{
    // No caption: a strip of icons.
    disableTitle();
    m_panelKind = PanelKind::Widget;
    setDraggable(true);
    // The icons, plus a theme's frame and three section cards (nine-slices each)
    // and the fill, so a themed strip never grows the list.
    m_quads.reserve(MAX_CONTROLS + MAX_ITEMS + 37);
    m_strings.reserve(1 + MAX_CONTROLS + MAX_ITEMS);  // the hover name; text stand-ins with UI icons off

    resetToDefaults();
    rebuildRenderData();
}

bool ToggleStripWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // Reads switch states, not game data: update() watches them itself.
    return false;
}

void ToggleStripWidget::update() {
    // Rebuilt only when what it draws changes: shown/hidden with the cursor, the
    // hovered row, the menu opening, a switch flipped anywhere (sidebar, hotkey,
    // this strip) or the Director's live state. Switched off, or with the cursor
    // hidden and no reveal running, this is a few bool tests.
    const InputManager& input = InputManager::getInstance();
    Show show = Show::None;
    if (isVisibleAnySurface()) {
        if (input.shouldShowCursor()) show = Show::All;
        else if (isRevealed()) show = Show::Controls;
    }
    bool settingsOpen = false;
    int count = 0;
    uint64_t bits = 0;
    int hover = -1;
    SettingsHud::DirectorState director = SettingsHud::DirectorState::Plain;
    if (show != Show::None) settingsOpen = HudManager::getInstance().isSettingsVisible();
    if (show == Show::All && !m_collapsed) {
        count = HudManager::getInstance().getSettingsHud().tabToggles(m_items, MAX_ITEMS);
        for (int i = 0; i < count; ++i) {
            if (m_items[i].on) bits |= (uint64_t{1} << i);
        }
        director = SettingsHud::directorState();
    }
    if (show == Show::All) {
        const CursorPosition& cursor = input.getCursorPosition();
        if (cursor.isValid) hover = rowAt(cursor.x, cursor.y);
    }
    // A click on an icon ends its preview until the cursor leaves it, so a HUD
    // switched off from here looks off rather than faint (hoverSample).
    if (hover != m_hoverRow) m_clickedHover = false;
    if (hover >= 0 && input.getLeftButton().isClicked()) m_clickedHover = true;
    if (show != m_show || settingsOpen != m_settingsOpen || count != m_itemCount ||
        bits != m_onBits || hover != m_hoverRow || director != m_directorState) {
        m_show = show;
        m_settingsOpen = settingsOpen;
        m_itemCount = count;
        m_onBits = bits;
        if (hover != m_hoverRow) m_hoverSince = std::chrono::steady_clock::now();
        m_hoverRow = hover;
        m_directorState = director;
        setDataDirty();
    }
    processDirtyFlags();
}

int ToggleStripWidget::rowAt(float cursorX, float cursorY) const {
    if (!isVisibleOnActiveSurface() || !isPointInActiveBounds(cursorX, cursorY)) return -1;
    float x = cursorX, y = cursorY;
    mapCursorToHudSpace(x, y);
    const float local = y - getOffsetY();
    for (int i = 0; i < m_rowCount; ++i) {
        if (local >= m_rows[i].y && local < m_rows[i].y + m_rowH) return i;
    }
    return -1;   // a gap between the sections
}

const char* ToggleStripWidget::rowName(int row) const {
    switch (m_rows[row].kind) {
        case Click::Settings: return "Settings";
        case Click::Collapse: return m_collapsed ? "Expand" : "Collapse";
        case Click::Tab: {
            const SettingsHud::TabToggle& item = m_items[m_rows[row].item];
            const char* state = item.tabId == SettingsHud::TAB_DIRECTOR
                ? SettingsHud::directorStateName(m_directorState) : nullptr;
            return state ? state : item.name;
        }
        case Click::None:     break;
    }
    return "";
}

BaseHud* ToggleStripWidget::hoverSample() const {
    if (m_show != Show::All || m_hoverRow < 0 || m_hoverRow >= m_rowCount) return nullptr;
    const Row& row = m_rows[m_hoverRow];
    if (row.kind != Click::Tab) return nullptr;
    const SettingsHud::TabToggle& item = m_items[row.item];
    if (item.on || !item.shows || m_clickedHover) return nullptr;
    // A pause on the icon, not a pass over it: running the cursor down the
    // strip to an icon further down would otherwise flash every HUD on the way.
    if (std::chrono::steady_clock::now() - m_hoverSince < std::chrono::milliseconds(HOVER_PREVIEW_DELAY_MS))
        return nullptr;
    return item.shows;
}

ToggleStripWidget::Click ToggleStripWidget::clicked(int* tabId) const {
    // A click needs the cursor, so the revealed Settings icon alone never takes one.
    if (m_show != Show::All || isHeldBack()) return Click::None;
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor() || !input.getLeftButton().isClicked()) return Click::None;
    const CursorPosition& cursor = input.getCursorPosition();
    if (!cursor.isValid) return Click::None;
    const int row = rowAt(cursor.x, cursor.y);
    if (row < 0) return Click::None;
    if (m_rows[row].kind == Click::Tab && tabId) *tabId = m_items[m_rows[row].item].tabId;
    return m_rows[row].kind;
}

PanelWant ToggleStripWidget::stripWant(const ScaledDimensions& dim, int controls, int globals, int profiles) const {
    // SLIM LIKE THE BARS WIDGET: the content is just the icon column, and the
    // plan adds the standard padding (and a theme's frame and card) around it.
    // Settings, the Global switches and the Profile switches are sections, so the
    // gaps between them are the panel's own section gap -- the sidebar's split.
    PanelWant want;
    want.contentW = dim.fontSize * SettingsHud::TAB_ICON_SCALE / UI_ASPECT_RATIO;
    want.sectionH = { dim.lineHeightNormal * static_cast<float>(controls) };
    if (globals > 0) want.sectionH.push_back(dim.lineHeightNormal * static_cast<float>(globals));
    if (profiles > 0) want.sectionH.push_back(dim.lineHeightNormal * static_cast<float>(profiles));
    return want;
}

void ToggleStripWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();
    m_rowCount = 0;
    m_motionPartQuadFirst = m_motionPartQuadEnd = -1;
    m_motionPartStringFirst = m_motionPartStringEnd = -1;

    if (m_show == Show::None) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();
    const float rowH = dim.lineHeightNormal;
    // The reveal shows Settings alone; the cursor adds the caret under it, and
    // the switches unless they are folded away (m_itemCount is 0 then).
    const int controls = m_show == Show::All ? MAX_CONTROLS : 1;
    const int switches = m_show == Show::All ? m_itemCount : 0;
    int globals = 0;
    while (globals < switches && m_items[globals].global) ++globals;   // Global first, in order
    const int profiles = switches - globals;
    PanelPlan& p = planPanel(dim, stripWant(dim, controls, globals, profiles));
    const float w = p.width();
    const float h = p.height();
    addPlanBackground(p, 0.0f, 0.0f);

    m_rowH = rowH;
    m_stripW = w;
    m_rows[m_rowCount++] = { Click::Settings, -1, p.contentY(0) };
    if (controls > 1) m_rows[m_rowCount++] = { Click::Collapse, -1, p.contentY(0) + rowH };
    for (int i = 0; i < switches; ++i) {
        const bool global = i < globals;
        const size_t section = global ? 1 : (globals > 0 ? 2 : 1);
        const int k = global ? i : i - globals;
        m_rows[m_rowCount++] = { Click::Tab, i, p.contentY(section) + rowH * static_cast<float>(k) };
    }

    const float iconX = p.sectionBoxCenterX();
    // The sidebar's icon size, so the strip's icons are the same glyphs at the
    // same size as the ones it mirrors.
    const float iconSize = dim.fontSize * SettingsHud::TAB_ICON_SCALE;
    const bool icons = UiConfig::getInstance().getTitleIcons();
    AssetManager& assets = AssetManager::getInstance();
    if (m_iconSpriteGen != assets.themeGeneration()) {
        m_iconSpriteGen = assets.themeGeneration();
        for (const char*& name : m_iconName) name = nullptr;
    }
    for (int r = 0; r < m_rowCount; ++r) {
        const Row& row = m_rows[r];
        const bool hovered = r == m_hoverRow;
        if (row.kind == Click::Tab && m_rows[r - 1].kind != Click::Tab) {
            // The switches, marked for Motion: opening the strip fades them in
            // over the panel. Keyed on the fold, so a hover or a flipped switch
            // does not replay it; folded, the range is empty.
            m_motionPartQuadFirst = static_cast<int>(m_quads.size());
            m_motionPartStringFirst = static_cast<int>(m_strings.size());
        }
        if (row.kind == Click::Collapse) {
            // The dropdowns' caret, in the Settings icon's accent: down while
            // folded (more below), up while open; "v" / "^" with UI icons off.
            unsigned long color = getColor(ColorSlot::ACCENT);
            if (hovered) color = PluginUtils::lightenColor(color, 0.25f);
            if (icons && m_iconName[r] != CARET) {
                m_iconName[r] = CARET;
                m_iconSprite[r] = assets.getIconSpriteIndex(CARET);
            }
            if (icons && m_iconSprite[r] > 0) {
                addRotatedSpriteQuad(iconX, row.y + rowH * 0.5f, iconSize * CARET_SCALE,
                                     m_collapsed ? -1.0f : 1.0f, 0.0f, m_iconSprite[r], color);
                shadowLastIcon();
            } else if (!icons) {
                addString(m_collapsed ? "v" : "^", iconX, row.y, Justify::CENTER,
                          getFont(FontCategory::NORMAL), color, dim.fontSize);
            }
            continue;   // an icons folder without the caret gets the broken-install warning
        }
        const char* icon = nullptr;
        const char* text = "";
        char abbr[3] = {};
        unsigned long color = 0;
        if (row.kind == Click::Settings) {
            // The menu's own button, in the accent like the old settings chip.
            icon = m_settingsOpen ? "hud-close" : "hud-menu";
            text = m_settingsOpen ? "x" : "=";
            color = getColor(ColorSlot::ACCENT);
            if (hovered) color = PluginUtils::lightenColor(color, 0.25f);
        } else {
            const SettingsHud::TabToggle& item = m_items[row.item];
            icon = item.icon;
            // UI icons off: the name's first two letters, since a bare "[x]" with
            // no label beside it would not say which switch it is.
            abbr[0] = item.name[0];
            abbr[1] = item.name[0] ? item.name[1] : '\0';
            text = abbr;
            color = SettingsHud::tabToggleColor(item.tabId, item.on, hovered, false);
        }
        // UI icons off, or an icons folder without this one (which also gets the
        // broken-install warning, HudManager::buildInstallWarning): the text.
        if (icons && m_iconName[r] != icon) {
            m_iconName[r] = icon;
            m_iconSprite[r] = (icon && icon[0]) ? assets.getIconSpriteIndex(icon) : 0;
        }
        if (icons && m_iconSprite[r] > 0) {
            addIcon(iconX, row.y + rowH * 0.5f, m_iconSprite[r], color, iconSize);
            shadowLastIcon();   // bare icons over the scene, like the letters and the hover name
        } else {
            addString(text, iconX, row.y, Justify::CENTER, getFont(FontCategory::NORMAL), color, dim.fontSize);
        }
    }

    m_motionPartQuadEnd = static_cast<int>(m_quads.size());
    m_motionPartStringEnd = static_cast<int>(m_strings.size());
    if (m_motionPartQuadFirst < 0) m_motionPartQuadFirst = m_motionPartQuadEnd;
    if (m_motionPartStringFirst < 0) m_motionPartStringFirst = m_motionPartStringEnd;
    setMotionPartKey(m_collapsed ? 1 : 0);

    // THE HOVERED ICON'S NAME, right-aligned one cell left of the strip: the strip
    // has no labels, and without one Session and Stats are a guess. Outside the
    // bounds, so it is never a drag or click target. Bare text like every other
    // HUD's, relying on the drop shadow over a busy scene. A strip dragged to the
    // screen's left edge has no room there, so the name flips to its right side,
    // left-aligned. Decided here, so only on a hover change or a move.
    if (m_hoverRow >= 0 && m_hoverRow < m_rowCount) {
        const char* name = rowName(m_hoverRow);
        const float textW = PluginUtils::calculateMonospaceTextWidth(
            static_cast<int>(std::strlen(name)), dim.fontSize);
        const float screenLeft = InputManager::getInstance().getWindowBounds().left;   // < 0 on ultrawide
        const bool left = getOffsetX() - dim.cellW - textW >= screenLeft;
        addString(name, left ? -dim.cellW : w + dim.cellW, m_rows[m_hoverRow].y,
                  left ? Justify::RIGHT : Justify::LEFT,
                  getFont(FontCategory::NORMAL), getColor(ColorSlot::PRIMARY), dim.fontSize);
    }

    setBounds(0.0f, 0.0f, w, h);
}

void ToggleStripWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 1.0f;
    setScale(1.0f);
    // Right edge on RIGHT_CELLS. The width is the box rebuildRenderData() fits,
    // in whole cells, and does not depend on the row count.
    const ScaledDimensions dim = getScaledDimensions();
    const float w = planPanel(dim, stripWant(dim, 1, 0, 0)).width();
    setPosition(cellsX(static_cast<float>(RIGHT_CELLS)) - w, cellsY(static_cast<float>(TOP_CELLS)));
    setDataDirty();
}
