// ============================================================================
// hud/settings/settings_layout.cpp
// Implementation of shared layout context and helper methods
// ============================================================================
// file-budget: 1610 the settings panel's geometry in one place; split with the next tab rework
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/color_config.h"

#include "../../core/font_config.h"
#include "../../core/ui_config.h"
#include "../../core/asset_manager.h"
#include "../../core/input_manager.h"
#include "../../core/tooltip_manager.h"
#include "../gamepad_widget.h"   // the Gamepad row's pack cycle reads activePack()
#include "../freeze_duration.h"
#include "../pitboard_hud.h"     // ...and the Pitboard row's
#include "../tacho_widget.h"     // ...and both gauge rows'

#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../speedo_widget.h"

// Defined here rather than in the header: SettingsHud is only forward-declared
// there (the header is included BY settings_hud.h), so the parent's layout() is
// not visible until this TU has the full definition.
const LayoutMetrics& SettingsLayoutContext::layout() const {
    return parent->layout();
}

using namespace PluginConstants;


SettingsLayoutContext::SettingsLayoutContext(
    SettingsHud* _parent,
    const ScaledDimensions& dim,
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

int SettingsLayoutContext::valueChars() const {
    // The row runs the whole content column (labelX == contentAreaStartX; see
    // rowSpanWidth), so its width in characters is the column's stated ask. In a
    // beginColumns run it is the cell's.
    if (m_gridCols > 0) return cellChars(m_gridCols) - m_gridLabelChars - 4;
    return layout().settingsContentAreaChars()
        - (m_rowLabelChars > 0 ? m_rowLabelChars : SETTINGS_CONTROL_COLUMN) - 4;
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
    // THE HINT IS PART OF THE HEADING STRING, not a second string placed after
    // it. It was drawn in the normal face at a fixed column counted in normal
    // cells, while the heading is drawn in STRONG and the layout has one cell
    // width for every face (PluginUtils::charWidthRatio) -- so the gap between
    // them was whatever the user's Strong font happened to measure: right under
    // the shipped one, a hole or an overlap under another. One string cannot
    // drift from itself; the callers already parenthesise their own text.
    char heading[160];
    if (hint && hint[0] != '\0') snprintf(heading, sizeof(heading), "%s %s", title, hint);
    else                         snprintf(heading, sizeof(heading), "%s", title);
    parent->addString(heading, labelX, headingY, Justify::LEFT,
        Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), fontSize);
    currentY = headingY + lineHeightNormal;
    return headingY;
}

void SettingsLayoutContext::addNote(const char* text) {
    // A note stays INSIDE the section it follows: half a row of air, then the
    // muted line, advanced at the 0.9 row its type draws. Drawn on air below a
    // closed card it would be rows no section reported, which the engine
    // (reserving per section) cannot see, so every tab ending in a tip would
    // overflow the panel by exactly the tip's height (pinned by
    // settings_fit_test). Reserving the tail separately would price the
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

void SettingsLayoutContext::addInlineNote(const char* text) {
    addSpacing();
    parent->addString(text, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), ColorConfig::getInstance().getMuted(), fontSize * 0.9f);
    currentY += lineHeightNormal;
}

size_t SettingsLayoutContext::addInlineCycle(float x, const char* value, int valueChars,
                                             SettingsHud::ClickRegion::Type downType,
                                             SettingsHud::ClickRegion::Type upType,
                                             BaseHud* target, bool enabled, bool muted) {
    const ColorConfig& colors = ColorConfig::getInstance();
    const float cw = charWidth();
    const size_t first = parent->m_clickRegions.size();
    const unsigned long valueColor = (enabled && !muted) ? colors.getPrimary() : colors.getMuted();
    // Arrows always drawn, muted when disabled -- the addCycleControl look, so a
    // greyed cell reads the same as a greyed row. Clickable only when enabled.
    const unsigned long arrowColor = enabled ? colors.getAccent() : colors.getMuted();
    float currentX = x;
    parent->addString("<", currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), arrowColor, fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal, downType, target));
    }
    currentX += cw * 2;
    const std::string formatted = formatValue(value, valueChars, false);
    parent->addString(formatted.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(valueChars, fontSize);
    // ">" placed one cell past the value, not " >" at it: in game a value that
    // filled its field ("100%" in four) read "100%>", so the gap is the grid's
    // rather than a leading space's.
    parent->addString(">", currentX + cw, currentY, Justify::LEFT,
        Fonts::getNormal(), arrowColor, fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal, upType, target));
    }
    return first;
}

static SliderControl sliderFor(const SettingsHud::SteppedControl& c);   // below, with the rows

// The inline cell over a descriptor: the cell's arrows step it, and like a row it
// draws as a dropdown (3+ named states) or a slider (a bounded number) over the value.
size_t SettingsLayoutContext::addInlineCycleControl(float x, const char* value, int valueChars,
                                                    const SettingsHud::CycleControl& control,
                                                    BaseHud* target, bool enabled, const char* tooltipId) {
    const int cycleIndex = static_cast<int>(parent->m_cycleControls.size());
    parent->m_cycleControls.push_back(control);
    const size_t first = addInlineCycle(x, value, valueChars, SettingsHud::ClickRegion::CYCLE_DOWN,
                                        SettingsHud::ClickRegion::CYCLE_UP, target, enabled);
    for (size_t r = first; r < parent->m_clickRegions.size(); ++r) {
        parent->m_clickRegions[r].cycleIndex = cycleIndex;
        if (tooltipId) parent->m_clickRegions[r].tooltipId = tooltipId;
    }
    if (control.nameOf && control.count >= 3) {
        addDropdownBox(x + charWidth() * 2.0f, currentY,
            PluginUtils::calculateMonospaceTextWidth(valueChars + 1, fontSize), cycleIndex, enabled, tooltipId);
    }
    return first;
}

size_t SettingsLayoutContext::addInlineSteppedControl(float x, const char* value, int valueChars,
                                                      const SettingsHud::SteppedControl& control,
                                                      BaseHud* target, bool enabled, const char* tooltipId,
                                                      bool muted) {
    const int steppedIndex = static_cast<int>(parent->m_steppedControls.size());
    parent->m_steppedControls.push_back(control);
    const size_t first = addInlineCycle(x, value, valueChars, SettingsHud::ClickRegion::STEPPED_DOWN,
                                        SettingsHud::ClickRegion::STEPPED_UP, target, enabled, muted);
    for (size_t r = first; r < parent->m_clickRegions.size(); ++r) {
        parent->m_clickRegions[r].steppedIndex = steppedIndex;
        if (tooltipId) parent->m_clickRegions[r].tooltipId = tooltipId;
    }
    if (control.kind != SettingsHud::SteppedControl::Kind::WRAP_INT) {
        addSliderTrack(x + charWidth() * 2.0f, currentY,
            PluginUtils::calculateMonospaceTextWidth(valueChars, fontSize), sliderFor(control), enabled, tooltipId);
    }
    return first;
}

// Three characters between neighbouring cells, so one cell's closing ">" reads as
// the end of its own control rather than a marker on the next cell's label (at one,
// "> Accent" did).
static constexpr int GRID_CELL_GAP_CHARS = 3;

int SettingsLayoutContext::cellChars(int cellCount) const {
    const int cells = (cellCount < 1) ? 1 : cellCount;
    return (layout().settingsContentAreaChars() - (cells - 1) * GRID_CELL_GAP_CHARS) / cells;
}

float SettingsLayoutContext::cellX(int cellIndex, int cellCount) const {
    const float left = (m_gridCols > 0) ? m_rowLabelX : labelX;
    return left + charWidth() * static_cast<float>(cellIndex * (cellChars(cellCount) + GRID_CELL_GAP_CHARS));
}

void SettingsLayoutContext::beginColumns(int columns, int items, int labelChars) {
    if (columns < 2 || items < 1) return;   // one column: the rows as they are
    m_gridCols = columns;
    m_gridRows = (items + columns - 1) / columns;
    m_gridItem = 0;
    m_gridLabelChars = labelChars;
    m_gridTop = currentY;
    m_rowLabelX = labelX;
    m_rowControlX = controlX;
    placeCell();
}

void SettingsLayoutContext::setRowLabelChars(int labelChars) {
    if (m_defaultControlX < 0.0f) m_defaultControlX = controlX;
    m_rowLabelChars = labelChars > 0 ? labelChars : 0;
    controlX = m_rowLabelChars > 0 ? labelX + charWidth() * static_cast<float>(m_rowLabelChars)
                                   : m_defaultControlX;
}

void SettingsLayoutContext::endColumns() {
    if (m_gridCols == 0) return;
    currentY = m_gridTop + lineHeightNormal * static_cast<float>(m_gridRows);
    labelX = m_rowLabelX;
    controlX = m_rowControlX;
    m_gridCols = 0;
}

void SettingsLayoutContext::placeCell() {
    const int col = m_gridItem / m_gridRows;
    currentY = m_gridTop + lineHeightNormal * static_cast<float>(m_gridItem % m_gridRows);
    labelX = cellX(col, m_gridCols);
    controlX = labelX + charWidth() * static_cast<float>(m_gridLabelChars);
    m_cellFirstRegion = parent->m_clickRegions.size();
}

// The blank part of the closing ">"'s character cell, right of its ink.
static constexpr float CLOSING_ARROW_TRAIL_CHARS = 0.4f;

void SettingsLayoutContext::endRow() {
    if (m_gridCols == 0) {
        currentY += lineHeightNormal;
        return;
    }
    // The cell's tooltip region was emitted at the row's width; make it the
    // cell's own span, label to closing arrow. SettingsHud::rowBandSpan grows it
    // by the margin a whole row's band has past its rows, so a cell's hover band
    // sits on the cell the way a row's sits on the row.
    // It ends at the closing arrow's INK, not its character cell: the ">" glyph
    // fills the left part of its cell, and the rest plus the margin read as a
    // band running on past the control into the gap (Appearance's colours).
    const int col = m_gridItem / m_gridRows;
    const float cw = charWidth();
    for (size_t r = m_cellFirstRegion; r < parent->m_clickRegions.size(); ++r) {
        SettingsHud::ClickRegion& region = parent->m_clickRegions[r];
        if (region.type != SettingsHud::ClickRegion::TOOLTIP_ROW) continue;
        region.x = labelX;
        region.width = cw * (static_cast<float>(cellChars(m_gridCols)) - CLOSING_ARROW_TRAIL_CHARS);
        region.cellIndex = col;
        region.cellCount = m_gridCols;
    }
    ++m_gridItem;
    if (m_gridItem < m_gridRows * m_gridCols) placeCell();
}

void SettingsLayoutContext::addInputField(float x, int fieldChars, const char* text,
                                          unsigned long color, bool active, int cursorColumn) {
    ColorConfig& colors = ColorConfig::getInstance();
    const float cw = charWidth();
    // The dropdown box's fill and height (addDropdownBox), so the two read as one
    // family; a field being typed into takes the accent instead.
    const float boxX = x + cw * 0.7f;
    const float boxW = cw * (static_cast<float>(fieldChars) + 0.3f);
    const float boxY = currentY + lineHeightNormal * 0.06f;
    const float boxH = lineHeightNormal * 0.88f;
    addSolidQuad(boxX, boxY, boxW, boxH, active
        ? PluginUtils::applyOpacity(colors.getAccent(), 0.18f)
        : PluginUtils::applyOpacity(colors.getPrimary(), 0.10f));
    if (active) {
        const float line = lineHeightNormal * 0.06f;
        addSolidQuad(boxX, boxY + boxH - line, boxW, line, colors.getAccent());
    }
    if (cursorColumn >= 0) {
        // A bar before the column (quads draw under strings, so it never hides a glyph).
        addSolidQuad(x + cw * (static_cast<float>(cursorColumn) + 1.0f) - cw * 0.06f,
            currentY + lineHeightNormal * 0.18f, cw * 0.12f, lineHeightNormal * 0.62f, color);
    }
    char inner[64];
    snprintf(inner, sizeof(inner), "%.*s", fieldChars, text);  // cut to the field
    parent->addString(inner, x + cw, currentY, Justify::LEFT, Fonts::getNormal(), color, fontSize);
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
    endRow();   // the next row, or the next cell of a beginColumns run
}

// The track and the fill of a band: two solid quads in the given rect.
void SettingsLayoutContext::emitFillLevel(float x, float y, float width, float height,
                                          float fraction, unsigned long trackColor,
                                          unsigned long fillColor) {
    if (width <= 0.0f || height <= 0.0f) return;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    // Track first, fill over it: quads draw in push order.
    addSolidQuad(x, y, width, height, trackColor);
    addSolidQuad(x, y, width * fraction, height, fillColor);
}

void SettingsLayoutContext::addSolidQuad(float x, float y, float width, float height,
                                         unsigned long color) {
    if (width <= 0.0f || height <= 0.0f) return;
    SPluginQuad_t q;
    parent->applyOffset(x, y);
    SettingsHud::setQuadPositions(q, x, y, width, height);   // named through the friend's class: clang rejects BaseHud:: here
    q.m_iSprite = SpriteIndex::SOLID_COLOR;   // solid-quad-exempt: a fill level or a swatch is a flat rectangle by definition, not a themed button
    q.m_ulColor = color;
    parent->m_quads.push_back(q);
}

void SettingsLayoutContext::addProgressBand(float x, float y, float width, float height,
                                            float fraction, unsigned long fillColor) {
    // Faint enough for the text over it: the fill at the hover tint's alpha, the
    // track at a third of that, so an empty band still marks the entry's extent.
    emitFillLevel(x, y, width, height, fraction,
                  PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), 0.08f),
                  PluginUtils::applyOpacity(fillColor, SettingsHud::ROW_HOVER_ALPHA));
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
                                       SettingsHud::ClickRegion::Type type, float fontScale,
                                       bool enabled) {
    const float fs = fontSize * fontScale;
    parent->addString(prefix, labelX, currentY, PluginConstants::Justify::LEFT,
                      Fonts::getNormal(), ColorConfig::getInstance().getMuted(), fs);
    // The region covers the URL ONLY, not the muted label, so only the link lights up
    // and only clicking the link opens a browser.
    addLinkCell(labelX + PluginUtils::calculateMonospaceTextWidth(prefixChars, fs), url, type,
                fontScale, enabled);
    nextLine();
}

float SettingsLayoutContext::addLinkCell(float x, const char* text,
                                         SettingsHud::ClickRegion::Type type, float fontScale,
                                         bool enabled) {
    ColorConfig& colors = ColorConfig::getInstance();
    const float fs = fontSize * fontScale;
    const float w = PluginUtils::calculateMonospaceTextWidth(
        static_cast<int>(std::strlen(text)), fs);
    if (!enabled) {
        parent->addString(text, x, currentY, PluginConstants::Justify::LEFT,
                          Fonts::getNormal(), colors.getMuted(), fs);
        return w;
    }
    parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
        x, currentY, w, lineHeightNormal, type, nullptr));
    const bool hovered = parent->m_hoveredRegionIndex >= 0 &&
        parent->m_hoveredRegionIndex == static_cast<int>(parent->m_clickRegions.size()) - 1;
    parent->addString(text, x, currentY, PluginConstants::Justify::LEFT,
                      Fonts::getNormal(),
                      hovered ? PluginUtils::lightenColor(colors.getAccent(), 0.25f)
                              : colors.getAccent(), fs);
    return w;
}

void SettingsLayoutContext::addActionButtonPair(
    const char* labelA, SettingsHud::ClickRegion::Type typeA, ButtonRole roleA, bool enabledA,
    const char* labelB, SettingsHud::ClickRegion::Type typeB, ButtonRole roleB, bool enabledB,
    int labelChars, const char* tooltipIdA, const char* tooltipIdB
) {
    ColorConfig& colors = ColorConfig::getInstance();
    const ButtonRowGeom bg = buttonRow(labelChars);
    const float gap = bt.gap;
    const float pairW = bg.w * 2.0f + gap;
    const float leftX = bg.centerX - pairW / 2.0f;

    struct One { const char* label; SettingsHud::ClickRegion::Type type; ButtonRole role; bool on; float x; const char* tip; };
    const One two[2] = {
        { labelA, typeA, roleA, enabledA, leftX, tooltipIdA },
        { labelB, typeB, roleB, enabledB, leftX + bg.w + gap, tooltipIdB },
    };
    for (const One& b : two) {
        // Same region policy and same colour/state derivation as the single-button
        // path above -- no region when disabled, hue for what it does, alpha for
        // disabled/idle/hover.
        const size_t regionIndex = parent->m_clickRegions.size();
        if (b.on) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                b.x, bg.y, bg.w, bg.h, b.type, nullptr));
            if (b.tip) parent->m_clickRegions.back().tooltipId = b.tip;
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
    bool enabled,
    const char* tooltipId
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
        // On the region itself, so the same rect answers the hover highlight, the
        // click and the description. See the declaration.
        if (tooltipId) parent->m_clickRegions.back().tooltipId = tooltipId;
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

void SettingsLayoutContext::addPager(int page, int pageCount,
                                     SettingsHud::ClickRegion::Type prevType,
                                     SettingsHud::ClickRegion::Type nextType) {
    if (pageCount <= 1) return;
    ColorConfig& colors = ColorConfig::getInstance();
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "Page %d/%d", page + 1, pageCount);
    // The label's slot is sized for the LAST page's text ("Page 14/14"), not this
    // page's: centred on its own width, the buttons stepped half a character
    // when the page number gained a digit (9/14 -> 10/14).
    char slotText[16];
    snprintf(slotText, sizeof(slotText), "Page %d/%d", pageCount, pageCount);
    const float cw = charWidth();
    const float textW = cw * static_cast<float>(std::strlen(slotText));
    // Row-height buttons three characters wide, a character of air to the label:
    // the row costs what the text pager cost, so no tab grows by it.
    const float btnW = cw * 3.0f;
    const float btnH = lineHeightNormal;
    const float gap = cw;
    float x = labelX + (rowSpanWidth() - (btnW + gap + textW + gap + btnW)) * 0.5f;
    const float y = currentY;
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const int chevron = useIcons ? AssetManager::getInstance().getIconSpriteIndex("hud-angle") : 0;   // flat angle-up; "<"/">" with UI icons off
    const float halfIcon = lineHeightNormal * 0.3f;

    // One end: region (only while it can be pressed), the state fill, the chevron
    // turned to point the way (hud-angle rotated a quarter turn either way).
    auto end = [&](float bx, bool enabled, SettingsHud::ClickRegion::Type type,
                   const char* tipId, float sinYaw) {
        const size_t regionIndex = parent->m_clickRegions.size();
        if (enabled) {
            parent->m_clickRegions.push_back(SettingsHud::ClickRegion(bx, y, btnW, btnH, type, nullptr));
            parent->m_clickRegions.back().tooltipId = tipId;
        }
        const BaseHud::ButtonState state =
            !enabled ? BaseHud::ButtonState::Disabled
            : (parent->m_hoveredRegionIndex == static_cast<int>(regionIndex))
                ? BaseHud::ButtonState::Hovered
                : BaseHud::ButtonState::Idle;
        parent->addStateButton(bx, y, btnW, btnH, "", y, fontSize, colors.getAccent(), state);
        // The glyph's ink, by the same rule addStateButton applies to a label.
        const unsigned long ink = (state == BaseHud::ButtonState::Disabled)
            ? colors.getMuted()
            : parent->buttonGlyphColor(parent->buttonStateColor(colors.getAccent(), state));
        if (chevron > 0) {
            parent->addRotatedSpriteQuad(bx + btnW * 0.5f, y + btnH * 0.5f, halfIcon,
                                         0.0f, sinYaw, chevron, ink);
        } else if (!useIcons) {
            parent->addString(sinYaw < 0.0f ? "<" : ">", bx + btnW * 0.5f, y,
                              PluginConstants::Justify::CENTER, Fonts::getNormal(), ink, fontSize);
        }
    };
    end(x, page > 0, prevType, "pager.prev", -1.0f);
    x += btnW + gap;
    // The label CENTRED in its slot: a page number a digit short of the last
    // page's would otherwise sit that half-character left of the midpoint.
    parent->addString(pageText, x + textW * 0.5f, y, PluginConstants::Justify::CENTER, Fonts::getNormal(),
                      colors.getPrimary(), fontSize);
    x += textW + gap;
    end(x, page + 1 < pageCount, nextType, "pager.next", 1.0f);
    currentY += lineHeightNormal;
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
    // NO CARD IS PUSHED HERE. addPlanBackground draws one per section of both
    // columns, from the engine's own boxes, before any row is emitted.
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
    // testCardEdgesX() reports. The engine owns the derivation.
    parent->m_testContentCardLeftX = planCardLeftX;
#endif
}

void SettingsLayoutContext::finishSections() {
    closeSectionCard();
}

void SettingsLayoutContext::beginUntitledSection() {
    closeSectionCard();
    m_lastWasNote = false;
    openSectionCard();
    m_hadSection = true;
}

// The row-wide hover region every labelled row carries. Test builds record a
// row with no id, or an id with no text: tooltip_coverage_test asks for none.
void SettingsLayoutContext::addRowTooltip([[maybe_unused]] const char* label, const char* tooltipId) {
    const bool hasId = tooltipId && tooltipId[0] != '\0';
    if (hasId) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            labelX, currentY, rowSpanWidth(), lineHeightNormal, tooltipId));
    }
#if defined(MXBMRP3_TEST_BUILD)
    if (!hasId || !TooltipManager::getInstance().getControlTooltip(tooltipId)[0])
        parent->m_testUntippedRows.push_back(currentTabId + ": " + (label ? label : "") +
                                             (hasId ? std::string(" (") + tooltipId + ")" : ""));
#endif
}

void SettingsLayoutContext::addTabTooltip(const char* tabId) {
    // Store tabId and Y position for later - tooltip will be rendered by settings_hud.cpp
    // This allows control tooltips to replace tab tooltip when hovering
    currentTabId = tabId ? tabId : "";

    // The description gets its OWN section card, like every other block in the
    // content column. An un-carded block would need two hand-written vertical
    // corrections -- one giving back the pad the panel reserves for a card that
    // isn't there, one re-adding the following card's -- just to sit at the same
    // rhythm as the carded ones. A card gets that rhythm for free.
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

    addRowTooltip(label, tooltipId);

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
    const int valueWidth = valueChars();
    std::string formattedValue = formatValue(value, valueWidth, false);  // left-align for consistency
    parent->addString(formattedValue.c_str(), currentX, currentY, Justify::LEFT,
        Fonts::getNormal(), valueColor, fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow, one cell past the value - always visible, muted when disabled,
    // clickable only when enabled
    parent->addString(">", currentX + cw, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            upType, targetHud, 0, false, 0
        ));
    }

    endRow();
}

#if defined(MXBMRP3_TEST_BUILD)
// Defined beside the row it measures. ROW closing arrows only: a CYCLE/STEPPED
// pair (emitted down, then up) whose opening arrow sits at the control column --
// a table's inline cells (the Rumble effects grid) use the same types at their
// own columns. theme_geometry_test holds the min and max to the row's right edge.
int SettingsHud::testClosingArrowRightX(int* minRight, int* maxRight) const {
    auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
    int count = 0, lo = 0, hi = 0;
    for (size_t i = static_cast<size_t>(m_testContentRegionBegin) + 1; i < m_clickRegions.size(); ++i) {
        const ClickRegion& down = m_clickRegions[i - 1];
        const ClickRegion& up = m_clickRegions[i];
        const bool pair = (down.type == ClickRegion::CYCLE_DOWN && up.type == ClickRegion::CYCLE_UP)
                       || (down.type == ClickRegion::STEPPED_DOWN && up.type == ClickRegion::STEPPED_UP);
        if (!pair || q(down.x) != q(m_testControlX)) continue;
        const int r = q(up.x + up.width);
        if (count == 0 || r < lo) lo = r;
        if (count == 0 || r > hi) hi = r;
        ++count;
    }
    if (minRight) *minRight = lo;
    if (maxRight) *maxRight = hi;
    return count;
}
#endif

void SettingsLayoutContext::addCycleControl(
    const char* label,
    const char* value,
    SettingsHud::ClickRegion::Type downType,
    SettingsHud::ClickRegion::Type upType,
    const SettingsHud::ClickRegion::TargetPointer& payload,
    const char* tooltipId
) {
    // Emit the row through the plain overload, then stamp the payload onto the
    // arrow regions it created. The row tooltip is a TOOLTIP_ROW region, so it is
    // never one of them.
    const size_t firstRegion = parent->m_clickRegions.size();
    addCycleControl(label, value, downType, upType,
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
    const SettingsHud::CycleControl& control,
    BaseHud* targetHud,
    bool enabled,
    bool isOff,
    const char* tooltipId,
    bool tooltipOnArrows,
    unsigned long valueColor
) {
    // Register the descriptor for this rebuild (m_cycleControls is cleared in
    // lockstep with m_clickRegions, so the index stays valid exactly as long as
    // the regions below do). Registered even when disabled (no arrow regions),
    // keeping index assignment deterministic.
    const int cycleIndex = static_cast<int>(parent->m_cycleControls.size());
    parent->m_cycleControls.push_back(control);

    // Emit the row via the enum-pair addCycleControl overload, then tag the arrow
    // regions it created with the descriptor index (and optionally the row
    // tooltip).
    const size_t firstRegion = parent->m_clickRegions.size();
    const float rowY = currentY;   // the field, before the row moves on
    const float fieldX = controlX + charWidth() * 2.0f;
    const float fieldW = PluginUtils::calculateMonospaceTextWidth(valueChars(), fontSize);
    addCycleControl(label, value,
        SettingsHud::ClickRegion::CYCLE_DOWN,
        SettingsHud::ClickRegion::CYCLE_UP,
        targetHud, enabled, isOff, tooltipId, valueColor);
    for (size_t r = firstRegion; r < parent->m_clickRegions.size(); ++r) {
        auto& region = parent->m_clickRegions[r];
        if (region.type == SettingsHud::ClickRegion::CYCLE_UP ||
            region.type == SettingsHud::ClickRegion::CYCLE_DOWN) {
            region.cycleIndex = cycleIndex;
            if (tooltipOnArrows && tooltipId) region.tooltipId = tooltipId;
        }
    }
    // Three or more NAMED states read better as a list than as a cycle: the
    // value field becomes a dropdown box (the arrows still step). Two states
    // are a toggle in all but name, and a list of two is no help.
    if (control.nameOf && control.count >= 3) {
        addDropdownBox(fieldX, rowY, fieldW + charWidth(), cycleIndex, enabled, tooltipId);
    }
}

void SettingsLayoutContext::addFreezeControl(
    const char* label,
    int* durationMs,
    bool allowOff,
    BaseHud* targetHud,
    bool enabled,
    const char* tooltipId,
    bool allowDefault
) {
    const bool isOff = (*durationMs == 0);
    char value[16];
    if (allowDefault && *durationMs < FreezeDuration::MIN_MS) {
        strcpy_s(value, sizeof(value), "Default");
    } else if (isOff) {
        strcpy_s(value, sizeof(value), "Off");
    } else {
        snprintf(value, sizeof(value), "%ds", *durationMs / 1000);
    }
    const int lo = allowDefault ? FreezeDuration::FOLLOW_DEFAULT
                 : allowOff ? FreezeDuration::MIN_MS : FreezeDuration::PITBOARD_MIN_MS;
    addSteppedControl(label, value,
        SettingsHud::SteppedControl::wrapInt(durationMs, FreezeDuration::STEP_MS, lo, FreezeDuration::MAX_MS, targetHud),
        targetHud, enabled, isOff, tooltipId);
}

void SettingsLayoutContext::addReferenceControl(const char* label, bool* followDefault,
                                               PbGapTracker::Ref* ref, BaseHud* targetHud,
                                               const char* tooltipId, bool* enabled) {
    static const char* const kNames[] = { "Off", "Default", "Session PB", "All-time", "Last lap" };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == PbGapTracker::REF_COUNT + 2,
                  "Off, Default and one name per reference");
    // Without `enabled` the list starts at Default: kNames + 1.
    const int first = enabled ? 0 : 1;
    SettingsHud::CycleControl c;
    c.count = PbGapTracker::REF_COUNT + 2 - first;
    c.get = [followDefault, ref, enabled]() {
        if (enabled && !*enabled) return 0;
        const int on = enabled ? 1 : 0;
        return on + (*followDefault ? 0 : 1 + static_cast<int>(*ref));
    };
    c.set = [followDefault, ref, enabled](int v) {
        if (enabled) {
            *enabled = (v != 0);
            if (v == 0) return;
            --v;
        }
        *followDefault = (v == 0);
        if (v > 0) *ref = static_cast<PbGapTracker::Ref>(v - 1);
    };
    c.nameOf = [first](int i) { return std::string(kNames[first + i]); };
    c.dirtyHud = targetHud;
    const bool off = enabled && !*enabled;
    addCycleControl(label, kNames[first + c.get()], c, targetHud, true, off, tooltipId);
}

// The slider a SteppedControl draws as: the same target, bounds and step, set
// directly rather than stepped. Integer kinds round to a whole value; the
// rumble-strength percent keeps its hundredths; a linked lower bound (a rumble
// effect's Max input) is read now, as the layout is.
static SliderControl sliderFor(const SettingsHud::SteppedControl& c) {
    using Kind = SettingsHud::SteppedControl::Kind;
    SliderControl s;
    s.postStep = c.postStep;
    s.dirtyHud = c.dirtyHud;
    s.valid = c.valid;
    if (c.kind == Kind::WRAP_INT || c.kind == Kind::CLAMP_INT || c.kind == Kind::FIXED_INT) {
        int* v = c.intValue;
        s.lo = static_cast<float>(c.lo);
        s.hi = static_cast<float>(c.hi);
        s.step = static_cast<float>(c.step > 0 ? c.step : 1);
        s.get = [v]() { return v ? static_cast<float>(*v) : 0.0f; };
        s.set = [v](float x) { if (v) *v = static_cast<int>(std::lround(x)); };
        return s;
    }
    if (c.kind == Kind::ACCESSOR) {
        s.lo = c.flo; s.hi = c.fhi; s.step = c.fstep;
        s.get = c.get; s.set = c.set;
        if (c.dragSet) { s.set = c.dragSet; s.onRelease = c.onRelease; }   // held: pending until release
        return s;
    }
    float* v = c.floatValue;
    s.lo = c.loLink ? *c.loLink : c.flo;
    s.hi = c.fhi;
    s.step = c.fstep;
    s.get = [v]() { return v ? *v : 0.0f; };
    if (c.kind == Kind::PERCENT_FLOAT) {
        s.set = [v](float x) { if (v) *v = std::round(x * 100.0f) / 100.0f; };
    } else {
        s.set = [v](float x) { if (v) *v = x; };
    }
    return s;
}

void SettingsLayoutContext::addSliderTrack(float x, float rowY, float width,
                                           const SliderControl& control, bool enabled,
                                           const char* tooltipId) {
    const int sliderIndex = static_cast<int>(parent->m_sliders.size());
    parent->m_sliders.push_back(control);
    if (width <= 0.0f) return;
    ColorConfig& colors = ColorConfig::getInstance();
    const float fraction = control.get ? control.fractionOf(control.get()) : 0.0f;
    // Along the foot of the row, under the value: the row keeps its height and
    // the value its place, so a slider costs no layout.
    const float trackH = lineHeightNormal * 0.08f;
    const float trackY = rowY + lineHeightNormal * 0.90f;
    const unsigned long fill = enabled ? colors.getAccent() : colors.getMuted();
    // A signed range (a tilt, an offset) fills from its zero, not its left end.
    const float zero = (control.lo < 0.0f && control.hi > 0.0f) ? control.fractionOf(0.0f) : 0.0f;
    addSolidQuad(x, trackY, width, trackH, PluginUtils::applyOpacity(colors.getMuted(), 0.45f));
    const float from = std::min(zero, fraction), to = std::max(zero, fraction);
    addSolidQuad(x + width * from, trackY, width * (to - from), trackH, fill);
    const float knobW = charWidth() * 0.45f;
    const float knobH = lineHeightNormal * 0.30f;
    float knobX = x + width * fraction - knobW * 0.5f;
    knobX = std::max(x, std::min(knobX, x + width - knobW));
    addSolidQuad(knobX, trackY + trackH * 0.5f - knobH * 0.5f, knobW, knobH, fill);
    if (enabled) {
        SettingsHud::ClickRegion region(x, rowY, width, lineHeightNormal,
            SettingsHud::ClickRegion::SLIDER, control.dirtyHud);
        region.steppedIndex = sliderIndex;
        if (tooltipId) region.tooltipId = tooltipId;   // the row's, as on its arrows
        parent->m_clickRegions.push_back(region);
    }
}

void SettingsLayoutContext::addDropdownBox(float x, float rowY, float width, int cycleIndex,
                                           bool enabled, const char* tooltipId) {
    ColorConfig& colors = ColorConfig::getInstance();
    const float cw = charWidth();
    const float boxX = x - cw * 0.3f;
    const float boxW = width + cw * 0.3f;
    addSolidQuad(boxX, rowY + lineHeightNormal * 0.06f, boxW, lineHeightNormal * 0.88f,
        PluginUtils::applyOpacity(colors.getPrimary(), enabled ? 0.10f : 0.05f));
    const bool open = enabled && parent->m_dropdown.open == cycleIndex &&
                      parent->m_dropdown.tab == parent->m_activeTab;
    // Down while closed, up while open: the caret Rumble's split effects use,
    // in the cell after the value (unturned, the sprite points up), centred on
    // the box's height and kept off its right edge by the air the text has on
    // the left, so a value that fills the field never runs under it.
    const float caretHalf = fontSize * 0.2f;
    const float caretX = x + width - cw * 0.3f - caretHalf;
    const float caretY = rowY + lineHeightNormal * 0.5f;
    const unsigned long caretColor = enabled ? colors.getAccent() : colors.getMuted();
    // Unturned (up) while open, turned 180 (down) while closed; "^"/"v" with UI icons off.
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const int caret = useIcons ? AssetManager::getInstance().getIconSpriteIndex("caret-up") : 0;
    if (caret > 0) parent->addRotatedSpriteQuad(caretX, caretY, caretHalf, open ? 1.0f : -1.0f, 0.0f, caret, caretColor);
    else if (!useIcons) parent->addString(open ? "^" : "v", caretX, rowY, PluginConstants::Justify::CENTER,
                                          Fonts::getNormal(), caretColor, fontSize);
    if (!enabled) return;
    // The row's arrow regions are two cells wide and were pushed first, so the
    // up arrow's already covers the caret's cell (and the down arrow's the box's
    // left air): hit-testing takes the first match, and a click on the caret
    // would step the value instead of opening the list. Trim them to the box.
    const float boxR = boxX + boxW;
    for (size_t i = parent->m_clickRegions.size(); i-- > 0;) {
        auto& r = parent->m_clickRegions[i];
        if (r.y != rowY) break;   // only this row's, which are the last pushed
        if (r.type != SettingsHud::ClickRegion::CYCLE_UP &&
            r.type != SettingsHud::ClickRegion::CYCLE_DOWN) continue;
        const float rR = r.x + r.width;
        if (r.x < boxR && rR > boxR) { r.x = boxR; r.width = rR - boxR; }
        else if (r.x < boxX && rR > boxX) { r.width = boxX - r.x; }
    }
    SettingsHud::ClickRegion region(boxX, rowY, boxW, lineHeightNormal,
        SettingsHud::ClickRegion::DROPDOWN);
    region.cycleIndex = cycleIndex;
    if (tooltipId) region.tooltipId = tooltipId;   // the row's, as on its arrows
    parent->m_clickRegions.push_back(region);
    anchorDropdown(cycleIndex, boxX, rowY, boxW, x, lineHeightNormal, fontSize);
}

void SettingsLayoutContext::anchorDropdown(int cycleIndex, float x, float rowY, float width,
                                           float textX, float rowH, float listFontSize) {
    DropdownState& d = parent->m_dropdown;
    if (d.open != cycleIndex || d.tab != parent->m_activeTab) return;
    d.anchored = true;
    d.x = x;
    d.y = rowY;
    d.w = width;   // the list's narrowest; buildDropdownPopup widens it to its entries
    d.textX = textX;
    d.rowH = rowH;
    d.fontSize = listFontSize;
}

void SettingsLayoutContext::addSteppedControl(
    const char* label,
    const char* value,
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
    const float rowY = currentY;   // the field, before the row moves on
    const float fieldX = controlX + charWidth() * 2.0f;
    const float fieldW = PluginUtils::calculateMonospaceTextWidth(valueChars(), fontSize);
    addCycleControl(label, value,
        SettingsHud::ClickRegion::STEPPED_DOWN,
        SettingsHud::ClickRegion::STEPPED_UP,
        targetHud, enabled, isOff, tooltipId);
    // A bounded number is a slider; a wrapping one (a duration that runs back
    // round to Off) has no ends for a track to show, so it stays a cycle.
    if (control.kind != SettingsHud::SteppedControl::Kind::WRAP_INT) {
        addSliderTrack(fieldX, rowY, fieldW, sliderFor(control), enabled, tooltipId);
    }
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

    addRowTooltip(label, tooltipId);

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    const int valueWidth = valueChars();

    // Use override value if provided, otherwise show On/Off
    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, valueWidth, false);

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
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow, one cell past the value - always visible, muted when disabled,
    // clickable only when enabled
    parent->addString(">", currentX + cw, currentY, Justify::LEFT,
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

    endRow();
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

    addRowTooltip(label, tooltipId);

    // Render label
    parent->addString(label, labelX, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getSecondary() : colors.getMuted(), fontSize);

    float currentX = controlX;
    unsigned long valueColor = (enabled && isOn) ? colors.getPrimary() : colors.getMuted();
    const int valueWidth = valueChars();

    const char* displayValue = valueOverride ? valueOverride : (isOn ? "On" : "Off");
    std::string formattedValue = formatValue(displayValue, valueWidth, false);

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
    currentX += PluginUtils::calculateMonospaceTextWidth(valueWidth, fontSize);

    // Right arrow, one cell past the value - always visible, muted when disabled,
    // clickable only when enabled
    parent->addString(">", currentX + cw, currentY, Justify::LEFT,
        Fonts::getNormal(), enabled ? colors.getAccent() : colors.getMuted(), fontSize);
    if (enabled) {
        parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
            currentX, currentY, cw * 2, lineHeightNormal,
            toggleType, boolPtr, targetHud
        ));
    }

    endRow();
}

// The per-HUD panel-theme row: Default / None / <each installed theme>.
// "Default" (the stored value being empty) means follow Appearance > Panel Theme,
// and is what every HUD ships as -- so a user who only sets the global theme never
// has per-HUD values written, and a changed default still reaches them on upgrade.
void SettingsLayoutContext::addPerHudThemeControl(BaseHud* hud) {
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

    addCycleControl("Theme", label.c_str(), SettingsHud::themeOverrideCycle(hud),
        hud, /*enabled=*/true, /*isOff=*/false, "common.theme");
}

// The Texture row for a HUD whose art comes from an asset PACK rather than from a
// textures/ variant -- the gamepad and the pit board. Same row, same label and the
// same two arrows Radar gets; only the source of the names differs.
//
// Not the per-HUD Theme control, which is dead on exactly these two (see
// BaseHud::m_packKind). Labelled "Texture" rather than
// "Pack" deliberately: it is the same question every other HUD's row asks, and a
// second word for it would be a second concept in the same column.
// The gauges pack a gauge row is DRAWING, by display name.
//
// Two widgets, two unrelated classes, one accessor -- there is no common base
// carrying activePack() and adding one would put an AssetManager type in BaseHud
// for the benefit of two panels. So the cast is picked here, in the one place
// that needs it, rather than duplicated at both call sites.
static std::string activeGaugesDisplayName(const BaseHud* hud) {
    const GaugesAsset* pack = nullptr;
    if (const auto* tacho = dynamic_cast<const TachoWidget*>(hud)) {
        pack = tacho->activePack();
    } else if (const auto* speedo = dynamic_cast<const SpeedoWidget*>(hud)) {
        pack = speedo->activePack();
    }
    return pack ? pack->displayName : std::string("None");
}

void SettingsLayoutContext::addPackControl(BaseHud* hud) {
    const AssetManager& assets = AssetManager::getInstance();

    // The pack actually IN USE, not the stored name: an uninstalled name still draws
    // the shipped default, and the row has to say what is on screen.
    //
    // Switched on the kind rather than a bool: this WAS `isPad ? ... : ...` in four
    // places, which reads as "pad or board" and silently means "pad or NOT pad" --
    // so the third pack type would have been labelled and clicked as a pit board
    // everywhere without one line failing to compile.
    std::string label = "None";
    size_t count = 0;
    const char* tip = "pitboard.pack";
    switch (hud->m_packKind) {
        case BaseHud::PackKind::Gamepad:
            if (const GamepadAsset* a = static_cast<GamepadWidget*>(hud)->activePack())
                label = a->displayName;
            count = assets.getGamepadCount();
            tip = "gamepad.pack";
            break;
        case BaseHud::PackKind::Gauges:
            // NOT REACHED TODAY, and kept anyway. The gauges have no per-HUD tab --
            // they are rows in the Widgets table, which builds its own compact
            // pack cell further down this file. What makes this arm load-bearing
            // rather than dead is the arm BELOW it: Pitboard and None share a
            // branch that static_casts to PitboardHud*, so deleting this one does
            // not remove a case, it silently routes a gauge into the wrong cast.
            // (Its "gauges.pack" tooltip is unreachable for the same reason;
            // it is what a Gauges tab would want on the day one exists.)
            label = activeGaugesDisplayName(hud);
            count = assets.getGaugesCount();
            tip = "gauges.pack";
            break;
        case BaseHud::PackKind::Pitboard:
        case BaseHud::PackKind::None:
            if (const PitboardAsset* a = static_cast<PitboardHud*>(hud)->activePack())
                label = a->displayName;
            count = assets.getPitboardCount();
            break;
    }

    // Greyed with nothing to pick -- one pack is a legitimate install, and with no
    // Off entry there is genuinely nowhere for the arrows to go.
    addCycleControl("Texture", label.c_str(), SettingsHud::packCycle(hud),
        hud, /*enabled=*/count > 1, /*isOff=*/false, tip);
}

void SettingsLayoutContext::addStandardHudControls(BaseHud* hud) {
    // Every row is the shared helper's: row-wide tooltip region, label, then the
    // "< value >" arrows -- so the block every HUD tab opens with cannot drift
    // from the rows below it.

    // Visibility toggle. ACTIVE SURFACE, not the game flag: the click below emits
    // HUD_TOGGLE, which edits whichever surface the menu is on, so displaying
    // isVisible() would show the game's state while the click changed the
    // companion's. (The inline variant further down already reads it this way.)
    // TWO COLUMNS (beginColumns): Visible, Title and the look down the left,
    // Opacity and Scale on the right. Every HUD tab opens with this block, so
    // three rows instead of five is two rows off nearly every tab. Its labels are
    // at most seven characters, so a 9-character label column leaves the values
    // room for a theme or pack name in full ("Carbon Light", "DualShock 4").
    beginColumns(2, hud->m_titleSupported ? 5 : 4, 9);
    addToggleControl("Visible", hud->isVisibleOnActiveSurface(),
        SettingsHud::ClickRegion::HUD_TOGGLE, hud, nullptr, 0, true, "common.visible");

    // THE TITLE ROW IS ABSENT, not greyed, on a panel that cannot carry a caption
    // (BaseHud::m_titleSupported). A greyed row is right for a setting that is
    // temporarily unavailable -- it says the setting exists and hints at what would
    // enable it -- and wrong for one that does not apply to this panel at all: it reads
    // as something broken, and it spends a row on every one of these tabs. The Widgets
    // TABLE still greys its Title column rather than dropping it, because that column is
    // shared by nineteen rows and cannot be per-row.
    if (hud->m_titleSupported) {
        addToggleControl("Title", hud->getShowTitle(),
            SettingsHud::ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, true, "common.title");
    }

    // One row, two meanings, decided by what the HUD actually HAS.
    //
    // A HUD with background textures (gamepad, pitboard, pointer, radar, speedo,
    // tacho) keeps the Texture cycle: that texture IS its look, and a theme is
    // suppressed for it anyway. Every other HUD declares a texture base name with
    // no files behind it, so its Texture row was permanently "Off" and unclickable
    // -- dead UI. Those get the per-HUD Theme override instead.
    const bool hasTextures = !hud->getAvailableTextureVariants().empty();
    if (hud->m_packKind != BaseHud::PackKind::None) {
        // A PACK HUD gets a Texture row that cycles PACKS -- the same row Radar gets,
        // driven by a different source. Not the per-HUD Theme control below (which
        // it would otherwise reach: no texture base name, so hasTextures is false) --
        // that control is DEAD here: the artwork is mandatory on these HUDs and
        // artwork suppresses the theme, so nothing it offers can take effect.
        addPackControl(hud);
    } else if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
        addPerHudThemeControl(hud);
    } else {
        // Background texture variant cycle (Off, 1, 2, ...). Greyed without textures.
        // The value keeps primary for "Off" while textures exist (isOff=false), as
        // this row always has.
        char textureValue[16];
        const int variant = hud->getTextureVariant();
        if (!hasTextures || variant == 0) {
            snprintf(textureValue, sizeof(textureValue), "Off");
        } else {
            snprintf(textureValue, sizeof(textureValue), "%d", variant);
        }
        addCycleControl("Texture", textureValue, SettingsHud::textureCycle(hud),
            hud, /*enabled=*/hasTextures, /*isOff=*/false, "common.texture");
    }

    addOpacityControl(hud);
    addScaleControl(hud);
    endColumns();
}

// A HUD's background opacity (0-100%) and scale (10-300%), 1% a step: one pair of
// descriptors for the rows and the Widgets table's cells.
static SettingsHud::SteppedControl opacityStepper(BaseHud* hud) {
    return SettingsHud::SteppedControl::accessor(
        [hud]() { return hud->getBackgroundOpacity(); },
        [hud](float v) { hud->setBackgroundOpacity(v); }, 0.01f, 0.0f, 1.0f, hud);
}

static SettingsHud::SteppedControl scaleStepper(BaseHud* hud) {
    return SettingsHud::SteppedControl::accessor(
        [hud]() { return hud->getOwnScale(); },
        [hud](float v) { hud->setScale(v); }, 0.01f, 0.1f, 3.0f, hud);
}

void SettingsLayoutContext::addOpacityControl(BaseHud* hud, bool enabled) {
    char value[16];
    snprintf(value, sizeof(value), "%d%%", static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f)));
    addSteppedControl("Opacity", value, opacityStepper(hud), hud, enabled, false, "common.opacity");
}

void SettingsLayoutContext::addScaleControl(BaseHud* hud, bool enabled) {
    char value[16];
    snprintf(value, sizeof(value), "%d%%", static_cast<int>(std::round(hud->getOwnScale() * 100.0f)));
    addSteppedControl("Scale", value, scaleStepper(hud), hud, enabled, false, "common.scale");
}

void SettingsLayoutContext::nextLine() {
    currentY += lineHeightNormal;
}

void SettingsLayoutContext::addSpacing() {
    // THE JUNCTION GAP, [Advanced] panelGap / a theme's [panel] gap — the term
    // already named for "air between a panel's stacked children", which is
    // exactly what a caller is asking for here.
    //
    // Converted the BOX way (cellW * aspect), not on cellH, so one stated cell is
    // square on screen wherever it is spent.
    currentY += static_cast<float>(parent->panelGapCells())
              * layout().cellW * PluginConstants::UI_ASPECT_RATIO * parent->getScale();
}

SettingsLayoutContext::WidgetColumns SettingsLayoutContext::widgetColumns() const {
    // Name, then Visible / Title (3-char values), Texture (the remainder),
    // Opacity / Scale (4-char percentages): every control is value + 4 wide.
    constexpr int NAME_CHARS = 10;
    constexpr int GAP_CHARS = 2;
    constexpr int TOGGLE_CHARS = 3 + 4;
    constexpr int PERCENT_CHARS = 4 + 4;
    const int fixedChars = NAME_CHARS + 2 * (TOGGLE_CHARS + GAP_CHARS)
        + 4 + GAP_CHARS + (PERCENT_CHARS + GAP_CHARS) + PERCENT_CHARS;
    WidgetColumns cols;
    cols.texChars = std::max(3, layout().settingsContentAreaChars() - fixedChars);
    const float cw = charWidth();
    cols.visX = labelX + cw * NAME_CHARS;
    cols.titleX = cols.visX + cw * (TOGGLE_CHARS + GAP_CHARS);
    cols.texX = cols.titleX + cw * (TOGGLE_CHARS + GAP_CHARS);
    cols.opacityX = cols.texX + cw * (cols.texChars + 4 + GAP_CHARS);
    cols.scaleX = cols.opacityX + cw * (PERCENT_CHARS + GAP_CHARS);
    return cols;
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
    ColorConfig& colors = ColorConfig::getInstance();

    const float nameX = labelX;
    const WidgetColumns cols = widgetColumns();

    addRowTooltip(name, tooltipId);

    // Widget name, in the row-label colour every other settings row uses
    parent->addString(name, nameX, currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getSecondary(), fontSize);


    // Visibility toggle (shows actual value, grayed out when disabled). The pointer
    // row is special: its toggle drives the menu-only-cursor mode, not the widget's
    // real visibility (On = pointer summoned by mouse movement during play; Off =
    // menu-only). The pointer's m_bVisible must stay true so it can still draw in the
    // settings menu, so it can't be the toggle target.
    if (menuOnlyPointerRow) {
        bool pointerOn = !UiConfig::getInstance().getMenuOnlyCursor();
        addInlineCycle(cols.visX, pointerOn ? "On" : "Off", 3,
            SettingsHud::ClickRegion::MENU_ONLY_CURSOR_TOGGLE,
            SettingsHud::ClickRegion::MENU_ONLY_CURSOR_TOGGLE, hud, true, !pointerOn);
    } else {
        // Show the ACTIVE surface's visibility, like the per-HUD tabs: on the companion
        // window the toggle already edits the companion instance (HUD_TOGGLE routes by
        // active surface), so the displayed On/Off must read it too — otherwise a widget
        // enabled only on the companion still shows the game's state.
        bool visOn = hud->isVisibleOnActiveSurface();
        addInlineCycle(cols.visX, visOn ? "On" : "Off", 3,
            SettingsHud::ClickRegion::HUD_TOGGLE, SettingsHud::ClickRegion::HUD_TOGGLE,
            hud, enableVisibility, !visOn);
    }

    // Title toggle, greyed on a widget that carries no caption. The column is shared by
    // every row, so unlike the per-HUD tabs it cannot be dropped -- and it does not need
    // to be, since a whole column of On/Off values reads as a comparison rather than as
    // a broken control. getShowTitle() is already forced false there (see
    // BaseHud::m_titleSupported), so there is no stale value to mask.
    const bool enableTitle = hud->m_titleSupported;
    const bool titleOn = hud->getShowTitle();
    addInlineCycle(cols.titleX, titleOn ? "On" : "Off", 3,
        SettingsHud::ClickRegion::TITLE_TOGGLE, SettingsHud::ClickRegion::TITLE_TOGGLE,
        hud, enableTitle, !titleOn);

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
        const AssetManager& assets = AssetManager::getInstance();
        // No "Off": the pack artwork IS the widget, so the cycle is packs only (see
        // BaseHud::m_textureRequired).
        //
        // Pack HUD != gamepad: the gauges are the second pack widget in this table.
        const bool isGauges = (hud->m_packKind == BaseHud::PackKind::Gauges);
        const size_t packCount =
            isGauges ? assets.getGaugesCount() : assets.getGamepadCount();
        std::string packValue = "None";
        if (isGauges) {
            packValue = activeGaugesDisplayName(hud);
        } else if (const GamepadAsset* active =
                       static_cast<const GamepadWidget*>(hud)->activePack()) {
            packValue = active->displayName;
        }
        // Needs somewhere to GO now: with one pack installed and no Off entry the
        // arrows would step from a pack to itself.
        addInlineCycleControl(cols.texX, packValue.c_str(), cols.texChars,
            SettingsHud::packCycle(hud), hud, enableBgTexture && packCount > 1,
            isGauges ? "gauges.pack" : "gamepad.pack");
    } else if (!hasTextures && AssetManager::getInstance().getThemeCount() > 0) {
        const std::string& ov = hud->getThemeOverride();
        std::string themeValue;
        if (ov.empty()) {
            themeValue = "Default";
        } else if (ov == BaseHud::THEME_NONE) {
            themeValue = "None";
        } else if (const ThemeAsset* t = AssetManager::getInstance().getThemeByName(ov)) {
            themeValue = t->displayName;
        } else {
            themeValue = "Default";   // unknown name renders as the global theme
        }
        addInlineCycleControl(cols.texX, themeValue.c_str(), cols.texChars,
            SettingsHud::themeOverrideCycle(hud), hud, enableBgTexture, "common.theme");
    } else {
        char texValue[8];
        int texVariant = hud->getTextureVariant();
        snprintf(texValue, sizeof(texValue), (!hasTextures || texVariant == 0) ? "Off" : "%d", texVariant);
        addInlineCycleControl(cols.texX, texValue, cols.texChars,
            SettingsHud::textureCycle(hud), hud, enableBgTexture && hasTextures, "common.texture");
    }

    // BG Opacity (shows muted value without arrows when disabled)
    char opacityValue[16];
    int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
    snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
    addInlineSteppedControl(cols.opacityX, opacityValue, 4, opacityStepper(hud), hud, enableOpacity, "common.opacity");

    // Scale (shows muted value without arrows when disabled)
    char scaleValue[16];
    int scalePercent = static_cast<int>(std::round(hud->getOwnScale() * 100.0f));
    snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
    addInlineSteppedControl(cols.scaleX, scaleValue, 4, scaleStepper(hud), hud, enableScale, "common.scale");

    currentY += lineHeightNormal;
}

// Get icon display name from shape index (0 = Off)
std::string getShapeDisplayName(int shapeIndex) {
    if (shapeIndex <= 0) return "Off";
    const auto& assetMgr = AssetManager::getInstance();
    // Base index: this asks for the icon's NAME, which a theme override does not change.
    int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
    std::string name = assetMgr.getIconDisplayName(spriteIndex);
    if (name.empty()) return "Unknown";
    return name;
}
