// ============================================================================
// hud/director_widget.cpp
// Camera-icon status button for the auto-director. The camera glyph stays
// constant; its tint alone encodes the state (off / manual / paused / running),
// and clicking it (cursor-visible) pauses/resumes auto-direction. With UI icons
// off, a one-word state ("auto", "lock", ...) in the same tint stands in for the
// camera. Mirrors SettingsButtonWidget.
// ============================================================================
#include "director_widget.h"
#include "corner_buttons.h"
#include "../core/layout_config.h"

#include "../core/director_manager.h"
#include "../handlers/spectate_handler.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/ui_config.h"
#include "../core/input_manager.h"
#include "../diagnostics/logger.h"

#include <cstdio>

using namespace PluginConstants;

namespace {
    // The director's display state, in priority order (off > manual > paused > running).
    enum class DirState { Off, Manual, Paused, Running, Awaiting };

    DirState currentState(const DirectorManager& dir) {
        if (!dir.isEnabled()) return DirState::Off;
        // Manual = a manual camera (Free-Roam / Orbit, by name) OR a gamepad takeover in
        // progress (which yields even on tracks with no Free-Roam camera to switch to).
        if (SpectateHandler::getInstance().isManualCameraActive() || dir.isTakeoverActive())
            return DirState::Manual;
        if (dir.isLocked()) return DirState::Paused;
        if (dir.getCurrentSubject() >= 0) return DirState::Running;
        return DirState::Awaiting;
    }

    // Coarse "reveal bucket": collapses the two auto sub-states (Running / Awaiting, which
    // flip every time the director gains or loses a subject) into one, so the button only
    // flashes on a MEANINGFUL mode change - off/on, manual takeover (gamepad or a manual
    // camera) and the return to automatic, and pause/resume - not on subject churn.
    int revealBucket(DirState s) {
        return (s == DirState::Running || s == DirState::Awaiting)
            ? static_cast<int>(DirState::Running)
            : static_cast<int>(s);
    }

    // Tint colour slot for each state - the camera glyph's colour is the sole
    // state indicator (no corner badge).
    ColorSlot stateColor(DirState s) {
        switch (s) {
            case DirState::Off:      return ColorSlot::MUTED;
            // Manual is a temporary, benign yield to the caster -> NEUTRAL (yellow).
            case DirState::Manual:   return ColorSlot::NEUTRAL;
            // Paused == the rider lock; WARNING (orange) to match the standings lock icon.
            case DirState::Paused:   return ColorSlot::WARNING;
            case DirState::Running:  return ColorSlot::POSITIVE;
            case DirState::Awaiting: return ColorSlot::NEUTRAL;
        }
        return ColorSlot::NEUTRAL;
    }

    // One-word text shown instead of the camera while UI icons are off.
    const char* stateWord(DirState s) {
        switch (s) {
            case DirState::Off:      return "off";
            case DirState::Manual:   return "man";
            case DirState::Paused:   return "lock";  // the rider lock (== isLocked)
            case DirState::Running:  return "auto";
            case DirState::Awaiting: return "...";
        }
        return "...";
    }
}

DirectorWidget::DirectorWidget() {
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    m_panelKind = PanelKind::Widget;
    setDraggable(true);
    m_strings.reserve(1);   // text fallback only
    m_quads.reserve(6);     // background + chip + camera (+ shadow); dot for text fallback
    resetToDefaults();
    // Seed the state baseline from the manager so the first rebuild doesn't read as a
    // mode change and spuriously reveal the button on load.
    m_lastRevealBucket = revealBucket(currentState(DirectorManager::getInstance()));
    rebuildRenderData();
}

bool DirectorWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // Rebuilt every frame (cheap), so it also reflects manual-pause / hover state
    // which don't emit a data-change notification.
    return false;
}

void DirectorWidget::update() {
    if (!isVisibleAnySurface()) return;   // off by default - do no work while hidden on both surfaces
    setDataDirty();
    processDirtyFlags();
}

void DirectorWidget::resetToDefaults() {
    m_bVisible = true;             // on by default (status + on/off control)
    m_bShowTitle = false;
    setTextureVariant(0);
    // Match SettingsButtonWidget: the accent chip scales with the opacity slider, so 0%
    // fades the box away leaving just the opaque glyph - the slider floors at 0%.
    m_fMinBackgroundOpacity = 0.0f;
    m_fBackgroundOpacity = 0.1f;
    // 100% scale, matching SettingsButtonWidget -> the same box. The position is DERIVED
    // from the gear's, one cell to its left; see hud/corner_buttons.h for why the two
    // defaults share a header (computed apart, this button overlaps its neighbour by a
    // cell, and a click in the overlap toggles the director AND opens the settings
    // panel).
    setScale(1.0f);
    setPosition(cellsX(CornerButtons::DIRECTOR_X), cellsY(CornerButtons::BUTTON_Y));
    setDataDirty();
}

bool DirectorWidget::isClicked() const {
    // Surface-aware visibility: the button may be enabled only on the companion.
    if (!isVisibleOnActiveSurface() || isHeldBack()) return false;
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor()) return false;          // only clickable in cursor mode
    const CursorPosition& cursor = input.getCursorPosition();
    if (!cursor.isValid) return false;
    if (!input.getLeftButton().isClicked()) return false;
    return isPointInActiveBounds(cursor.x, cursor.y);   // test on the surface the cursor is on
}

void DirectorWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();
    m_titleIconQuadIndex = -1;   // reset each rebuild; set on the camera so it's shadowed
    m_titleStringIndex = -1;

    const DirectorManager& dir = DirectorManager::getInstance();
    const DirState st = currentState(dir);

    // A meaningful state change briefly reveals the button, so the flip is visible even
    // while the mouse is idle: on/off (click / hotkey / settings), a gamepad manual
    // takeover or the return to automatic, and pause/resume. Checked every rebuild (runs
    // each frame) so it catches the change regardless of source.
    const int bucket = revealBucket(st);
    if (bucket != m_lastRevealBucket) {
        m_lastRevealBucket = bucket;
        reveal(PluginConstants::WIDGET_REVEAL_MS);
    }

    // Only relevant while spectating/replaying - stays out of the way otherwise.
    const PluginData& pd = PluginData::getInstance();
    int ds = pd.getDrawState();
    if (ds != ViewState::SPECTATE && ds != ViewState::REPLAY) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Auto-hide with the cursor (like the settings button), unless briefly revealed
    // (state change / entering the track) so users can find it without moving the mouse.
    const InputManager& input = InputManager::getInstance();
    if (!input.shouldShowCursor() && !isRevealed()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const unsigned long tint = getColor(stateColor(st));

    auto dim = getScaledDimensions();
    const float startX = 0.0f, startY = 0.0f;

    // Hover highlight (cursor mode only), so the button reads as clickable.
    bool isHovering = false;
    if (input.shouldShowCursor()) {
        const CursorPosition& cursor = input.getCursorPosition();
        if (cursor.isValid) isHovering = isPointInActiveBounds(cursor.x, cursor.y);
    }

    // Borderless "hud-" camera for the shadowed main glyph (the hud-* icons are the
    // no-outline variants meant to carry the drop shadow, like the settings button).
    // With UI icons off the state word below stands in for it. Without its icons the
    // install gets the broken-install warning (HudManager::buildInstallWarning), and
    // the chip simply stays empty.
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const int camSprite = useIcons ? AssetManager::getInstance().getIconSpriteIndex("hud-video") : 0;
    // Content + padding, like every other panel: the glyph is the content and the
    // panel padding surrounds it, so panelPaddingXCells/panelPaddingYCells apply
    // here as on every other panel.
    //
    // The box is only pixel-square when the padding makes it so (the grid cell is
    // 10.56 x 12.672px, so equal cell counts are not equal pixels); a fixed
    // pixel-square would let a 2x2 block fill one square gauge widget, and uniform
    // padding is the explicit trade against it.
    const float btnIcon = dim.fontSizeLarge * layout().titleIconSize;
    // BOTH AXES ON THE LATTICE. Unlike a dial, this box is not art -- it is a chip
    // (a themed button slice set, or a plain solid quad), which is MEANT to be sized to
    // the box, so growing it to whole cells distorts nothing and the glyph stays centred
    // in it. The box is not pixel-square, because a cell is not; it is a whole number
    // of cells, which is what tiling actually needs. See fitPanelToGrid.
    const GridFit btnFit = fitPanelToGrid(dim.paddingH + btnIcon + dim.paddingH,
                                          panelHeight(dim, btnIcon));
    const float bgW = btnFit.w;
    const float bgH = btnFit.h;
    // Camera glyph at the SAME size as a HUD title/identity icon. HUD titles draw at
    // the LARGE font, so the identity icon is fontSizeLarge * 0.63 (~20px at 1080p) -
    // use that exact size (not fontSize * 0.63, which is smaller) for a consistent glyph.
    const float iconSize = dim.fontSizeLarge * layout().titleIconSize;

    // Accent "chip" (full on hover, dimmed otherwise) - matches SettingsButtonWidget.
    // Holds its normal weight at/above 10% opacity (unchanged), then fades with the
    // slider below 10% so the whole box can vanish at 0%, leaving just the opaque glyph.
    const float chipScale = (m_fBackgroundOpacity < 0.1f) ? (m_fBackgroundOpacity / 0.1f) : 1.0f;
    const float chipAlpha = (isHovering ? 1.0f : 128.0f / 255.0f) * chipScale;
    const unsigned long chipColor =
        PluginUtils::applyOpacity(getColor(ColorSlot::ACCENT), chipAlpha);

    // A button, not a panel: takes the theme's BUTTON slices. See SettingsButtonWidget
    // for why a frame-plus-opaque-chip pair reads as unthemed.
    if (addThemedButton(startX, startY, bgW, bgH, chipColor)) {
        // Same reason as SettingsButtonWidget: the themed branch skips
        // addBackgroundQuad, which is what arms the panel rect and fill strips.
        invalidatePanelRect();
    } else {
        addBackgroundQuad(startX, startY, bgW, bgH);
        SPluginQuad_t chip;
        float x = startX, y = startY;
        applyOffset(x, y);
        setQuadPositions(chip, x, y, bgW, bgH);
        chip.m_iSprite = SpriteIndex::SOLID_COLOR;
        chip.m_ulColor = chipColor;
        m_quads.push_back(chip);
    }

    const float camCx = startX + bgW * 0.5f;
    const float camCy = startY + bgH * 0.5f;

    // Flag the camera as the "title icon" so it gets the same togglable drop shadow
    // as the settings button's glyph (added in HudManager's collect path). The
    // camera's TINT alone encodes the director state (off / manual / paused /
    // running) - no corner badge, colours only. The text stand-in keeps the box (so
    // the corner row's spacing holds, see corner_buttons.h) and the tint.
    if (camSprite > 0) {
        m_titleIconQuadIndex = static_cast<int>(m_quads.size());
        addIcon(camCx, camCy, camSprite, tint, iconSize);
    } else if (!useIcons) {
        addString(stateWord(st), camCx, startY + (bgH - dim.fontSize) * 0.5f, Justify::CENTER,
            getFont(FontCategory::NORMAL), tint, dim.fontSize);
    }

    setBounds(startX, startY, startX + bgW, startY + bgH);
}
