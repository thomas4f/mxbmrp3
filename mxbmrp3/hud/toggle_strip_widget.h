// ============================================================================
// hud/toggle_strip_widget.h
// Toggles widget: the plugin's on-screen controls in one strip. First (top, or
// left when horizontal), the Settings icon (opens and closes the menu). After it, every on/off switch
// in the settings sidebar -- its red/green identity icons without their labels,
// the Global section's (Rumble, Helmet, Director, ...) then the Profile
// section's -- so a feature or HUD can be switched without opening the menu.
// Shown with the cursor; hovering an icon names it, and resting on one that is
// off shows what it would put on screen, dimmed; clicking a switch is the sidebar
// checkbox's own click (SettingsHud::toggleTab).
//
// DOCKS AT AN EDGE: dragged, only the POINTER decides the shape. Within
// DOCK_CELLS of a side edge the strip turns vertical; of the top or bottom
// edge, horizontal; a corner (both) and the middle (neither) keep the shape.
// The dock decides only the shape: where it sits is the drag's, under grid
// snap and screen clamp like any HUD, so it can lie flush on the edge. On a
// turn the icon that was grabbed lands under the pointer and the drag carries
// on from there, so nothing desyncs and, as the box never decides, a turn
// cannot set off the next. Decided only while dragging, never at rest (the
// default spot is a corner), and saved per profile beside the position. One shape serves both surfaces (the companion draws the same
// geometry at its own offset), so a drag on either window turns both; only
// the dragged one moves.
//
// COLLAPSIBLE: after Settings, the dropdowns' caret (pointing back at Settings
// while open, away while folded) folds the switches away along the strip, leaving Settings and the caret -- the
// single button players knew before the strip. One choice for every profile
// ([Display] togglesCollapsed); collapsed, update() does not read the switches
// at all. Opening fades the switches in as Motion's part (core/motion.h), like
// a settings tab's body; Motion Off, and closing, are instant.
//
// It replaced the two corner buttons (settings gear, director camera), so it
// inherits their duties: it is SETTINGS CHROME to the hide-all hotkey and the
// "All HUDs hidden" notice (HudManager), and entering the track briefly reveals
// its top section -- the Settings icon, not the switches -- so the way into the
// menu can be found without moving the mouse. The camera button's job is the
// Global section's Director switch.
//
// NOT A "WIDGET" TO THE WIDGETS MASTER (HudManager::isWidgetHud): the strip
// carries that master's own switch, so it must not switch itself off with it.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "settings_hud.h"

class ToggleStripWidget : public BaseHud {
    friend class SettingsManager;

public:
    ToggleStripWidget();
    virtual ~ToggleStripWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    bool handleMouseInput(bool allowInput = true) override;
    void resetToDefaults();

    // What a click this frame landed on. Polled by HudManager (handleToggleStrip).
    enum class Click { None, Settings, Collapse, Tab };
    // The clicked control, and for Tab the tab id in *tabId.
    Click clicked(int* tabId) const;
    // The cursor is on a row: InputManager keeps it from auto-hiding meanwhile,
    // so a player reading the strip does not lose it under the pointer.
    bool isHovered() const { return m_hoverRow >= 0; }
    // HOVER PREVIEW: what the switched-off switch under the cursor would show,
    // which HudManager draws dimmed until the cursor leaves its icon
    // (setPreviewHud); null on any other row, one that is on, before the cursor
    // has rested HOVER_PREVIEW_DELAY_MS on it, or after a click on it.
    BaseHud* hoverSample() const;
    static constexpr int HOVER_PREVIEW_DELAY_MS = 250;

    // Every tab with a toggle fits: ~28 today, the headroom is for new HUDs.
    static constexpr int MAX_ITEMS = 48;
    // The controls above the switches: Settings, and the caret that folds them.
    static constexpr int MAX_CONTROLS = 2;

    // How close to an edge, in grid cells, the pointer docks the strip there.
    static constexpr int DOCK_CELLS = 5;

    bool isHorizontal() const { return m_horizontal; }
    void setHorizontal(bool horizontal) {
        if (horizontal != m_horizontal) { m_horizontal = horizontal; setDataDirty(); }
    }

    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed) {
        if (collapsed != m_collapsed) { m_collapsed = collapsed; setDataDirty(); }
    }

#if defined(MXBMRP3_TEST_BUILD)
    // Row i as last drawn (controls first, then switches): its centre on the game
    // surface, its switch (controls: false) and its name. Returns the rows drawn,
    // 0 while hidden.
    int testRow(int i, float* x, float* y, bool* on, const char** name) const {
        if (i < 0 || i >= m_rowCount) return m_rowCount;
        if (x) *x = getOffsetX() + m_rows[i].cx;
        if (y) *y = getOffsetY() + m_rows[i].cy;
        if (on) *on = m_rows[i].kind == Click::Tab && m_items[m_rows[i].item].on;
        if (name) *name = rowName(i);
        return m_rowCount;
    }
    int testHoverRow() const { return m_hoverRow; }
#endif

private:
    // Which part of the strip shows.
    enum class Show { None, Controls, All };

    struct Row {
        Click kind = Click::None;
        int item = -1;      // index into m_items for a Tab row
        float x = 0.0f, y = 0.0f;     // hit box's top-left, build space (offset not applied)
        float cx = 0.0f, cy = 0.0f;   // the icon's centre, same space
    };

    void rebuildRenderData() override;
    PanelWant stripWant(const ScaledDimensions& dim, int controls, int globals, int profiles) const;
    // Mid-drag: turn the strip if the pointer has reached an edge of the other kind.
    void dockToPointer();
    // The row under the cursor on the active surface, or -1.
    int rowAt(float cursorX, float cursorY) const;
    const char* rowName(int row) const;

    SettingsHud::TabToggle m_items[MAX_ITEMS] = {};
    int m_itemCount = 0;
    // What the last rebuild drew, so update() rebuilds only when one changes.
    uint64_t m_onBits = 0;
    SettingsHud::DirectorState m_directorState = SettingsHud::DirectorState::Plain;
    int m_hoverRow = -1;
    std::chrono::steady_clock::time_point m_hoverSince;   // when m_hoverRow last changed
    bool m_clickedHover = false;   // clicked since the cursor came to m_hoverRow: no preview
    Show m_show = Show::None;
    bool m_settingsOpen = false; // the Settings icon reads "close"
    // The switches folded away. Global ([Display] togglesCollapsed), so
    // resetToDefaults() leaves it to the global reset.
    bool m_collapsed = false;
    // Lies along the top or bottom edge. Per profile, saved with the position.
    bool m_horizontal = false;
    int m_dragRow = 0;   // the row the running drag grabbed, kept under the pointer on a turn
    bool m_dragMoved = false;            // the running drag has left the press point
    float m_pressX = 0.0f, m_pressY = 0.0f;
    // Row geometry from the last rebuild, in build space (offset not applied).
    Row m_rows[MAX_CONTROLS + MAX_ITEMS] = {};
    // Each row's icon sprite and the name it was looked up for (static storage),
    // under the AssetManager::themeGeneration it was read in, like
    // DashHud::m_iconSprite: the lookup builds a string, and every hover change
    // rebuilds the strip.
    const char* m_iconName[MAX_CONTROLS + MAX_ITEMS] = {};
    int m_iconSprite[MAX_CONTROLS + MAX_ITEMS] = {};
    unsigned int m_iconSpriteGen = 0xFFFFFFFFu;
    int m_rowCount = 0;
    float m_hitW = 0.0f, m_hitH = 0.0f;   // one row's hit box
    float m_stripW = 0.0f, m_stripH = 0.0f;
};
