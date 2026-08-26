// ============================================================================
// hud/settings/settings_layout.cpp
// Implementation of shared layout context and helper methods
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/color_config.h"

#include "../../core/font_config.h"
#include "../../core/ui_config.h"
#include "../../core/asset_manager.h"
#include "../../core/input_manager.h"
#include "../gamepad_widget.h"   // the Gamepad row's pack cycle reads activePack()
#include "../pitboard_hud.h"     // ...and the Pitboard row's

// Defined here rather than in the header: SettingsHud is only forward-declared
// there (the header is included BY settings_hud.h), so the parent's layout() is
// not visible until this TU has the full definition.
const LayoutMetrics& SettingsLayoutContext::layout() const {
    return parent->layout();
}

using namespace PluginConstants;


// Standard width (in characters) of a control's value field. Value strings longer
// than this are truncated by formatValue(), so keep cycle/toggle value text within it.


SettingsLayoutContext::SettingsLayoutContext(
    SettingsHud* _parent,
    const BaseHud::ScaledDimensions& dim,
    float _labelX,
    float _controlX,
    float _rightColumnX,
    float _contentAreaStartX,
    float _panelWidth,
    float _panelContentRightX,
    float _currentY
)
    : parent(_parent)
    , fontSize(dim.fontSize)
    , fontSizeLarge(dim.fontSizeLarge)
    , lineHeightNormal(dim.lineHeightNormal)
    , lineHeightLarge(dim.lineHeightLarge)
    , cellH(dim.cellH)
    , paddingH(dim.paddingH)
    , paddingV(dim.paddingV)
    , labelX(_labelX)
    , controlX(_controlX)
    , rightColumnX(_rightColumnX)
    , contentAreaStartX(_contentAreaStartX)
    , panelWidth(_panelWidth)
    , panelContentRightX(_panelContentRightX)
    , currentY(_currentY)
    , scale(dim.scale)
    , tooltipY(0.0f)
{
    bt = _parent->planButtonTerms(dim);
}

SettingsLayoutContext::ButtonRowGeom SettingsLayoutContext::buttonRow(int labelChars) const {
    ButtonRowGeom g;
    g.w = charWidth() * static_cast<float>(labelChars) + bt.insetL + bt.insetR;
    g.h = bt.insetT + lineHeightNormal + bt.insetB;
    g.centerX = contentAreaStartX + (panelWidth - paddingH - paddingH) / 2.0f;
    g.x = g.centerX - g.w / 2.0f;
    // The BOX's own margin, and only that: the junction above a button is the
    // caller's addSpacing() (the same [panel] gap every other stacked thing in
    // a tab uses), which the three Updates buttons already called and the two
    // in General spelled as a hardcoded `lineHeightNormal * 0.5f` instead. The
    // split is the box model's own: a junction belongs to the stack, a margin
    // belongs to the box.
    g.y = currentY + bt.marginT;
    g.labelY = g.y + bt.insetT;
    g.advance = bt.marginT + g.h + bt.marginB;
    return g;
}

float SettingsLayoutContext::charWidth() const {
    return PluginUtils::calculateMonospaceTextWidth(1, fontSize);
}

std::string SettingsLayoutContext::formatValue(const char* value, int maxWidth, bool center) {
    std::string result(value);

    // CUT, not ellipsised. An ellipsis spends three of a narrow field's characters
    // saying "there was more" -- which the clipped word already says -- and it did it
    // inconsistently: some values here ellipsised while others elsewhere simply ran
    // out of column, so two truncations of the same length looked like two different
    // states. Cutting is the one behaviour, and it keeps three more characters of the
    // thing you were trying to read.
    if (static_cast<int>(result.length()) > maxWidth) {
        result.resize(static_cast<size_t>(maxWidth));
    }

    // Left-pad for centering if requested
    if (center && static_cast<int>(result.length()) < maxWidth) {
        int padding = (maxWidth - static_cast<int>(result.length())) / 2;
        result = std::string(padding, ' ') + result;
    }

    // Right-pad to fixed width
    while (static_cast<int>(result.length()) < maxWidth) {
        result += ' ';
    }

    return result;
}

float SettingsLayoutContext::addSectionHeading(const char* title, const char* hint) {
    // A section card is a FULL 9-slice over the whole section -- top/left/right/
    // bottom edges and four corners, exactly like the outer panel. Its bottom edge
    // is drawn by the bottom edge slice; nothing special-cases it.
    //
    // It has to be pushed BEFORE the section's controls (quads draw in order, so it
    // must sit behind them) but is only sized once the section ends, so each header
    // closes the previous card and reserves its own.
    closeSectionCard();
    m_lastWasNote = false;

    // THIS FUNCTION OWNS THE GAP -- never precede a call with addSpacing().
    //
    // Enforced by tests/integration/check_section_spacing.sh, not by this comment.
    // The gap landed here after 29 addSpacing() calls already preceded it, and every
    // one was left behind: each section boundary then cost 1.18 line heights instead
    // of 0.5, and the tallest tabs (Appearance, Widgets, Hotkeys) ran off the bottom
    // of the panel. Prose would not have stopped the 30th.
    //
    // It cannot be made impossible by construction here, and the reason is worth
    // recording so nobody re-tries it: absorbing the stray spacing would mean
    // snapping currentY to the previous section's last ROW, and this function only
    // ever sees the cursor AFTER the caller has moved it -- the two are
    // indistinguishable from in here. Tracking a per-row bottom would mean touching
    // every control helper, for a rule one grep already catches.
    //
    // The card pads are in the gap because each card extends that far toward its
    // neighbour (the closed card's bottom is at currentY + its bottom pad, the next
    // card's top at m_sectionTop - its top pad), so the VISIBLE gap would otherwise
    // be short by both pads.
    openSectionCard();
    m_hadSection = true;

    const float headingY = currentY;
    parent->addString(title, labelX, headingY, Justify::LEFT,
        Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), fontSize);
    if (hint && hint[0] != '\0') {
        parent->addString(hint, labelX + charWidth() * SECTION_HINT_COLUMN, headingY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getMuted(), fontSize * 0.9f);
    }
    currentY = headingY + lineHeightNormal;
    return headingY;
}

void SettingsLayoutContext::addNote(const char* text) {
    // A note stays INSIDE the section it follows: half a row of air, then the
    // muted line, advanced at the 0.9 row its type draws. It used to close the
    // card and draw on air below it -- rows no section reported, which the
    // engine (reserving per section) could not see, so every tab ending in a
    // tip overflowed the panel by exactly the tip's height (nine tabs,
    // measured by settings_fit_test). Reserving the tail separately priced the
    // tallest tabs past the screen at the themed frames; as section content it
    // is measured by the same walk as every other row, for free.
    //
    // Consecutive notes are one paragraph, not two captions -- the air belongs
    // only above the first (the Timing tab has two lines).
    if (!m_lastWasNote) currentY += lineHeightNormal * 0.5f;
    m_lastWasNote = true;
    parent->addString(text, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), ColorConfig::getInstance().getMuted(), fontSize * 0.9f);
    currentY += lineHeightNormal * 0.9f;
}

float SettingsLayoutContext::addRadioRow(
    bool selected, SettingsHud::ClickRegion::Type type,
    int clickChars, const char* tooltipId
) {
    const float radioWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::CHECKBOX_WIDTH, fontSize);

    if (tooltipId && tooltipId[0] != '\0') {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowSpanWidth(), lineHeightNormal, tooltipId));
    }
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY,
        radioWidth + PluginUtils::calculateMonospaceTextWidth(clickChars, fontSize),
        lineHeightNormal, type, nullptr));

    parent->addString(selected ? "(O)" : "( )", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), fontSize);

    return labelX + radioWidth;
}

void SettingsLayoutContext::addInlineNote(const char* text) {
    addSpacing();
    parent->addString(text, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), ColorConfig::getInstance().getMuted(), fontSize * 0.9f);
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addTextRow(const char* text, unsigned long color) {
    parent->addString(text, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), color, fontSize);
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addLabelValueRow(
    const char* label, unsigned long labelColor,
    const char* value, unsigned long valueColor,
    int valueColumn
) {
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), labelColor, fontSize);
    if (value && value[0] != '\0') {
        const float valueX = (valueColumn < 0)
            ? controlX
            : labelX + PluginUtils::calculateMonospaceTextWidth(valueColumn, fontSize);
        parent->addString(value, valueX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
    }
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addButtonBackground(float x, float y, float width, float height, unsigned long color) {
    // Height comes from the caller (buttonRow().h — the full [button] box).
    // There was a BUTTON_ROW_FILL fudge here once that shrank the button so it
    // would not touch the card's bottom edge -- a button-shaped patch for a
    // card-shaped problem.
    const float h = height;
    // THROUGH addButtonQuad, not a hand-rolled copy of it. This duplicated the themed
    // branch and then fell back to a raw quad, which skipped opaqueButtonColor -- so with
    // NO theme installed (the shipped default) the settings panel drew Reset and the
    // update button at their 50% state alpha while Save and Close, which go through the
    // helper, were opaque. Four buttons on one row under two compositing rules, and the
    // comment beside Reset promising the opposite.
    parent->addButtonQuad(x, y, width, h, color);
}

// See the declaration. Shares buttonRow()'s geometry rather than re-deriving it: the
// pair is laid out as one row of (w + gap + w), centred on the same axis a lone
// button uses, so a tab mixing the two keeps one button axis.
// See the declaration: one owner for link styling and hit-testing.
void SettingsLayoutContext::addLinkRow(const char* prefix, const char* url, int prefixChars,
                                       SettingsHud::ClickRegion::Type type, float fontScale) {
    ColorConfig& colors = ColorConfig::getInstance();
    const float fs = fontSize * fontScale;
    const float urlX = labelX + PluginUtils::calculateMonospaceTextWidth(prefixChars, fs);
    const float urlW = PluginUtils::calculateMonospaceTextWidth(
        static_cast<int>(std::strlen(url)), fs);

    // The region covers the URL ONLY, not the muted label, so only the link lights up
    // and only clicking the link opens a browser.
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        urlX, currentY, urlW, lineHeightNormal, type, nullptr));
    const bool hovered = parent->m_hoveredRegionIndex >= 0 &&
        parent->m_hoveredRegionIndex == static_cast<int>(parent->m_clickRegions.size()) - 1;

    parent->addString(prefix, labelX, currentY, PluginConstants::Justify::LEFT,
                      Fonts::getNormal(), colors.getMuted(), fs);
    parent->addString(url, urlX, currentY, PluginConstants::Justify::LEFT,
                      Fonts::getNormal(),
                      hovered ? PluginUtils::lightenColor(colors.getAccent(), 0.25f)
                              : colors.getAccent(), fs);
    nextLine();
}

void SettingsLayoutContext::addActionButtonPair(
    const char* labelA, SettingsHud::ClickRegion::Type typeA, ButtonRole roleA, bool enabledA,
    const char* labelB, SettingsHud::ClickRegion::Type typeB, ButtonRole roleB, bool enabledB,
    int labelChars
) {
    ColorConfig& colors = ColorConfig::getInstance();
    const ButtonRowGeom bg = buttonRow(labelChars);
    const float gap = bt.gap;
    const float pairW = bg.w * 2.0f + gap;
    const float leftX = bg.centerX - pairW / 2.0f;

    struct One { const char* label; SettingsHud::ClickRegion::Type type; ButtonRole role; bool on; float x; };
    const One two[2] = {
        { labelA, typeA, roleA, enabledA, leftX },
        { labelB, typeB, roleB, enabledB, leftX + bg.w + gap },
    };
    for (const One& b : two) {
        // Same region policy and same colour/state derivation as the single-button
        // path above -- no region when disabled, hue for what it does, alpha for
        // disabled/idle/hover.
        const size_t regionIndex = parent->m_clickRegions.size();
        if (b.on) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                b.x, bg.y, bg.w, bg.h, b.type, nullptr));
        }
        const unsigned long roleColor = (b.role == ButtonRole::Positive) ? colors.getPositive()
                                      : (b.role == ButtonRole::Negative) ? colors.getNegative()
                                                                         : colors.getAccent();
        const BaseHud::ButtonState state =
            !b.on ? BaseHud::ButtonState::Disabled
            : (parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                ? BaseHud::ButtonState::Hovered
                : BaseHud::ButtonState::Idle;
        parent->addStateButton(b.x, bg.y, bg.w, bg.h, b.label, bg.labelY, fontSize,
                               roleColor, state);
    }
    currentY += bg.advance;
}

void SettingsLayoutContext::addActionButton(
    const char* label,
    int labelChars,
    SettingsHud::ClickRegion::Type type,
    ButtonRole role,
    bool enabled
) {
    ColorConfig& colors = ColorConfig::getInstance();
    const ButtonRowGeom bg = buttonRow(labelChars);

    // NO REGION WHEN DISABLED, the stricter of the two policies the five sites
    // used: Check Now suppressed it, Copy and Reset pushed one anyway and leaned
    // on their handlers re-checking the same condition. Both handlers do guard,
    // so this is not a fix so much as one answer instead of two.
    const size_t regionIndex = parent->m_clickRegions.size();
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            bg.x, bg.y, bg.w, bg.h, type, nullptr));
    }

    // Colour carries state; the shape and the label come from BaseHud's one button
    // emitter. HUE says what the button does (see ButtonRole), alpha says
    // disabled/idle/hover, and the glyph is derived from the fill there.
    const unsigned long roleColor = (role == ButtonRole::Positive) ? colors.getPositive()
                                  : (role == ButtonRole::Negative) ? colors.getNegative()
                                                                   : colors.getAccent();
    const BaseHud::ButtonState state =
        !enabled ? BaseHud::ButtonState::Disabled
        : (parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
            ? BaseHud::ButtonState::Hovered
            : BaseHud::ButtonState::Idle;
    parent->addStateButton(bg.x, bg.y, bg.w, bg.h, label, bg.labelY, fontSize,
                           roleColor, state);

    currentY += bg.advance;
}

void SettingsLayoutContext::openSectionCard() {
    // THE SEAM ABOVE THIS SECTION -- and where it comes from is the whole point of
    // the port. Every section in this column goes through here, the tab
    // description's block included, so the plan index cannot drift the way it
    // would if only captioned sections consumed one.
    if (nextSection < planSectionY.size()) {
        // DRAWING: the engine already placed this section, so jump to its origin.
        // The air above it is then the seam the engine puts between any two
        // siblings -- not a second spelling of it here, which is what had to be
        // kept equal to the sidebar's and was not.
        currentY = planSectionY[nextSection];
    } else if (m_hadSection) {
        // MEASURING: there is no plan yet (this pass is what produces one), so lay
        // the sections end to end with the seam spelled out: this card's bottom
        // pad, the visible gap, the next card's top pad. It must equal what the
        // engine will spend, and settings_fit_test's overflow number is what
        // reports it when it does not.
        currentY += parent->cardPadBotY() + parent->contentGapY()
                  + parent->cardPadTopY();
    }
    ++nextSection;

    // THE SECTION IS OPEN WHETHER OR NOT ART DRAWS IT. A section is a box in the
    // layout; the card is what a theme paints over one. Tying the bookkeeping to
    // hasThemedCard() would make the section list -- which is what the panel
    // declares to the box engine -- appear only under a theme with card sprites,
    // which is the same "a border needs art, a box does not" split layoutPanel
    // makes for every term.
    m_sectionTop = currentY;
    m_sectionOpen = true;
    // NO CARD IS PUSHED HERE ANY MORE. addPlanBackground drew one per section of
    // both columns, from the engine's own boxes, before any row was emitted -- so
    // the reserve-then-rewrite pair this used to run (and the card edges it had to
    // derive) are gone with it.
    m_sectionCardIndex = -1;
}

void SettingsLayoutContext::closeSectionCard() {
    // The section's CONTENT height -- what it asked for, with no card pad or border
    // in it, which is exactly what PanelWant::ColumnWant::sectionH states. A
    // MEASURING pass is the only reader; a drawing pass has the plan already.
    if (!m_sectionOpen) return;
    measuredSections.push_back(currentY - m_sectionTop);
    m_sectionOpen = false;
#if defined(MXBMRP3_TEST_BUILD)
    // The card's left edge, straight off the engine's box for this column -- what
    // testCardEdgesX() reports. It used to be derived here, from the column and the
    // card's own terms, which is the derivation the engine now owns.
    parent->m_testContentCardLeftX = planCardLeftX;
#endif
}

void SettingsLayoutContext::finishSections() {
    closeSectionCard();
}

void SettingsLayoutContext::addTabTooltip(const char* tabId) {
    // Store tabId and Y position for later - tooltip will be rendered by settings_hud.cpp
    // This allows control tooltips to replace tab tooltip when hovering
    currentTabId = tabId ? tabId : "";

    // The description gets its OWN section card, like every other block in the
    // content column. It used to be the one exception, and being the exception cost
    // it two hand-written vertical corrections -- one giving back the pad the panel
    // reserves for a card that wasn't there, one re-adding the following card's --
    // which existed only to make an un-carded block sit at the same rhythm as the
    // carded ones. A card gets that rhythm for free.
    //
    // Reserve-then-size, exactly as addSectionHeading does: the card must be pushed
    // before the text (quads draw in order) but is only sized once the block ends.
    // The text itself is drawn later, by settings_hud.cpp at tooltipY, because a
    // hovered control replaces the tab description in place.
    openSectionCard();
    tooltipY = currentY;  // Save Y position for rendering
    // Reserve space for 2 tooltip lines (rendered later in settings_hud.cpp)
    currentY += lineHeightNormal * 2;
    closeSectionCard();
    // Tell the next addSectionHeading() that a card precedes it, so it lays in the
    // standard pad + gap + pad rather than treating itself as the first.
    m_hadSection = true;
}

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    unsigned long valueColorOverride
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided (for Phase 3 hover)
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = rowSpanWidth();
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    // An override wins outright: it exists for the states primary/muted cannot
    // express (a connected device reading green), so enabled/isOff must not
    // second-guess it. Arrows and label keep deriving from `enabled`.
    //
    // 0 means "not set", so a FULLY TRANSPARENT colour cannot be passed — it
    // silently reads as unset and the derived colour is used. Safe today (every
    // palette entry has alpha 0xFF), but PluginUtils::applyOpacity(c, 0.0f) is
    // exactly 0, so an opacity-adjusted colour is the way in. If a second
    // override lands, make this a std::optional rather than widening the rule.
    unsigned long valueColor = valueColorOverride ? valueColorOverride
        : ((enabled && !isOff) ? colors.getPrimary() : colors.getMuted());

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            downType, targetHud, 0, false, 0
        ));
    }
    currentX += cw * 2;

    // Value with fixed width (formatted, left-aligned for consistent positioning)
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    }

    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    const SettingsHud::ClickRegion::TargetPointer& payload,
    const char* tooltipId
) {
    // Emit the row through the plain overload, then stamp the payload onto the
    // arrow regions it created. The row tooltip is a TOOLTIP_ROW region, so it is
    // never one of them.
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, valueWidth, downType, upType,
        /*targetHud=*/nullptr, /*enabled=*/true, /*isOff=*/false, tooltipId);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == downType || region.type == upType) {
            region.targetPointer = payload;
        }
    }
}

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    const SettingsHud::CycleControl& control,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    bool tooltipOnArrows
) {
    // Register the descriptor for this rebuild (m_cycleControls is cleared in
    // lockstep with m_clickRegions, so the index stays valid exactly as long as
    // the regions below do). Registered even when disabled (no arrow regions),
    // keeping index assignment deterministic.
    const int cycleIndex = static_cast<int>(parent->m_cycleControls.size());
    parent->m_cycleControls.push_back(control);

    // Emit the row via the legacy cycle control, then tag the arrow regions it
    // created with the descriptor index (and optionally the row tooltip, which
    // is what the old per-type tooltip fallback resolved to for these controls).
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, valueWidth,
        SettingsHud::ClickRegion::CYCLE_DOWN,
        SettingsHud::ClickRegion::CYCLE_UP,
        targetHud, enabled, isOff, tooltipId);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == SettingsHud::ClickRegion::CYCLE_UP ||
            region.type == SettingsHud::ClickRegion::CYCLE_DOWN) {
            region.cycleIndex = cycleIndex;
            if (tooltipOnArrows && tooltipId) region.tooltipId = tooltipId;
        }
    }
}

void SettingsLayoutContext::addSteppedControl(
    const char* label,
    const char* value,
    int valueWidth,
    const SettingsHud::SteppedControl& control,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    bool tooltipOnArrows
) {
    // Register the descriptor for this rebuild (m_steppedControls is cleared in
    // lockstep with m_clickRegions, so the index stays valid exactly as long as
    // the regions below do). Registered even when disabled (no arrow regions),
    // keeping index assignment deterministic.
    const int steppedIndex = static_cast<int>(parent->m_steppedControls.size());
    parent->m_steppedControls.push_back(control);

    // Emit the row via the standard cycle control, then tag the arrow regions it
    // created with the descriptor index (and optionally the row tooltip, which is
    // what the old per-type tooltip fallback resolved to for these controls).
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, valueWidth,
        SettingsHud::ClickRegion::STEPPED_DOWN,
        SettingsHud::ClickRegion::STEPPED_UP,
        targetHud, enabled, isOff, tooltipId);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == SettingsHud::ClickRegion::STEPPED_UP ||
            region.type == SettingsHud::ClickRegion::STEPPED_DOWN) {
            region.steppedIndex = steppedIndex;
            if (tooltipOnArrows && tooltipId) region.tooltipId = tooltipId;
        }
    }
}

void SettingsLayoutContext::addToggleControl(
    const char* label,
    bool isOn,
    SettingsHud::ClickRegion::Type toggleType,
    BaseHud* targetHud,
    uint32_t* bitfield,
    uint32_t flag,
    bool enabled,
    const char* tooltipId,
    const char* valueOverride
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided (for Phase 3 hover)
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = rowSpanWidth();
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

    // Use override value if provided, otherwise show On/Off
    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, VALUE_WIDTH, false);

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        if (bitfield != nullptr) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
    }
    currentX += cw * 2;

    // Value with fixed width
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        if (bitfield != nullptr) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, bitfield, flag, false, targetHud
            ));
        } else {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, targetHud
            ));
        }
    }

    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addToggleControl(
    const char* label,
    bool isOn,
    SettingsHud::ClickRegion::Type toggleType,
    BaseHud* targetHud,
    bool* boolPtr,
    bool enabled,
    const char* tooltipId,
    const char* valueOverride
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Add row-wide tooltip region if tooltipId is provided
    if (tooltipId && tooltipId[0] != '\0') {
        float rowWidth = rowSpanWidth();
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, VALUE_WIDTH, false);

    // Left arrow "<" - always visible, muted when disabled, clickable only when enabled
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            toggleType, boolPtr, targetHud
        ));
    }
    currentX += cw * 2;

    // Value
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - always visible, muted when disabled, clickable only when enabled
    parent->addString(" >", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            toggleType, boolPtr, targetHud
        ));
    }

    currentY += lineHeightNormal;
}

// The per-HUD panel-theme row: Default / None / <each installed theme>.
// "Default" (the stored value being empty) means follow Appearance > Panel Theme,
// and is what every HUD ships as -- so a user who only sets the global theme never
// has per-HUD values written, and a changed default still reaches them on upgrade.
void SettingsLayoutContext::addPerHudThemeControl(BaseHud* hud) {
    const ColorConfig& colors = ColorConfig::getInstance();
    float rowWidth = rowSpanWidth();

    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.theme"));
    parent->addString("Theme", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);

    const std::string& ov = hud->getThemeOverride();
    std::string label;
    if (ov.empty()) {
        label = "Default";
    } else if (ov == BaseHud::THEME_NONE) {
        label = "None";
    } else if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
        label = t->displayName;
    } else {
        // Names an installed-then-removed theme. activeTheme() falls back to the
        // global one, so say so rather than showing a theme that isn't drawing.
        label = "Default";
    }

    float toggleX = controlX;
    float cw = charWidth();
    constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

    parent->addString("<", toggleX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getAccent(), fontSize);
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        toggleX, currentY, cw * 2, lineHeightNormal,
        SettingsHud::ClickRegion::HUD_THEME_DOWN, hud, 0, false, 0));
    toggleX += cw * 2;

    std::string formatted = formatValue(label.c_str(), VALUE_WIDTH, false);
    parent->addString(formatted.c_str(), toggleX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getPrimary(), fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    parent->addString(" >", toggleX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getAccent(), fontSize);
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        toggleX, currentY, cw * 2, lineHeightNormal,
        SettingsHud::ClickRegion::HUD_THEME_UP, hud, 0, false, 0));
}

// The Texture row for a HUD whose art comes from an asset PACK rather than from a
// textures/ variant -- the gamepad and the pit board. Same row, same label and the
// same two arrows Radar gets; only the source of the names differs.
//
// It replaces the per-HUD Theme control these two used to fall through to, which was
// dead on exactly them (see BaseHud::m_packKind). Labelled "Texture" rather than
// "Pack" deliberately: it is the same question every other HUD's row asks, and a
// second word for it would be a second concept in the same column.
void SettingsLayoutContext::addPackControl(BaseHud* hud) {
    const AssetManager& assets = AssetManager::getInstance();
    const bool isPad = (hud->m_packKind == BaseHud::PackKind::Gamepad);

    // The pack actually IN USE, not the stored name: an uninstalled name still draws
    // the shipped default, and the row has to say what is on screen.
    std::string label = "None";
    if (isPad) {
        if (const GamepadAsset* a = static_cast<GamepadWidget*>(hud)->activePack())
            label = a->displayName;
    } else {
        if (const PitboardAsset* a = static_cast<PitboardHud*>(hud)->activePack())
            label = a->displayName;
    }

    // Greyed with nothing to cycle -- one pack is a legitimate install, and with the
    // Off entry gone there is genuinely nowhere for the arrows to go.
    const size_t count = isPad ? assets.getGamepadCount() : assets.getPitboardCount();
    addCycleControl("Texture", label.c_str(), STANDARD_VALUE_WIDTH,
        isPad ? SettingsHud::ClickRegion::GAMEPAD_PACK_DOWN
              : SettingsHud::ClickRegion::PITBOARD_PACK_DOWN,
        isPad ? SettingsHud::ClickRegion::GAMEPAD_PACK_UP
              : SettingsHud::ClickRegion::PITBOARD_PACK_UP,
        hud, /*enabled=*/count > 1, /*isOff=*/false,
        isPad ? "gamepad.pack" : "pitboard.pack");
}

float SettingsLayoutContext::addStandardHudControls(BaseHud* hud) {
    const bool enableTitle = hud->m_titleSupported;
    // Save starting Y for right column (data toggles)
    float sectionStartY = currentY;
    ColorConfig& colors = ColorConfig::getInstance();
    // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
    float rowWidth = rowSpanWidth();

    // Visibility toggle. ACTIVE SURFACE, not the game flag: the click below emits
    // HUD_TOGGLE, which edits whichever surface the menu is on, so displaying
    // isVisible() would show the game's state while the click changed the
    // companion's. (The inline variant further down already reads it this way.)
    bool isVisible = hud->isVisibleOnActiveSurface();
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.visible"
    ));
    parent->addString("Visible", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    // Use inline toggle (same line)
    {
        float toggleX = controlX;
        float cw = charWidth();
        unsigned long valueColor = isVisible ? colors.getPrimary() : colors.getMuted();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::HUD_TOGGLE, hud
        ));
        toggleX += cw * 2;

        std::string formattedVisible = formatValue(isVisible ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedVisible.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::HUD_TOGGLE, hud
        ));
    }
    currentY += lineHeightNormal;

    // THE TITLE ROW IS ABSENT, not greyed, on a panel that cannot carry a caption
    // (BaseHud::m_titleSupported). A greyed row is right for a setting that is
    // temporarily unavailable -- it says the setting exists and hints at what would
    // enable it -- and wrong for one that does not apply to this panel at all: it reads
    // as something broken, and it spends a row on every one of these tabs. The Widgets
    // TABLE still greys its Title column rather than dropping it, because that column is
    // shared by nineteen rows and cannot be per-row.
    if (enableTitle) {
        const bool showTitle = hud->getShowTitle();
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, "common.title"
        ));
        parent->addString("Title", labelX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getSecondary(), fontSize);

        float toggleX = controlX;
        const float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::TITLE_TOGGLE, hud
        ));
        toggleX += cw * 2;

        std::string formattedTitle = formatValue(showTitle ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedTitle.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), showTitle ? colors.getPrimary() : colors.getMuted(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::TITLE_TOGGLE, hud
        ));
        currentY += lineHeightNormal;
    }

    // One row, two meanings, decided by what the HUD actually HAS.
    //
    // A HUD with background textures (gamepad, pitboard, pointer, radar, speedo,
    // tacho) keeps the Texture cycle: that texture IS its look, and a theme is
    // suppressed for it anyway. Every other HUD declares a texture base name with
    // no files behind it, so its Texture row was permanently "Off" and unclickable
    // -- dead UI. Those get the per-HUD Theme override instead.
    bool hasTextures = !hud->getAvailableTextureVariants().empty();
    if (hud->m_packKind != BaseHud::PackKind::None) {
        // A PACK HUD gets a Texture row that cycles PACKS -- the same row Radar gets,
        // driven by a different source. It used to fall through to the per-HUD Theme
        // control below (no texture base name, so hasTextures is false), and that
        // control is DEAD here: the artwork is mandatory on these HUDs and artwork
        // suppresses the theme, so nothing it offered could take effect. Reported as
        // "why does the pitboard still have a theme option".
        addPackControl(hud);   // advances currentY itself (addCycleControl does)
    } else if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
        addPerHudThemeControl(hud);
        currentY += lineHeightNormal;
    } else {

    // Background texture variant cycle (Off, 1, 2, ...)
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.texture"
    ));
    parent->addString("Texture", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), hasTextures ? colors.getSecondary() : colors.getMuted(), fontSize);
    char textureValue[16];
    int variant = hud->getTextureVariant();
    if (!hasTextures || variant == 0) {
        snprintf(textureValue, sizeof(textureValue), "Off");
    } else {
        snprintf(textureValue, sizeof(textureValue), "%d", variant);
    }
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        if (hasTextures) {
            parent->addString("<", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN, hud, 0, false, 0
            ));
        }
        toggleX += cw * 2;

        std::string formattedTexture = formatValue(textureValue, VALUE_WIDTH, false);
        parent->addString(formattedTexture.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), hasTextures ? colors.getPrimary() : colors.getMuted(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        if (hasTextures) {
            parent->addString(" >", toggleX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                toggleX, currentY, cw * 2, lineHeightNormal,
                SettingsHud::ClickRegion::TEXTURE_VARIANT_UP, hud, 0, false, 0
            ));
        }
    }
    currentY += lineHeightNormal;

    }   // end of the Texture branch (see the Theme/Texture choice above)

    // Background opacity controls
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.opacity"
    ));
    parent->addString("Opacity", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    char opacityValue[16];
    int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
    snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN, hud, 0, false, 0
        ));
        toggleX += cw * 2;

        std::string formattedOpacity = formatValue(opacityValue, VALUE_WIDTH, false);
        parent->addString(formattedOpacity.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getPrimary(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP, hud, 0, false, 0
        ));
    }
    currentY += lineHeightNormal;

    // Scale controls
    // Add row-wide tooltip region
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        labelX, currentY, rowWidth, lineHeightNormal, "common.scale"
    ));
    parent->addString("Scale", labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);
    char scaleValue[16];
    int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
    snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
    {
        float toggleX = controlX;
        float cw = charWidth();
        constexpr int VALUE_WIDTH = STANDARD_VALUE_WIDTH;

        parent->addString("<", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::SCALE_DOWN, hud, 0, false, 0
        ));
        toggleX += cw * 2;

        std::string formattedScale = formatValue(scaleValue, VALUE_WIDTH, false);
        parent->addString(formattedScale.c_str(), toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getPrimary(), fontSize);
        toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        parent->addString(" >", toggleX, currentY, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, currentY, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::SCALE_UP, hud, 0, false, 0
        ));
    }
    currentY += lineHeightNormal;

    return sectionStartY;
}

void SettingsLayoutContext::addDataToggle(
    const char* label,
    uint32_t* bitfield,
    uint32_t flag,
    bool isRequired,
    BaseHud* targetHud,
    float yPos,
    int labelWidth
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();
    bool isChecked = (*bitfield & flag) != 0;
    bool enabled = !isRequired;

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Toggle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && isChecked) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = 3;

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, flag, false, targetHud
        ));
    }
    toggleX += cw * 2;

    std::string formattedValue = formatValue(isChecked ? "On" : "Off", VALUE_WIDTH, false);
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, flag, false, targetHud
        ));
    }
}

void SettingsLayoutContext::addGroupToggle(
    const char* label,
    uint32_t* bitfield,
    uint32_t groupFlags,
    bool isRequired,
    BaseHud* targetHud,
    float yPos,
    int labelWidth
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();
    // Group is checked if all bits in group are set
    bool isChecked = (*bitfield & groupFlags) == groupFlags;
    bool enabled = !isRequired;

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Toggle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && isChecked) ? colors.getPrimary() : colors.getMuted();
    constexpr int VALUE_WIDTH = 3;

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, groupFlags, false, targetHud
        ));
    }
    toggleX += cw * 2;

    std::string formattedValue = formatValue(isChecked ? "On" : "Off", VALUE_WIDTH, false);
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            SettingsHud::ClickRegion::CHECKBOX, bitfield, groupFlags, false, targetHud
        ));
    }
}

void SettingsLayoutContext::nextLine() {
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addSpacing() {
    // THE JUNCTION GAP, [Advanced] panelGap / a theme's [panel] gap — the term
    // already named for "air between a panel's stacked children", which is
    // exactly what a caller is asking for here.
    //
    // It replaced settingsRowGap (1 cell) and settingsBlockGap (2), a private
    // two-tier rhythm reachable from no ini and spent at six call sites in one
    // tab. Two knobs for one distance, and neither the same knob the seam either
    // side of them already used.
    //
    // Converted the BOX way (cellW * aspect), not on cellH: one stated cell is
    // square on screen wherever it is spent, and this used to be the one place
    // in the panel where it was not.
    currentY += static_cast<float>(parent->panelGapCells())
              * layout().cellW * PluginConstants::UI_ASPECT_RATIO * parent->getScale();
}

float SettingsLayoutContext::addRightColumnCycleControl(
    const char* label,
    const char* value,
    int valueWidth,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    BaseHud* targetHud,
    float yPos,
    int labelWidth,
    bool enabled,
    bool isOff
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Label with padding
    char paddedLabel[32];
    snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
    parent->addString(paddedLabel, rightColumnX, yPos, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    // Cycle control
    float toggleX = rightColumnX + PluginUtils::calculateMonospaceTextWidth(labelWidth, fontSize);
    unsigned long valueColor = (enabled && !isOff) ? colors.getPrimary() : colors.getMuted();

    // Left arrow "<" - only show when enabled
    if (enabled) {
        parent->addString("<", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            downType, targetHud, 0, false, 0
        ));
    }
    toggleX += cw * 2;

    // Value with fixed width (formatted, left-aligned for consistent positioning)
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), toggleX, yPos, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    toggleX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow " >" - only show when enabled
    if (enabled) {
        parent->addString(" >", toggleX, yPos, Justify::LEFT,
            Fonts::getNormal(), colors.getAccent(), fontSize);
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            toggleX, yPos, cw * 2, lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    }

    return yPos + lineHeightNormal;
}

void SettingsLayoutContext::addWidgetRow(
    const char* name,
    BaseHud* hud,
    bool enableVisibility,
    bool enableBgTexture,
    bool enableOpacity,
    bool enableScale,
    const char* tooltipId,
    bool menuOnlyPointerRow
) {
    float cw = charWidth();
    ColorConfig& colors = ColorConfig::getInstance();

    // Column positions (spacing for table layout with toggle controls)
    float nameX = labelX;
    float visX = nameX + PluginUtils::calculateMonospaceTextWidth(10, fontSize);   // After name
    float titleX = visX + PluginUtils::calculateMonospaceTextWidth(8, fontSize);   // After Vis toggle (< On >)
    float bgTexX = titleX + PluginUtils::calculateMonospaceTextWidth(8, fontSize); // After Title toggle
    float opacityX = bgTexX + PluginUtils::calculateMonospaceTextWidth(8, fontSize); // After BG Tex toggle
    float scaleX = opacityX + PluginUtils::calculateMonospaceTextWidth(9, fontSize); // After Opacity cycle

    // Add row-wide tooltip region if tooltipId is provided
    if (tooltipId && tooltipId[0] != '\0') {
        // panelWidth is actually contentAreaWidth (from contentAreaStartX to right edge)
        float rowWidth = rowSpanWidth();
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowWidth, lineHeightNormal, tooltipId
        ));
    }

    // Widget name
    parent->addString(name, nameX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getPrimary(), fontSize);

    // Helper lambda for inline toggle control (position-based, no label)
    auto addInlineToggle = [&](float x, bool isOn, SettingsHud::ClickRegion::Type toggleType, bool enabled) {
        float currentX = x;
        unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
        constexpr int VALUE_WIDTH = 3;

        if (enabled) {
            parent->addString("<", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, hud
            ));
        }
        currentX += cw * 2;

        std::string formattedValue = formatValue(isOn ? "On" : "Off", VALUE_WIDTH, false);
        parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, fontSize);

        if (enabled) {
            parent->addString(" >", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                toggleType, hud
            ));
        }
    };

    // Helper lambda for inline cycle control (position-based, no label)
    auto addInlineCycle = [&](float x, const char* value, int valueWidth,
                              SettingsHud::ClickRegion::Type downType,
                              SettingsHud::ClickRegion::Type upType,
                              bool enabled) {
        float currentX = x;
        unsigned long valueColor = enabled ? colors.getPrimary() : colors.getMuted();

        if (enabled) {
            parent->addString("<", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                downType, hud, 0, false, 0
            ));
        }
        currentX += cw * 2;

        std::string formattedValue = formatValue(value, valueWidth, false);
        parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
            Fonts::getNormal(), valueColor, fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

        if (enabled) {
            parent->addString(" >", currentX, currentY, Justify::LEFT,
                Fonts::getNormal(), colors.getAccent(), fontSize);
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                currentX, currentY, cw * 2, lineHeightNormal,
                upType, hud, 0, false, 0
            ));
        }
    };

    // Visibility toggle (shows actual value, grayed out when disabled). The pointer
    // row is special: its toggle drives the menu-only-cursor mode, not the widget's
    // real visibility (On = pointer summoned by mouse movement during play; Off =
    // menu-only). The pointer's m_bVisible must stay true so it can still draw in the
    // settings menu, so it can't be the toggle target.
    if (menuOnlyPointerRow) {
        bool pointerOn = !UiConfig::getInstance().getMenuOnlyCursor();
        addInlineToggle(visX, pointerOn, SettingsHud::ClickRegion::MENU_ONLY_CURSOR_TOGGLE, true);
    } else {
        // Show the ACTIVE surface's visibility, like the per-HUD tabs: on the companion
        // window the toggle already edits the companion instance (HUD_TOGGLE routes by
        // active surface), so the displayed On/Off must read it too — otherwise a widget
        // enabled only on the companion still shows the game's state.
        bool visOn = hud->isVisibleOnActiveSurface();
        addInlineToggle(visX, visOn, SettingsHud::ClickRegion::HUD_TOGGLE, enableVisibility);
    }

    // Title toggle, greyed on a widget that carries no caption. The column is shared by
    // every row, so unlike the per-HUD tabs it cannot be dropped -- and it does not need
    // to be, since a whole column of On/Off values reads as a comparison rather than as
    // a broken control. getShowTitle() is already forced false there (see
    // BaseHud::m_titleSupported), so there is no stale value to mask.
    const bool enableTitle = hud->m_titleSupported;
    addInlineToggle(titleX, hud->getShowTitle(), SettingsHud::ClickRegion::TITLE_TOGGLE, enableTitle);

    // Same column, same rule as the per-HUD tabs (addStandardHudControls): a widget
    // with real texture variants keeps the Texture cycle, everything else gets its
    // panel-theme override. Widgets were left on Texture when the per-tab control
    // changed, so most of them showed a permanently-"Off" cycle while the identical
    // setting was reachable for full HUDs.
    bool hasTextures = !hud->getAvailableTextureVariants().empty();
    if (hud->m_packKind != BaseHud::PackKind::None) {
        // The pad picker: this column selects a PACK (gamepads/<name>/ -- art plus the
        // geometry that places buttons on it), not a texture variant, so without this
        // branch the widget would fall into the panel-theme one below -- offering a
        // theme cycle on a panel whose entire body is a photograph, and leaving no way
        // to choose a pad. m_packKind is the same flag the per-HUD tabs route on, so
        // the two can't disagree about which HUDs are pack HUDs.
        //
        // Abbreviated to three characters for the same reason the theme cycle below
        // is: this column is shared with every other widget row and cannot widen for
        // one of them.
        const AssetManager& assets = AssetManager::getInstance();
        const size_t packCount = assets.getGamepadCount();
        // No "Off": the pad artwork IS the widget, so the cycle is packs only (see
        // BaseHud::m_textureRequired). The two comments that used to sit here said the
        // opposite -- that Off was a real position and the only thing clearing
        // showBackgroundTexture -- and both stopped being true when it was removed.
        std::string packValue = "None";
        if (const GamepadAsset* active = static_cast<const GamepadWidget*>(hud)->activePack())
            packValue = active->displayName.substr(0, 3);
        // Needs somewhere to GO now: with one pack installed and no Off entry the
        // arrows would step from a pack to itself.
        addInlineCycle(bgTexX, packValue.c_str(), 3,
            SettingsHud::ClickRegion::GAMEPAD_PACK_DOWN,
            SettingsHud::ClickRegion::GAMEPAD_PACK_UP,
            enableBgTexture && packCount > 1);
    } else if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
        const std::string& ov = hud->getThemeOverride();
        // Abbreviated to fit the table column; the per-HUD tab spells it out.
        std::string themeValue;
        if (ov.empty()) {
            themeValue = "Def";
        } else if (ov == BaseHud::THEME_NONE) {
            themeValue = "Off";
        } else if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
            themeValue = t->displayName.substr(0, 3);
        } else {
            themeValue = "Def";   // unknown name renders as the global theme
        }
        addInlineCycle(bgTexX, themeValue.c_str(), 3,
            SettingsHud::ClickRegion::HUD_THEME_DOWN,
            SettingsHud::ClickRegion::HUD_THEME_UP,
            enableBgTexture);
    } else {
        char texValue[8];
        int texVariant = hud->getTextureVariant();
        snprintf(texValue, sizeof(texValue), (!hasTextures || texVariant == 0) ? "Off" : "%d", texVariant);
        addInlineCycle(bgTexX, texValue, 3,
            SettingsHud::ClickRegion::TEXTURE_VARIANT_DOWN,
            SettingsHud::ClickRegion::TEXTURE_VARIANT_UP,
            enableBgTexture && hasTextures);
    }

    // BG Opacity (shows muted value without arrows when disabled)
    char opacityValue[16];
    int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
    snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
    addInlineCycle(opacityX, opacityValue, 4,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_DOWN,
        SettingsHud::ClickRegion::BACKGROUND_OPACITY_UP,
        enableOpacity);

    // Scale (shows muted value without arrows when disabled)
    char scaleValue[16];
    int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
    snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
    addInlineCycle(scaleX, scaleValue, 4,
        SettingsHud::ClickRegion::SCALE_DOWN,
        SettingsHud::ClickRegion::SCALE_UP,
        enableScale);

    currentY += lineHeightNormal;
}

// Get icon display name from shape index (0 = Off)
std::string getShapeDisplayName(int shapeIndex, int maxWidth) {
    if (shapeIndex <= 0) return "Off";
    const auto& assetMgr = AssetManager::getInstance();
    // Base index: this asks for the icon's NAME, which a theme override does not change.
    int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
    std::string name = assetMgr.getIconDisplayName(spriteIndex);
    if (name.empty()) return "Unknown";
    if (static_cast<int>(name.length()) > maxWidth) name.resize(maxWidth);
    return name;
}
