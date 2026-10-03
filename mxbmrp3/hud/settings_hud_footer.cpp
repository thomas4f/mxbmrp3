// ============================================================================
// hud/settings_hud_footer.cpp
// The settings panel's footer button row: [Reset <Tab>] on the left, [Save /
// Saved] [Close] centred, [About] on the right. Part of SettingsHud::
// rebuildRenderData() (settings_hud_render.cpp), split out so that file keeps
// to its budget; click handling is settings_hud_input.cpp.
// ============================================================================
#include "settings_hud.h"
#include "../core/color_config.h"
#include "../core/plugin_utils.h"
#include "../core/settings_manager.h"
#include <cstdio>
#include <cstring>

void SettingsHud::buildFooterButtons(const ScaledDimensions& dim, const PanelPlan& plan,
                                     const PanelBox::ColumnGeom& sideCol, const PanelBox::ColumnGeom& mainCol,
                                     float startX, float panelWidth) {
    // Bottom button row - always [Save/Saved] [Close]. The Save button reflects unsaved changes:
    // lit + clickable ("Save") when there are pending changes, grayed-out ("Saved") when
    // everything is persisted. It lets the player save manually without leaving the track,
    // regardless of the Auto-Save setting (which only controls the automatic leave-track flush).
    // THE ENGINE'S BUTTON ROW. Its y, its height and each button's box come from
    // the plan, which placed them under the body with the same margins and gap any
    // other child gets -- no reserve-and-spend pair composed here that has to agree
    // with the plan.
    const PlanButtonTerms bt = planButtonTerms(dim);
    const float buttonBoxH = plan.H(plan.g.btnH);
    const float buttonRowY = plan.Y(plan.g.btnTop);
    const float buttonAreaCenterX = startX + panelWidth / 2.0f;
    bool settingsDirty = SettingsManager::getInstance().isDirty();

    // Size both buttons for the widest label they can show (Saved / Close =
    // 5 chars), plus the [button] border+padding each side — the box-model
    // terms, resolved with the same fallbacks the plan applies. The gap
    // between the two is the SUM of the facing [button] margins. At the
    // shipped defaults the gap is one character; the WIDTHS follow the terms
    // — 6 unthemed (padding 0.5/side), 8 themed (border 1 + padding 0.5/side)
    // — and [Advanced] buttonPadding retunes them.
    float saveButtonWidth = PluginUtils::calculateMonospaceTextWidth(5, dim.fontSize)
        + bt.insetL + bt.insetR;
    float closeButtonWidth = saveButtonWidth;
    float buttonGap = bt.gap;
    float totalWidth = saveButtonWidth + buttonGap + closeButtonWidth;
    float startButtonX = buttonAreaCenterX - totalWidth / 2.0f;

    // [Save] / [Saved] button
    float saveButtonX = startButtonX;
    if (settingsDirty) {
        // Unsaved changes: lit and clickable.
        size_t saveRegionIndex = m_clickRegions.size();
        m_clickRegions.push_back(ClickRegion(
            saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            ClickRegion::SAVE_BUTTON, nullptr, 0, false, 0
        ));
        addStateButton(saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            "Save", buttonRowY + bt.insetT, dim.fontSize,
            ColorConfig::getInstance().getPositive(),
            (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
                ? ButtonState::Hovered : ButtonState::Idle);
    } else {
        // Nothing to save: grayed out, not clickable (no click region -> no hover/click).
        addStateButton(saveButtonX, buttonRowY, saveButtonWidth, buttonBoxH,
            "Saved", buttonRowY + bt.insetT, dim.fontSize,
            ColorConfig::getInstance().getPositive(), ButtonState::Disabled);
    }

    // [Close] button
    float closeButtonX = saveButtonX + saveButtonWidth + buttonGap;
    size_t closeRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        closeButtonX, buttonRowY, closeButtonWidth, buttonBoxH,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));
    addStateButton(closeButtonX, buttonRowY, closeButtonWidth, buttonBoxH,
        "Close", buttonRowY + bt.insetT, dim.fontSize,
        ColorConfig::getInstance().getAccent(),
        (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
            ? ButtonState::Hovered : ButtonState::Idle);

    addResetTabButton(dim, plan, sideCol, bt, buttonRowY, buttonBoxH);
    addAboutButton(dim, plan, mainCol, bt, buttonRowY, buttonBoxH);
}

// [Reset <TabName>] button - bottom left corner.
//
// ONLY WHERE THERE IS SOMETHING TO RESET. A tab's reset is its registry row's
// resetHud / resetExtra, and a row with neither has nothing the button could do
// -- About is prose and links, so "Reset About" would be a live-looking control
// that does nothing at all when clicked. Read off the registry rather than a list of
// exceptions, so a future page of pure text gets the same treatment for free.
void SettingsHud::addResetTabButton(const ScaledDimensions& dim, const PanelPlan& plan,
                                    const PanelBox::ColumnGeom& sideCol, const PlanButtonTerms& bt,
                                    float buttonRowY, float buttonBoxH) {
    const TabDescriptor* activeDesc = findTabDescriptor(m_activeTab);
    const bool tabHasReset = activeDesc && (activeDesc->resetHud || activeDesc->resetExtra);
    if (!tabHasReset) return;

    float resetTabButtonY = buttonRowY;
    char resetTabButtonText[32];
    snprintf(resetTabButtonText, sizeof(resetTabButtonText), "Reset %s", getTabName(m_activeTab));
    int resetTabButtonChars = static_cast<int>(strlen(resetTabButtonText));
    // The [button] insets pad the label.
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(resetTabButtonChars, dim.fontSize)
        + bt.insetL + bt.insetR;
    // LEFT-ALIGNED ON THE SIDEBAR'S CARD, which is the panel's leftmost surface --
    // the same line every other left edge in this panel comes from.
    const float resetTabButtonX = plan.X(sideCol.cardLeft);

    // Add click region first for hover check
    size_t resetTabRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        resetTabButtonX, resetTabButtonY, resetTabButtonWidth, buttonBoxH,
        ClickRegion::RESET_TAB_BUTTON, nullptr
    ));

    // NEGATIVE, like the Reset button in General's Reset section: both destroy
    // settings, and a destructive control that reads as an ordinary accent action
    // is the one place in this panel where colour should carry the warning.
    addStateButton(resetTabButtonX, resetTabButtonY, resetTabButtonWidth, buttonBoxH,
        resetTabButtonText, resetTabButtonY + bt.insetT, dim.fontSize,
        ColorConfig::getInstance().getNegative(),
        (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
            ? ButtonState::Hovered : ButtonState::Idle);
}

// [About] button - bottom right corner.
//
// Neither the version nor the update notice lives on this button; each is where
// its owner is: the update notice is a tag on the Updates row in the sidebar
// (see updateTagLive, which is dismissible and re-arms for a newer version),
// and the version is the first line of the About page this opens.
//
// A REAL BUTTON rather than muted text, because it is the ONLY way to reach
// About -- the page is not in the tab list (TabDescriptor::hidden), so an
// affordance that does not look clickable would make it unreachable in
// practice. Secondary rather than Close's accent: it is a quieter action than
// the one that shuts the panel.
//
// The five-click easter egg works from here, and still works after the first
// click has navigated: the footer is drawn on every tab, so clicks two through
// five land while About is already open.
void SettingsHud::addAboutButton(const ScaledDimensions& dim, const PanelPlan& plan,
                                 const PanelBox::ColumnGeom& mainCol, const PlanButtonTerms& bt,
                                 float buttonRowY, float buttonBoxH) {
    // The content column's right edge -- the same line a row ends on, so the
    // button sits flush with the settings above it.
    const float rightEdgeX = plan.X(mainCol.cardLeft + mainCol.cardW);
    const char* aboutLabel = "About";
    const float aboutWidth =
        PluginUtils::calculateMonospaceTextWidth(
            static_cast<int>(strlen(aboutLabel)), dim.fontSize) + bt.insetL + bt.insetR;
    const float aboutX = rightEdgeX - aboutWidth;

    const size_t aboutRegionIndex = m_clickRegions.size();
    ClickRegion aboutRegion;
    aboutRegion.type = ClickRegion::VERSION_CLICK;
    aboutRegion.x = aboutX;
    aboutRegion.y = buttonRowY;
    aboutRegion.width = aboutWidth;
    aboutRegion.height = buttonBoxH;
    m_clickRegions.push_back(aboutRegion);

    addStateButton(aboutX, buttonRowY, aboutWidth, buttonBoxH,
        aboutLabel, buttonRowY + bt.insetT, dim.fontSize,
        ColorConfig::getInstance().getSecondary(),
        (m_hoveredRegionIndex == static_cast<int>(aboutRegionIndex))
            ? ButtonState::Hovered : ButtonState::Idle);
}

#if defined(MXBMRP3_TEST_BUILD)
// The About button's own click region, so a test measures the geometry the panel
// actually built. Returns false when the footer has not been drawn yet.
bool SettingsHud::testAboutButtonRect(int* l, int* t, int* r, int* b) const {
    for (const ClickRegion& reg : m_clickRegions) {
        if (reg.type != ClickRegion::VERSION_CLICK) continue;
        auto q = [](float v) { return static_cast<int>(v * 1e6f + (v < 0 ? -0.5f : 0.5f)); };
        if (l) *l = q(reg.x);
        if (t) *t = q(reg.y);
        if (r) *r = q(reg.x + reg.width);
        if (b) *b = q(reg.y + reg.height);
        return true;
    }
    return false;
}
#endif
