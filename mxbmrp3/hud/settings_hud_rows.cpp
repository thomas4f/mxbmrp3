// ============================================================================
// hud/settings_hud_rows.cpp
// SettingsHud's per-row overlays drawn after a tab's rows: the what's-new row
// bands, the hovered-row highlight, and the tab-overflow check. Split out of
// settings_hud_render.cpp verbatim.
// ============================================================================
#include "settings_hud.h"
#include "settings/whats_new.h"
#include "../core/color_config.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"

#include <algorithm>

// A row highlight's span: the column's band (plan.rowBandX/W) for a whole row, cut
// to the cell for a GRID CELL (ClickRegion::cellIndex/cellCount).
static void rowBandSpan(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol,
                        const SettingsHud::ClickRegion& r,
                        const std::vector<SettingsHud::ClickRegion>& regions, float& x, float& w) {
    const float left = plan.rowBandX(mainCol);
    const float right = left + plan.rowBandW(mainCol);
    if (r.cellCount <= 1) {
        x = left;
        w = right - left;
        return;
    }
    // A cell's region is its own span (SettingsLayoutContext::endRow). Its inner
    // edges take the margin a whole row's band has past the rows on the left, so
    // the band sits on the cell as a row's sits on the row; the outer ends are the
    // column's band, so the first and last cell line up with every whole-row band.
    const float margin = std::max(0.0f, plan.colContentX(mainCol) - left);
    const float cellLeft = (r.cellIndex == 0) ? left : r.x - margin;
    float cellRight = (r.cellIndex == r.cellCount - 1) ? right : r.x + r.width + margin;
    // NEVER SHORTER THAN WHAT THE CELL DRAWS. The span above is the grid's, but a
    // cell's controls can run past it (a hotkey cell's pad field, a closing arrow
    // in a wider font): reach the right end of every control that starts in the
    // cell on this row, plus the same margin.
    for (const SettingsHud::ClickRegion& c : regions) {
        if (c.type == SettingsHud::ClickRegion::TOOLTIP_ROW ||
            c.type == SettingsHud::ClickRegion::DROPDOWN_OPTION) continue;
        const float cy = c.y + c.height * 0.5f;
        if (cy < r.y || cy >= r.y + r.height) continue;
        if (c.x < r.x || c.x >= r.x + r.width) continue;
        cellRight = std::max(cellRight, c.x + c.width + margin);
    }
    x = cellLeft;
    w = cellRight - cellLeft;
}

void SettingsHud::addWhatsNewRowBands(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol) {
    // THE WHAT'S-NEW ROW BANDS, in one pass over what the tab just registered.
    //
    // Here rather than inside every row helper because a row's identity is its
    // row-wide tooltip region, and by now they all exist -- one loop marks any
    // row on any tab, and no helper needs to know this feature exists.
    //
    // Drawn AFTER the rows and still behind them: the plugin API takes quads and
    // strings as two arrays, so every quad draws before every string whatever
    // order they were pushed in (see HudManager::draw). The band cannot cover the
    // label it is pointing at.
    //
    // The POSITIVE colour, matching the "New" tag on the tab that led the player
    // here -- one colour for the whole trail, tag to row. Not WARNING, which this
    // plugin spends everywhere else on "careful": a band in it reads as a problem
    // with the row rather than as the thing worth looking at, and is
    // indistinguishable from the Beta caveat two tabs down.
    //
    // At the same alpha the hover band uses, so it reads as "look here" rather
    // than as a selection -- and so it disappears under the hover band the moment
    // the pointer arrives, which is also when it is dismissed.
    // SPANNED FROM THE PLAN (rowBandX/W), not from the region's own rect. A
    // highlight is a property of the COLUMN, not of the control in it: a row that
    // builds its tooltip region by hand gets a different rect from one that went
    // through the layout helpers, so a band spanned from the region changes
    // width by tab and does not line up with the accent band that replaces it on
    // hover.
    // THE ROW'S OWN REGION ONLY: a cycler's arrows carry the row's tooltip id too
    // (so hovering them explains the row), and banding those as well spanned the
    // whole row across a two-column grid, covering the neighbouring cell.
#if defined(MXBMRP3_TEST_BUILD)
    m_testWhatsNewBands = 0;
#endif
    for (const ClickRegion& r : m_clickRegions) {
        if (r.type != ClickRegion::TOOLTIP_ROW || r.tooltipId.empty()) continue;
        if (!WhatsNew::liveForRow(m_activeTab, r.tooltipId.c_str())) continue;
#if defined(MXBMRP3_TEST_BUILD)
        ++m_testWhatsNewBands;
#endif
        float bandX, bandW;
        rowBandSpan(plan, mainCol, r, m_clickRegions, bandX, bandW);
        addRowHighlight(bandX, r.y, bandW, r.height,
                        PluginUtils::applyOpacity(
                            ColorConfig::getInstance().getPositive(), ROW_HOVER_ALPHA));
    }
}

void SettingsHud::checkTabOverflow(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol,
                                   const ScaledDimensions& dim, float currentY) {
    // HOW FAR THE TAB OVERRAN THE SPACE RESERVED FOR IT, in rows -- negative is
    // slack, and it should ALWAYS be negative: the height is measured from the
    // tallest tab, so this one had room by construction.
    //
    // Plus the last card's bottom pad, which finishSections() drew BELOW
    // currentY: the cursor stops on the last row, the card does not, and it is
    // the CARD the footer buttons collide with.
    //
    // It is kept because "by construction" has one failure mode left: a renderer
    // that lays out differently between the measure pass and this one -- reading
    // the panel's own height, say. The warning is for a player's log, the number
    // for CI (settings_fit_test reads it for every tab).
    // WHERE THE COLUMN'S LAST SECTION ENDS, straight off the engine -- what the
    // tab was given.
    const float contentLimit = mainCol.sections.empty()
        ? plan.Y(plan.g.btnTop)
        : plan.Y(mainCol.sections.back().bot);
    const float overflow = (currentY + cardPadBotY() - contentLimit)
                         / dim.lineHeightNormal;
#if defined(MXBMRP3_TEST_BUILD)
    m_testOverflowRows = overflow;
#endif
    // A HUNDREDTH OF A ROW OF TOLERANCE, because the measure pass runs at the
    // origin and this one at the panel's real Y: the tallest tab has no slack by
    // construction, so float rounding alone left it "overflowing by 0.0 rows"
    // on every frame (2,339 log lines in 40 seconds on the Stream Chat tab). A real
    // overrun is at least a row. And ONCE per tab, not per rebuild.
    if (overflow > 0.01f) {
        if (m_overflowWarnedTab != m_activeTab) {
            m_overflowWarnedTab = m_activeTab;
            DEBUG_WARN_F("Settings tab %d overflows the panel by %.1f rows -- it "
                         "measured shorter than it drew, so a tab renderer is not "
                         "reproducible", m_activeTab, overflow);
        }
    } else if (m_overflowWarnedTab == m_activeTab) {
        m_overflowWarnedTab = -1;
    }
}

void SettingsHud::addHoveredRowHighlight(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol) {
    if (m_hoveredRegionIndex >= 0 && m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size())) {
        const ClickRegion& hoveredRegion = m_clickRegions[m_hoveredRegionIndex];
        if (hoveredRegion.type == ClickRegion::TOOLTIP_ROW) {
            // THE CONTENT COLUMN, from the plan (rowBandX/W) -- the same band the
            // sidebar, StandingsHud and RecordsHud span, inset by [content] padding
            // like theirs rather than taking the card's interior.
            //
            // Not the region's own x and width: that ties the decoration to a
            // hit-test rectangle, so every row that builds its region by hand
            // highlights to a different width from the rows that went through the
            // layout helpers. A highlight is a property of the column, not of the
            // control in it. The plan also answers the themed card's clamp against
            // the frame, so the themed case needs no second expression.
            //
            // A GRID CELL (one of several controls sharing the row) lights only its
            // own cell -- see rowBandSpan.
            float bandX, bandW;
            rowBandSpan(plan, mainCol, hoveredRegion, m_clickRegions, bandX, bandW);
            addRowHighlight(bandX, hoveredRegion.y, bandW,
                            hoveredRegion.height,
                            PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(),
                                                      ROW_HOVER_ALPHA));
        }
    }
}
