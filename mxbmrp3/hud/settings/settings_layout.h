// ============================================================================
// hud/settings/settings_layout.h
// Shared layout context and helper methods for settings panel rendering
// ============================================================================
#pragma once

#include "../base_hud.h"
#include "../settings_hud.h"
#include "../../core/color_config.h"
#include "../../core/font_config.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include <string>

// Forward declarations
class SettingsHud;
class BaseHud;

// Layout context for settings panel rendering
// An explicit context object rather than lambda captures, so tab rendering lives in
// separate files while keeping access to shared state.
// Width of a `< value >` control's VALUE field, in characters. One definition so a
// hand-rolled row (the Appearance tab's colour and font rows, which need click
// targets the helpers do not carry) lines its arrows up with every helper-built row
// instead of picking its own number.
constexpr int STANDARD_VALUE_WIDTH = 10;

struct SettingsLayoutContext {
    // Parent reference for adding render primitives
    SettingsHud* parent;

    // The panel's layout vocabulary, resolved through the parent's active theme.
    // Not mirrored into a field here: the context is rebuilt per frame and already
    // forwards to `parent` for every render primitive, so forwarding this too keeps
    // one source rather than a copy that can go stale mid-rebuild.
    const LayoutMetrics& layout() const;

    // Dimensions (from getScaledDimensions())
    float fontSize;
    float fontSizeLarge;
    float lineHeightNormal;
    float lineHeightLarge;
    // The vertical snap cell at this panel's scale -- half a row. Every gap the
    // layout file states is in cells, so this is what spends them.
    float cellH;
    float paddingH;
    float paddingV;

    // Layout positions
    float labelX;              // Where labels start (left column)
    float controlX;            // Where control values start (toggle position)
    float rightColumnX;        // Where right column starts (for data toggles)
    float contentAreaStartX;   // Start of content area (after tab bar)
    float panelWidth;          // Content area width (from contentAreaStartX to right edge)
    // The panel's own inner right boundary -- where the frame's edge slice begins, and
    // the line the title band's edge slice stops on. NOT derivable from the two above:
    // the content COLUMN deliberately stops short of it (it is symmetric with the tab
    // trough on the left, and theme-invariant -- theme_geometry_test asserts both), so
    // a card that overhangs its column has to be clamped against the panel instead.
    float panelContentRightX;

    // Mutable cursor
    float currentY;

    // Scale factor
    float scale;

    // Tab ID for tooltip display (set by addTabTooltip)
    std::string currentTabId;
    float tooltipY;  // Y position where tooltip should be rendered

    // Constructor
    SettingsLayoutContext(
        SettingsHud* _parent,
        const BaseHud::ScaledDimensions& dim,
        float _labelX,
        float _controlX,
        float _rightColumnX,
        float _contentAreaStartX,
        float _panelWidth,
        float _panelContentRightX,
        float _currentY
    );

    // === Layout Helper Methods ===

    // Add a section header (bold, primary color)
    // Opens a themed card and draws the section heading. OWNS the gap above itself
    // -- never precede a call with addSpacing().
    //
    // `hint` is the muted parenthetical some sections carry ("(click to track/untrack)").
    // It lives here rather than in the callers so a hand-rolled heading has no reason
    // to exist: a heading drawn with addString gets no card, and that tab is then the
    // one tab without section cards.
    // Returns the Y the heading row was drawn at, for the rare caller that needs to
    // put something else on that row (the Hotkeys tab's column labels). Deriving it
    // as currentY - lineHeightNormal works today and breaks silently the moment this
    // function's advance changes.
    //
    // HEADING, not "header": that word does duty for a settings block, a HUD section
    // and a table's column-label row. A HUD opens its content blocks with the SAME
    // thing under the SAME name (BaseHud::addSectionHeading) -- see there for why the
    // two are one concept and the face is the only difference.
    // Width of a full-width row -- a hover highlight, a click region -- inside the
    // content column's section card.
    //
    // THEMED, the row is symmetric inside its CARD: it starts one label-column inset
    // from the card's left edge and must end the same distance from its right, so the
    // inset comes off TWICE. Off once, the row's right edge lands exactly ON the
    // card's border -- a clean gap on the left and a spill through the card on the
    // right.
    //
    // UNTHEMED there is no card, and taking it off twice stops the row two cells
    // short of everything else. The panel's margin is [panel] padding-x, and unthemed
    // every other element honours exactly that: the tab column's highlight, the Reset
    // button, the right-aligned version string. A row highlight at double it makes
    // the content column look like it ends early while the tab column looks glued to
    // the edge. The row's LEFT edge is the label text either way -- the label column
    // is an indent, not a margin, and only the card turns it into one.
    float rowSpanWidth() const {
        const float inset = labelX - contentAreaStartX;
        return panelWidth - (parent->hasThemedCard() ? 2.0f * inset : inset);
    }

    // THE OPEN SECTION CARD'S LEFT EDGE, handed over by the panel from the engine's
    // own box for this column -- not derived here from the column and the card's
    // terms, which would be a second derivation that has to stay equal to the one
    // the card is drawn with. No right edge: the hover band that fills the card's
    // INTERIOR is PanelPlan::rowBandX/W, one owner for every row highlight in the
    // plugin.
    float planCardLeftX = 0.0f;

    float addSectionHeading(const char* title, const char* hint = nullptr);

    // A muted tip INSIDE the section it follows ("Tip: click to rebind, ESC to
    // cancel."), half a row below the section's last control. Inside on
    // purpose: rows outside a section are invisible to the box engine, which
    // reserves per section — a card-free block below the last card draws on
    // unreserved air (the tab overflows the panel by exactly its tip;
    // settings_fit_test measures it), and reserving that tail separately prices
    // the tallest tabs past the screen at the themed frames.
    void addNote(const char* text);

    // A RADIO row's shared half: the row-wide tooltip region, a click region
    // `clickChars` wide (the glyph plus the label it reads with), and the
    // "(O)"/"( )" glyph itself. Returns the x where the caller's label starts.
    //
    // The label stays the CALLER'S, because that is the only part that differs:
    // the General tab's two rows are one run of text and three (it colours the
    // active profile's name apart from the words around it). The caller ends the
    // row with nextLine().
    float addRadioRow(bool selected, SettingsHud::ClickRegion::Type type,
                      int clickChars, const char* tooltipId);

    // A muted caption INSIDE the open section card, under the control it explains
    // (the General tab's web-server hint). Same voice as addNote -- muted, 0.9x --
    // but it belongs to its section rather than ending it, so no card is closed.
    // The junction above it is addSpacing()'s [panel] gap, not a hardcoded
    // fraction of a row that no ini could reach.
    void addInlineNote(const char* text);

    // A plain TEXT ROW inside the content column: one line at labelX, advanced by
    // one row. Everything a tab SAYS rather than asks -- update status lines,
    // release-note lines, the error text under a failed download. The colour is
    // the caller's, because that is the only thing that varies between them.
    void addTextRow(const char* text, unsigned long color);

    // A LABEL + VALUE row, the value starting `valueColumn` characters right of
    // labelX: "Current:   v1.2.3", "Available: 1.3.0 (PRE)", a download step and
    // its "OK". The column is a parameter because the Updates tab aligns its
    // version pair at 11 and its progress pair at 10; -1 (the default) puts the
    // value in the CONTROL column, where the Stats tab's read-only rows line up
    // with every other tab's cyclers.
    void addLabelValueRow(const char* label, unsigned long labelColor,
                          const char* value, unsigned long valueColor,
                          int valueColumn = -1);

    // Background for a button drawn INSIDE a tab's content: takes the theme's button
    // slices when there are any, a solid quad otherwise, and fills its row. `color`
    // is the caller's state colour -- disabled/hover/normal are the caller's
    // decision, not this helper's. Every in-tab button goes through here so none can
    // be left unthemed.
    // Geometry for a centred in-card ACTION BUTTON row, from the [button]
    // terms: the box wraps the label (insets each side, full box height), and
    // labelY sits below the top inset so the glyph stays row-centred in the
    // box's content row. One helper so the five action buttons (Copy, Reset,
    // Check Now, Retry, Install) cannot each miss a term their own way.
    //
    // THE MARGIN IS PART OF THE ANSWER: `.y` already has the top margin in it
    // and `advance` is the whole vertical cost, so a caller adds `advance` to
    // currentY and nothing else. With `bt.marginT/B` unread, `[button] margin`
    // would move the settings FOOTER and do nothing at all to Copy, Reset,
    // Check Now, Retry or Install.
    //
    // The JUNCTION above the button stays the caller's addSpacing() — [panel]
    // gap, the same term every other stacked thing in a tab uses. The split is
    // the box model's own: a junction belongs to the stack, a margin belongs to
    // the box.
    struct ButtonRowGeom { float x, y, w, h, centerX, labelY, advance; };
    ButtonRowGeom buttonRow(int labelChars) const;
    void addButtonBackground(float x, float y, float width, float height, unsigned long color);

    // What pressing an in-tab action button DOES, said in hue -- so the two ends
    // of "safe to press" are told apart without reading the label:
    //   Accent   -- the neutral action (Copy, Retry, Install)
    //   Positive -- the confirming action of its tab (Check Now)
    //   Negative -- throws something away and cannot be undone (Reset)
    enum class ButtonRole { Accent, Positive, Negative };

    // A centred in-card ACTION BUTTON, whole: click region, themed background,
    // centred label, row advance. `labelChars` is the WIDEST label the button
    // ever shows (Check Now becomes "Checking...", so 11), so it does not resize
    // under the cursor.
    //
    // One spelling for all five, so they cannot drift (one dimming to 0.3 alpha
    // where another uses 64/255); why the glyph colour is DERIVED is explained at
    // the implementation.
    void addActionButton(const char* label, int labelChars,
                         SettingsHud::ClickRegion::Type type,
                         ButtonRole role = ButtonRole::Accent,
                         bool enabled = true);

    // TWO buttons side by side on one row, centred as a pair with the [button] gap
    // between them -- for a choice between two acts rather than one act with a
    // preceding selection (the Reset section: one row for two outcomes).
    //
    // Both take the same width so neither reads as the default, which for a pair
    // where one is destructive is the point.
    // A "label: url" row where only the URL is clickable and lights on hover.
    //
    // ONE OWNER for that styling and hit-testing, because there are two callers
    // in different sections -- the Help & Community footer and the web server's own
    // "live overlay at ..." line, which is a link the moment the server is actually
    // serving. A second hand-rolled copy is how the two would drift into looking like
    // different kinds of thing.
    //
    // `prefix` is drawn muted at labelX and `url` in the accent colour after it;
    // prefixChars fixes the column so a group of rows aligns.
    void addLinkRow(const char* prefix, const char* url, int prefixChars,
                    SettingsHud::ClickRegion::Type type, float fontScale = 0.9f);

    void addActionButtonPair(const char* labelA, SettingsHud::ClickRegion::Type typeA,
                             ButtonRole roleA, bool enabledA,
                             const char* labelB, SettingsHud::ClickRegion::Type typeB,
                             ButtonRole roleB, bool enabledB,
                             int labelChars);
    // The [button] terms, resolved once at construction (planButtonTerms).
    BaseHud::PlanButtonTerms bt{};
    // Column (in characters, from labelX) where a section heading's muted hint starts.
    // 16 = the widest heading that carries one ("Tracked Riders") plus a gap.
    static constexpr int SECTION_HINT_COLUMN = 16;
    // Close the final section's card; earlier ones close at the next header.
    void finishSections();

    // Add tab tooltip area (string sourced from TooltipManager)
    // tabId is the lowercase tab name (e.g., "standings", "map")
    void addTabTooltip(const char* tabId);

    // Add a cycle control with < value > pattern
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    // tooltipId is optional - if provided, a row-wide hover region is created
    // valueColor - 0 derives the value colour from enabled/isOff (the common
    //   case); pass a colour for a control whose value carries a THIRD state the
    //   primary/muted pair can't say, e.g. a live connection reading green
    //   (Controller / Steam / Analytics). Arrows and label are unaffected.
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        unsigned long valueColor = 0
    );

    // Add a cycle control whose arrows carry a TYPED TARGET the click handler
    // reads back out of ClickRegion::TargetPointer -- a FontCategory, a
    // ColorSlot, a HotkeyAction. Same row as the plain overload, emitted through
    // it and stamped afterwards (the shape the CycleControl/SteppedControl
    // overloads already use), so a payload-carrying row cannot pick its own
    // column geometry -- a hand-rolled "< value >" run gets its own value width
    // and skips formatValue.
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        const SettingsHud::ClickRegion::TargetPointer& payload,
        const char* tooltipId = nullptr
    );

    // Add a cycle control whose arrows step a mod-N state through the shared
    // data-driven cycle handler (ClickRegion::CYCLE_UP/CYCLE_DOWN + a
    // CycleControl descriptor registered for this rebuild). Use this instead of
    // a dedicated enum pair when the handler would be the plain archetype
    // "value = (value ± 1) mod N; hud->setDataDirty(); setDataDirty();"
    // (optionally with uniform postStep work). Cycles never hold-accelerate.
    // tooltipOnArrows mirrors addSteppedControl: also stamp tooltipId onto the
    // two arrow regions; pass false for controls whose arrows show no tooltip.
    void addCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        const SettingsHud::CycleControl& control,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        bool tooltipOnArrows = true
    );

    // Add a cycle control whose arrows step a numeric HUD member through the
    // shared data-driven stepped handler (ClickRegion::STEPPED_UP/STEPPED_DOWN +
    // a SteppedControl descriptor registered for this rebuild). Use this instead
    // of a dedicated enum pair when the handler would be the plain archetype
    // "value = applyAccelerated*(...); hud->setDataDirty(); setDataDirty();".
    // Handlers with any other side effect keep their own enum pair.
    // tooltipOnArrows: also stamp tooltipId onto the two arrow regions; pass
    // false for controls whose arrows show no tooltip.
    void addSteppedControl(
        const char* label,
        const char* value,
        int valueWidth,
        const SettingsHud::SteppedControl& control,
        BaseHud* targetHud,
        bool enabled = true,
        bool isOff = false,
        const char* tooltipId = nullptr,
        bool tooltipOnArrows = true
    );

    // Add a toggle control with < On/Off > pattern
    // Both arrows trigger the same toggle action
    // tooltipId is optional - if provided, a row-wide hover region is created
    // valueOverride - if provided, shows this text instead of "On"/"Off"
    void addToggleControl(
        const char* label,
        bool isOn,
        SettingsHud::ClickRegion::Type toggleType,
        BaseHud* targetHud,
        uint32_t* bitfield = nullptr,
        uint32_t flag = 0,
        bool enabled = true,
        const char* tooltipId = nullptr,
        const char* valueOverride = nullptr
    );

    // Overload for bool* toggle controls (e.g., LAP_LOG_GAP_ROW_TOGGLE)
    void addToggleControl(
        const char* label,
        bool isOn,
        SettingsHud::ClickRegion::Type toggleType,
        BaseHud* targetHud,
        bool* boolPtr,
        bool enabled = true,
        const char* tooltipId = nullptr,
        const char* valueOverride = nullptr
    );

    // Per-HUD panel-theme row (Default / None / <installed themes>). Shown in place
    // of the Texture row for HUDs that have no background texture variants.
    void addPerHudThemeControl(BaseHud* hud);
    // Texture row for a pack HUD (gamepad / pit board). See the definition.
    void addPackControl(BaseHud* hud);

    // Add standard HUD controls block (Visible, Title, Texture|Theme, Opacity, Scale)
    // Returns the Y position where the section started (for right column alignment).
    // Whether the Title row appears comes from BaseHud::m_titleSupported, not from an
    // argument here -- a bool at the call site can disagree with the HUD it describes.
    float addStandardHudControls(BaseHud* hud);

    // Add a data toggle control in the right column (for bitfield toggles)
    // labelWidth should accommodate the longest label in the group for alignment
    void addDataToggle(
        const char* label,
        uint32_t* bitfield,
        uint32_t flag,
        bool isRequired,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12
    );

    // Add a group toggle control in the right column (toggles multiple bits)
    void addGroupToggle(
        const char* label,
        uint32_t* bitfield,
        uint32_t groupFlags,
        bool isRequired,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12
    );

    // Add a cycle control in the right column (label + < value > on same row)
    // Used for Rows, Show mode, etc. in the right column area
    // Returns the Y position after this control
    float addRightColumnCycleControl(
        const char* label,
        const char* value,
        int valueWidth,
        SettingsHud::ClickRegion::Type downType,
        SettingsHud::ClickRegion::Type upType,
        BaseHud* targetHud,
        float yPos,
        int labelWidth = 12,
        bool enabled = true,
        bool isOff = false
    );

    // Advance cursor by one line
    void nextLine();

    // One junction's worth of vertical air: [Advanced] panelGap, the same term
    // every seam between two stacked children spends. NO multiplier -- one
    // distance, one knob. Never before addSectionHeading(), which owns its own
    // gap (check_section_spacing.sh).
    void addSpacing();

    // Helper to format and truncate values for cycle controls
    // If value exceeds maxWidth, truncates to maxWidth-1 chars + ellipsis
    // If center is true, centers the value within maxWidth
    static std::string formatValue(const char* value, int maxWidth, bool center = false);

    // Add a widget row for the Widgets tab table
    // The enable* parameters follow the visual column order:
    //   Name | Visible | Title | Texture | Opacity | Scale
    // tooltipId is optional - if provided, a row-wide hover region is created
    // No enableTitle: the Title column reads BaseHud::m_titleSupported, so a widget's
    // row and the widget itself cannot disagree about whether it has a caption.
    void addWidgetRow(
        const char* name,
        BaseHud* hud,
        bool enableVisibility = true,
        bool enableBgTexture = true,
        bool enableOpacity = true,
        bool enableScale = true,
        const char* tooltipId = nullptr,
        // Pointer row only: the visibility column can't toggle the widget's real
        // m_bVisible (the pointer must stay drawable so it can still appear in the
        // settings menu), so it toggles the menu-only-cursor mode instead. On = the
        // pointer is summoned by mouse movement during play; Off = menu-only.
        bool menuOnlyPointerRow = false
    );

    // EVERY SECTION THIS PASS WALKED, as content heights in normalized units and in
    // order -- the list a caller hands to PanelWant::ColumnWant::sectionH so the box
    // engine can lay the column out. Filled by closeSectionCard whether or not a
    // card was drawn, so it describes the LAYOUT rather than the paint.
    std::vector<float> measuredSections;

    // WHERE THE ENGINE PUT EACH SECTION, when this pass is drawing rather than
    // measuring: one content-origin y per section, in the same order
    // measuredSections reports them. addSectionHeading jumps to the next one
    // instead of computing a seam, so the air between two section cards is the
    // engine's -- the same seam it puts between any two siblings.
    //
    // EMPTY MEANS MEASURING. A measuring pass has no plan to place anything from
    // (the plan is what it exists to feed), so it falls back to laying the
    // sections end to end with the seam spelled out. The two agree by
    // construction: the seam it spells is the one the engine spends, and a tab
    // whose measured height differed from its drawn one is exactly what
    // settings_fit_test's overflow number reports.
    std::vector<float> planSectionY;
    size_t nextSection = 0;

private:
    void openSectionCard();
    void closeSectionCard();
    int m_sectionCardIndex = -1;
    float m_sectionTop = 0.0f;
    bool m_sectionOpen = false;
    // False until the first section opens, so no gap is inserted above it.
    bool m_hadSection = false;
    // True while the previous emission was a note, so a run of notes reads as one
    // paragraph rather than gaining a gap per line.
    bool m_lastWasNote = false;

    // Helper to calculate character width at current scale
    float charWidth() const;
};

// Utility function for icon/shape display names
// Gets the display name for an icon shape index (0 = Off, 1-N = icon names)
std::string getShapeDisplayName(int shapeIndex, int maxWidth = 12);

// Note: Tab rendering functions are declared as static members of SettingsHud
// to inherit the friend relationships with HUD classes. See settings_hud.h.

