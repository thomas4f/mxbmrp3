// ============================================================================
// hud/director_widget.cpp
// Camera-icon status button for the auto-director. The camera glyph stays
// constant; its tint alone encodes the state (off / manual / paused / running),
// and clicking it (cursor-visible) pauses/resumes auto-direction. Mirrors
// SettingsButtonWidget.
// ============================================================================
#include "director_widget.h"

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
#include <cstring>

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

    // One-word text shown when flat icons are disabled (UiConfig title icons off).
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
    DEBUG_INFO("DirectorWidget created");
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
    // 100% scale, matching SettingsButtonWidget -> the same pixel-square 6x5 grid-cell box
    // (~0.033 x 0.0587, ~63x63 px; a 2x2 block = one square gauge widget). Defaults to the
    // LEFT of the settings button (left edge x=0.957) with a one-cell gap: 0.957 - 0.033
    // (width) - 0.0055 (1 grid gap) = 0.9185 (= 167 grid cells). Same y (1 cell). Draggable.
    m_fScale = 1.0f;
    setPosition(0.9185f, 0.01173f);
    setDataDirty();
}

bool DirectorWidget::isClicked() const {
    // Surface-aware visibility: the button may be enabled only on the companion.
    if (!isVisibleOnActiveSurface()) return false;
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

    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    AssetManager& assets = AssetManager::getInstance();
    // Borderless "hud-" camera for the shadowed main glyph (the hud-* icons are the
    // no-outline variants meant to carry the drop shadow, like the settings button).
    const int camSprite = useIcons ? assets.getIconSpriteIndex("hud-video") : 0;

    // Icon button when UI icons are on AND the camera glyph resolves; a missing asset
    // (or icons disabled) degrades to the "DIR <state>" text button below - like
    // SettingsButtonWidget, never a meaningless coloured square.
    if (camSprite > 0) {
        // PIXEL-SQUARE grid-aligned icon button, sized so a 2x2 block fills one square
        // gauge widget (e.g. the compass = 12 x 10 grid cells): each button is HALF that,
        // 6 x 5 cells. On the current grid 6*gridH == 5*gridV in pixels, so the box is a
        // true pixel-square; whole cells in both axes keep every edge on a grid line, so
        // four buttons snap perfectly inside a gauge widget and any pair tiles flush.
        // Cells scale with m_fScale (default 1.0). Must match SettingsButtonWidget.
        const float gridH = HudGrid::GRID_SIZE_HORIZONTAL;
        const float gridV = HudGrid::GRID_SIZE_VERTICAL;
        int hCells = static_cast<int>(6.0f * m_fScale + 0.5f); if (hCells < 1) hCells = 1;
        int vCells = static_cast<int>(5.0f * m_fScale + 0.5f); if (vCells < 1) vCells = 1;
        const float bgW = hCells * gridH;
        const float bgH = vCells * gridV;
        // Camera glyph at the SAME size as a HUD title/identity icon. HUD titles draw at
        // the LARGE font, so the identity icon is fontSizeLarge * 0.63 (~20px at 1080p) -
        // use that exact size (not fontSize * 0.63, which is smaller) for a consistent glyph.
        const float iconSize = dim.fontSizeLarge * TITLE_ICON_SCALE;

        addBackgroundQuad(startX, startY, bgW, bgH);

        // Accent "chip" (full on hover, dimmed otherwise) - matches SettingsButtonWidget.
        // Holds its normal weight at/above 10% opacity (unchanged), then fades with the
        // slider below 10% so the whole box can vanish at 0%, leaving just the opaque glyph.
        {
            SPluginQuad_t chip;
            float x = startX, y = startY;
            applyOffset(x, y);
            setQuadPositions(chip, x, y, bgW, bgH);
            chip.m_iSprite = SpriteIndex::SOLID_COLOR;
            const float chipScale = (m_fBackgroundOpacity < 0.1f) ? (m_fBackgroundOpacity / 0.1f) : 1.0f;
            const float chipAlpha = (isHovering ? 1.0f : 128.0f / 255.0f) * chipScale;
            chip.m_ulColor = PluginUtils::applyOpacity(getColor(ColorSlot::ACCENT), chipAlpha);
            m_quads.push_back(chip);
        }

        const float camCx = startX + bgW * 0.5f;
        const float camCy = startY + bgH * 0.5f;

        // Flag the camera as the "title icon" so it gets the same togglable drop shadow
        // as the settings button's glyph (added in HudManager's collect path). The
        // camera's TINT alone encodes the director state (off / manual / paused /
        // running) - no corner badge, colours only.
        m_titleIconQuadIndex = static_cast<int>(m_quads.size());
        addIcon(camCx, camCy, camSprite, tint, iconSize);

        setBounds(startX, startY, startX + bgW, startY + bgH);
        return;
    }

    // Text fallback (UI icons off, or the camera glyph missing): "DIR <state>" with a
    // small state-tinted status dot. (addDot draws a solid quad - fine as a status dot
    // here, but it's never used to stand in for an icon.)
    char detail[24];
    snprintf(detail, sizeof(detail), "DIR  %s", stateWord(st));
    const float dotDia = dim.fontSize * 0.6f;
    const float gap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize) * 0.5f;
    const float textW = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(std::strlen(detail)), dim.fontSize);
    const float lineH = dim.lineHeightNormal;
    const float textX = startX + dim.paddingH + dotDia + gap;
    const float bgW = textX + textW + dim.paddingH;
    const float bgH = dim.paddingV + lineH + dim.paddingV;

    addBackgroundQuad(startX, startY, bgW, bgH);
    addDot(startX + dim.paddingH + dotDia * 0.5f, startY + dim.paddingV + lineH * 0.5f, tint, dotDia);
    const unsigned long textColor = (st == DirState::Off) ? getColor(ColorSlot::MUTED) : getColor(ColorSlot::PRIMARY);
    addString(detail, textX, startY + dim.paddingV, Justify::LEFT, getFont(FontCategory::NORMAL), textColor, dim.fontSize);
    setBounds(startX, startY, startX + bgW, startY + bgH);
}
