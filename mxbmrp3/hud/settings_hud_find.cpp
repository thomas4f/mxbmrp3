// ============================================================================
// hud/settings_hud_find.cpp
// The settings panel's Find box: the field in the title band, the results page
// (TAB_FIND, hidden from the tab list) and the jump to a result's row.
//
// THE FIELD sits in the title band's right end, in the band the caption already
// reserves, so the panel keeps its size. Clicking it opens the results page and
// types into the shared text capture (HotkeyManager, TextKeys::SEARCH); a second
// click or Enter stops typing, Esc empties it and goes back to the tab it was
// opened from.
//
// THE INDEX (settings/settings_find.h) is every tab laid out once with its
// drawing thrown away, collected through addRowTooltip. Built when the box opens
// or the menu reopens -- never per frame -- so a value shown is the value the row
// had when the box was opened. Every listed tab is also a result of its own, so
// a HUD's name finds the HUD first; Achievements is that and no more: its rows
// are a catalogue, not settings.
//
// A RESULT is a link, not a control: clicking it opens its tab with the row lit
// as if hovered (addHoveredRowHighlight) until the pointer moves.
//
// Pinned by tests/unit/test_settings_find.cpp (match and rank) and
// tests/integration/tests/settings_find_test.cpp (index, page fit, the jump).
// ============================================================================
#include "settings_hud.h"
#include "settings/settings_layout.h"
#include "settings/whats_new.h"
#include "../core/color_config.h"
#include "../core/hotkey_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/text_edit.h"
#include "../core/tooltip_manager.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace PluginConstants;

namespace {
constexpr size_t MAX_QUERY = 24;      // typed characters
constexpr int FIELD_TEXT_CHARS = 9;   // visible characters of the query
constexpr int FIELD_CHARS = 2 + FIELD_TEXT_CHARS + 4;   // icon, text, the hit count
constexpr int ROWS_PER_PAGE = 18;     // results per page; settings_find_test holds the fit
constexpr int VALUE_CHARS = 20;       // a long value ("Default (Session PB)") is cut here
}  // namespace

void SettingsHud::openFind() {
    HotkeyManager& hk = HotkeyManager::getInstance();
    if (m_textField == TextField::FIND && hk.isCapturing() && hk.getCaptureType() == CaptureType::TEXT) {
        hk.commitTextCapture();   // a second click is Enter
        return;
    }
    hk.cancelCapture();
    if (m_activeTab != TAB_FIND) {
        m_find.returnTab = m_activeTab;
        m_activeTab = TAB_FIND;
    }
    m_find.indexStale = true;   // values follow the settings as they are now
    m_find.landTooltipId.clear();
    m_textField = TextField::FIND;
    hk.startTextCapture(m_find.query, MAX_QUERY, TextKeys::SEARCH);
    disarmResets();
    rebuildRenderData();
}

void SettingsHud::closeFind() {
    m_find.query.clear();
    m_find.page = 0;
    if (m_activeTab == TAB_FIND) {
        m_activeTab = isTabAvailable(m_find.returnTab) ? m_find.returnTab : TAB_GENERAL;
    }
}

void SettingsHud::buildFindIndex(const ScaledDimensions& dim) {
    // Registry order is sidebar order, and the descriptors live in one array, so
    // sorting them by address walks the sidebar without reaching for the array.
    const TabDescriptor* tabs[TAB_COUNT];
    int count = 0;
    for (int t = 0; t < TAB_COUNT; ++t) {
        const TabDescriptor* d = findTabDescriptor(t);
        if (!d || !d->render || d->hidden || !isTabAvailable(t)) continue;
        tabs[count++] = d;
    }
    std::sort(tabs, tabs + count);

    // The same relative columns the panel measures with (planSettingsPanel).
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    const float labelToControl = PluginUtils::calculateMonospaceTextWidth(SETTINGS_CONTROL_COLUMN, dim.fontSize);
    const float labelToRight = PluginUtils::calculateMonospaceTextWidth(
        layout().settingsControlColumn - layout().settingsLabelColumn, dim.fontSize);
    const float contentAsk = charW * static_cast<float>(layout().settingsContentAreaChars());

    m_find.index.clear();
    m_find.index.reserve(400);
    m_find.indexing = true;
    for (int i = 0; i < count; ++i) {
        const int tabId = tabs[i]->tabId;
        const size_t tabEntry = m_find.index.size();
        m_find.addTab(tabId, getTabName(tabId), tabs[i]->tooltipId,
                      TooltipManager::getInstance().getTabTooltip(tabs[i]->tooltipId));
        if (tabId == TAB_ACHIEVEMENTS) continue;   // the tab, but not its catalogue
        m_find.section[0] = '\0';   // a tab's first rows may sit under no heading
        m_find.tabName = getTabName(tabId);   // what the results' Tab column shows
        m_find.widgetRows = tabId == TAB_WIDGETS;
        measureTab(tabId, dim, 0.0f, labelToControl, labelToRight, 0.0f, contentAsk, contentAsk);
        m_find.tabVisible(tabEntry);
    }
    m_find.indexing = false;
    m_find.widgetRows = false;
    m_find.openRow = -1;
    m_find.indexStale = false;
    m_find.matchedFresh = false;
    DEBUG_INFO_F("Settings Find: indexed %zu rows on %d tabs", m_find.index.size(), count);
}

void SettingsHud::buildFindField(const ScaledDimensions& dim, const PanelPlan& plan,
                                 const PanelBox::ColumnGeom& mainCol) {
    ColorConfig& cc = ColorConfig::getInstance();
    const HotkeyManager& hk = HotkeyManager::getInstance();
    const bool typing = m_textField == TextField::FIND && hk.isCapturing() &&
                        hk.getCaptureType() == CaptureType::TEXT;
    const bool showing = m_activeTab == TAB_FIND && !m_find.query.empty();
    // A result's tab keeps the query in the box, dimmed: the jump is "take me
    // there", not "done searching". Clicking the box brings the results back,
    // Esc clears it.
    const bool held = !showing && !m_find.query.empty();
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // Right-aligned to the content column, centred in the title band. addInputField
    // draws its box from 0.7 of a cell past x to 0.3 past the last character.
    const float right = plan.X(mainCol.cardLeft + mainCol.cardW);
    const float x = right - cw * (static_cast<float>(FIELD_CHARS) + 1.0f);
    const float y = plan.Y(plan.g.titleTop) + (plan.H(plan.g.titleH) - dim.lineHeightNormal) * 0.5f;

    const size_t region = m_clickRegions.size();
    const bool hovered = m_hoveredRegionIndex == static_cast<int>(region);
    ClickRegion field(x + cw * 0.7f, y, cw * (static_cast<float>(FIELD_CHARS) + 0.3f), dim.lineHeightNormal,
                      ClickRegion::FIND_FIELD);
    field.tooltipId = "find.field";
    m_clickRegions.push_back(field);

    // Two cells for the icon, then the text -- drawn at its own x, not padded with
    // spaces: under a proportional theme font two spaces are narrower than the icon.
    // The caret sits before its column, as in the Stream Chat fields.
    char inner[48];
    int cursorColumn = -1;
    unsigned long color;
    if (typing) {
        const std::string& text = hk.getCaptureText();
        const size_t cursor = hk.getCaptureCursor();
        const size_t from = TextEdit::viewStart(text.size(), cursor, FIELD_TEXT_CHARS);
        snprintf(inner, sizeof(inner), "%.*s",
                 static_cast<int>(std::min(text.size() - from, static_cast<size_t>(FIELD_TEXT_CHARS))),
                 text.c_str() + from);
        cursorColumn = 2 + static_cast<int>(cursor - from);
        color = cc.getAccent();
    } else if (showing) {
        snprintf(inner, sizeof(inner), "%.*s", FIELD_TEXT_CHARS, m_find.query.c_str());
        color = hovered ? cc.getAccent() : cc.getPrimary();
    } else if (held) {
        snprintf(inner, sizeof(inner), "%.*s", FIELD_TEXT_CHARS, m_find.query.c_str());
        color = hovered ? cc.getAccent() : cc.getMuted();
    } else {
        snprintf(inner, sizeof(inner), "Find");
        color = hovered ? cc.getAccent() : cc.getMuted();
    }
    SettingsLayoutContext ctx(this, dim, x, x, x, x, cw * static_cast<float>(FIELD_CHARS), right, y);
    ctx.addInputField(x, FIELD_CHARS, "", color, typing, cursorColumn);
    // The release's "New" band, like a marked row's, until the pointer reaches the
    // field (the hover in settings_hud.cpp dismisses it). It is the Find tab's
    // marker, so the test count takes it only there: the field is that tab's row.
    if (WhatsNew::liveForRow(TAB_FIND, "find.field")) {
        addRowHighlight(field.x, y, field.width, dim.lineHeightNormal,
                        PluginUtils::applyOpacity(cc.getPositive(), ROW_HOVER_ALPHA));
#if defined(MXBMRP3_TEST_BUILD)
        if (m_activeTab == TAB_FIND) ++m_testWhatsNewBands;
#endif
    }
    addString(inner, x + cw * 3.0f, y, Justify::LEFT, Fonts::getNormal(), color, dim.fontSize);
    drawTabIcon(x + cw * 1.0f, y, "magnifying-glass", typing ? cc.getAccent() : color, dim, cw * 1.5f);
    if (typing || showing) {
        char hits[8];
        snprintf(hits, sizeof(hits), "%d", static_cast<int>(std::min<size_t>(m_find.hits.size(), 999)));
        addString(hits, right - cw * 0.5f, y, Justify::RIGHT, Fonts::getNormal(), cc.getMuted(), dim.fontSize);
    }
}

BaseHud* SettingsHud::renderTabFind(SettingsLayoutContext& ctx) {
    SettingsHud& self = *ctx.parent;
    SettingsFind::State& f = self.m_find;
    const HotkeyManager& hk = HotkeyManager::getInstance();
    if (self.m_textField == TextField::FIND && hk.isCapturing() && hk.getCaptureType() == CaptureType::TEXT) {
        f.query = hk.getCaptureText();
    }
    ctx.addTabTooltip("find");
    // Never the tab that sets the panel's height: its pages are cut to fit the others.
    if (self.m_measuringTallest) return nullptr;

    if (f.query.find_first_not_of(' ') == std::string::npos) {
        ctx.addSectionHeading("Find");
        ctx.addNote("Type a setting's name, its tab, or a word from its description.");
        return nullptr;
    }
    f.refresh();
    const int total = static_cast<int>(f.hits.size());
    if (total == 0) {
        ctx.addSectionHeading("No matches");
        ctx.addNote("No setting is called that or mentions it. Try a shorter word.");
        return nullptr;
    }
    const int pageCount = (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    f.page = std::clamp(f.page, 0, pageCount - 1);

    ColorConfig& cc = ColorConfig::getInstance();
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    const int first = f.page * ROWS_PER_PAGE;
    const int last = std::min(first + ROWS_PER_PAGE, total);
    for (int i = first; i < last; ++i) {
        // Each group's heading where the group starts (or where a page does).
        struct Group { const char* title; int count; };
        const Group groups[] = {
            { "Tabs", f.counts.tabs }, { "Widgets", f.counts.widgets }, { "Names", f.counts.names },
            { "By tab or section", f.counts.places }, { "Descriptions", f.counts.descriptions },
        };
        for (int g = 0, start = 0; g < 5; start += groups[g].count, ++g) {
            if (groups[g].count == 0 || i < start || i >= start + groups[g].count) continue;
            if (i == first || i == start) {
                char heading[32];
                snprintf(heading, sizeof(heading), "%s (%d)", groups[g].title, groups[g].count);
                ctx.addSectionHeading(heading);
            }
        }
        const SettingsFind::Entry& e = f.index[static_cast<size_t>(f.hits[static_cast<size_t>(i)])];
        const float x = ctx.labelX;
        const float y = ctx.currentY;

        // The row-wide hover region first (the band and the row's own tooltip);
        // clicks skip it, so the result region below takes them.
        self.m_clickRegions.push_back(ClickRegion(x, y, ctx.rowSpanWidth(), ctx.lineHeightNormal, e.tooltipId));
        ClickRegion result(x, y, ctx.rowSpanWidth(), ctx.lineHeightNormal, ClickRegion::FIND_RESULT);
        result.tabIndex = e.tab;
        result.tooltipId = e.tooltipId;
        self.m_clickRegions.push_back(result);

        // The label, with every typed word's letters in the accent. Drawn as PREFIXES
        // of the label, longest first, each in the colour of the run it ends with:
        // every prefix starts at x, so its glyphs land where the full label's do in
        // any font -- placing runs at `x + cw * at` assumed monospace, and a
        // proportional theme font opened gaps mid-label.
        bool lit[sizeof(e.label)] = {};
        const size_t len = std::strlen(e.label);
        const char* p = f.query.c_str();
        while (*p) {
            while (*p == ' ') ++p;
            const char* w = p;
            while (*p && *p != ' ') ++p;
            const size_t n = static_cast<size_t>(p - w);
            for (size_t at = 0; n > 0 && at + n <= len; ++at) {
                if (SettingsFind::startsWithWord(e.label + at, w, n)) {
                    for (size_t k = 0; k < n; ++k) lit[at + k] = true;
                }
            }
        }
        for (size_t end = len; end > 0;) {
            size_t start = end - 1;
            while (start > 0 && lit[start - 1] == lit[end - 1]) --start;
            char prefix[sizeof(e.label)];
            snprintf(prefix, sizeof(prefix), "%.*s", static_cast<int>(end), e.label);
            self.addString(prefix, x, y, Justify::LEFT, Fonts::getNormal(),
                           lit[end - 1] ? cc.getAccent() : cc.getSecondary(), ctx.fontSize);
            end = start;
        }
        // Where it lives ("Gap Bar > Layout") at the row's end, and the value where a
        // row shows it, muted and without arrows (read-only), cut short of the former.
        char where[48];
        if (e.isTab) snprintf(where, sizeof(where), "Tab");   // the label names which
        else if (e.section[0]) snprintf(where, sizeof(where), "%s > %s", self.getTabName(e.tab), e.section);
        else snprintf(where, sizeof(where), "%s", self.getTabName(e.tab));
        self.addString(where, x + ctx.rowSpanWidth(), y, Justify::RIGHT,
                       Fonts::getNormal(), cc.getMuted(), ctx.fontSize);
        const int rowChars = static_cast<int>(ctx.rowSpanWidth() / cw);
        const int valueChars = std::min(VALUE_CHARS,
            rowChars - static_cast<int>(std::strlen(where)) - 1 - (SETTINGS_CONTROL_COLUMN + 2));
        if (e.value[0] && valueChars > 0) {
            char value[VALUE_CHARS + 1];
            snprintf(value, sizeof(value), "%.*s", valueChars, e.value);
            self.addString(value, ctx.controlX + cw * 2.0f, y, Justify::LEFT, Fonts::getNormal(),
                           cc.getMuted(), ctx.fontSize);
        }
        ctx.currentY += ctx.lineHeightNormal;
    }
    ctx.addPager(f.page, pageCount, ClickRegion::FIND_PAGE_PREV, ClickRegion::FIND_PAGE_NEXT);
    return nullptr;
}

bool SettingsHud::handleClickTabFind(const ClickRegion& region) {
    if (region.type == ClickRegion::FIND_PAGE_PREV || region.type == ClickRegion::FIND_PAGE_NEXT) {
        m_find.page += region.type == ClickRegion::FIND_PAGE_NEXT ? 1 : -1;
        rebuildRenderData();
        return true;
    }
    if (region.type != ClickRegion::FIND_RESULT) return false;
    // Copied: opening the tab rebuilds the regions `region` lives in.
    ClickRegion tab(region.x, region.y, region.width, region.height, ClickRegion::TAB);
    tab.tabIndex = region.tabIndex;
    // The landing row stays lit until the pointer leaves the spot it was clicked from.
    CursorPosition cursor = InputManager::getInstance().getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    m_find.landX = cursor.x;
    m_find.landY = cursor.y;
    // A tab result has no row to light: just open the tab.
    const TabDescriptor* d = findTabDescriptor(region.tabIndex);
    const bool isTab = d && d->tooltipId && region.tooltipId == d->tooltipId;
    m_find.landTooltipId = isTab ? std::string() : region.tooltipId;
    m_hoveredRegionIndex = -1;
    m_hoveredTooltipId = region.tooltipId;
    handleTabClick(tab);   // cancels the typing, persists the tab, rebuilds
    return true;
}

#if defined(MXBMRP3_TEST_BUILD)
void SettingsHud::testFindQuery(const char* q) {
    HotkeyManager::getInstance().cancelCapture();
    m_textField = TextField::NONE;
    if (!q[0]) {
        closeFind();
        rebuildRenderData();
        return;
    }
    if (m_activeTab != TAB_FIND) m_find.returnTab = m_activeTab;
    m_activeTab = TAB_FIND;
    m_find.query = q;
    m_find.indexStale = true;
    rebuildRenderData();
}
#endif
