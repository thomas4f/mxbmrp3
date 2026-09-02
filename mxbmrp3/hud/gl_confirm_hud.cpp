// ============================================================================
// hud/gl_confirm_hud.cpp
// Implementation of the Direct GL confirmation prompt - see the header for why
// its primitives are handed to the ENGINE rather than drawn through GL.
// ============================================================================
#include "gl_confirm_hud.h"

#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_utils.h"
#include "../core/settings_manager.h"
#include "../core/ui_config.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace PluginConstants;

namespace {
// Persist immediately on either answer. The setting this prompt exists to guard
// must survive a restart WITHOUT the user going back into a menu they may not be
// able to read - which is the whole scenario.
void persistNow() {
    SettingsManager::getInstance().saveSettings(HudManager::getInstance(),
                                                PluginManager::getInstance().getSavePath());
}
}  // namespace

GlConfirmHud::GlConfirmHud() {
    m_bVisible = false;          // armed on demand, never by a setting
    m_bShowTitle = true;
    m_fOffsetX = 0.0f;
    m_fOffsetY = 0.0f;
}

bool GlConfirmHud::handlesDataType(DataChangeType) const {
    // Nothing in PluginData drives this. It is driven by its own countdown, which
    // tickDrawn() advances, and that already marks the data dirty.
    return false;
}

void GlConfirmHud::arm() {
    if (m_active) return;
    m_remaining = TIMEOUT_SECONDS;
    m_lastTick = {};
    m_active = true;
    m_shownPercent = -1;
    m_keepHovered = m_revertHovered = false;
    // Swallow whatever button state the click that ENABLED the setting left
    // behind, or that same press is read here as a click on whichever of our
    // buttons happens to land under the cursor.
    m_wasLeftPressed = true;
    m_bVisible = true;
    setDataDirty();
    DEBUG_INFO_F("GlConfirm: armed - reverting Direct GL in %.0fs unless confirmed",
                 TIMEOUT_SECONDS);
}

void GlConfirmHud::cancel() {
    if (!m_active) return;
    m_active = false;
    m_bVisible = false;
    setDataDirty();
}

float GlConfirmHud::remainingFraction() const {
    if (!m_active || TIMEOUT_SECONDS <= 0.0f) return 0.0f;
    return std::clamp(m_remaining / TIMEOUT_SECONDS, 0.0f, 1.0f);
}

void GlConfirmHud::tickDrawn(float dtSeconds) {
    if (!m_active) return;
    if (dtSeconds > 0.0f) m_remaining -= dtSeconds;
    if (m_remaining <= 0.0f) {
        DEBUG_WARN("GlConfirm: no confirmation before the timer ran out - reverting "
                   "Direct GL Rendering to engine drawing");
        revertNow();
        return;
    }
    // Rebuild only when the bar would actually move a pixel's worth.
    const int pct = static_cast<int>(remainingFraction() * 100.0f);
    if (pct != m_shownPercent) {
        m_shownPercent = pct;
        setDataDirty();
    }
}

void GlConfirmHud::keep() {
    DEBUG_INFO("GlConfirm: confirmed - Direct GL Rendering stays on");
    cancel();
    persistNow();
}

void GlConfirmHud::revertNow() {
    // Turn the SETTING off, not just the backend: the point is that the user ends
    // up somewhere they can read, and stays there across restarts.
    UiConfig::getInstance().setGlInGame(false);
    cancel();
    persistNow();
}

void GlConfirmHud::update() {
    if (!m_active) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // TIME PASSES WHILE FRAMES ARE BEING PRODUCED, and that needs no flag: this
    // update() is only ever called from produceFrame, which is only ever called
    // from the game's Draw callback. In menus the game issues no Draw at all, so
    // no update runs and no time passes - the menu requirement holds by
    // construction rather than by a test against renderer state.
    //
    // It DID gate on HudManager::glDrewLastFrame() at first, which made the
    // countdown crawl in game while measuring perfectly in the harness.
    // renderInContextGl clears that flag on entry and only sets it when it
    // actually draws, and on the pluginThread path the game thread calls it every
    // frame - including with an EMPTY frame whenever takeFrame finds nothing
    // ready. At several hundred fps against a worker building far slower, most
    // samples therefore read false, and every false sample DISCARDED ITS
    // INTERVAL. Ten seconds became minutes. A flag that means "did the last GL
    // call draw" is not the same question as "is time passing for the player",
    // and only the second one belongs here.
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_lastTick.time_since_epoch().count() != 0) {
            const float dt = std::chrono::duration<float>(now - m_lastTick).count();
            // Clamp: a hitch, an alt-tab or a breakpoint between two frames must
            // not swallow the whole countdown in one step.
            tickDrawn(dt > 0.5f ? 0.5f : dt);
            if (!m_active) return;          // that tick expired it
        }
        m_lastTick = now;
    }

    handleClickDetection();
    if (isDataDirty() || isLayoutDirty()) rebuildAndRecord();
}

void GlConfirmHud::rebuildLayout() {
    rebuildRenderData();
}

void GlConfirmHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();
    if (!m_active) return;

    const auto dim = getScaledDimensions();

    // Two message rows, the countdown bar, then the button row.
    const char* kQ1 = "Direct GL Rendering is ON.";
    const char* kQ2 = "Can you read this clearly?";
    const float barH = dim.lineHeightNormal * 0.45f;
    // FULL CHIP HEIGHT - the theme's [button] insets around a text row, which is
    // what Compare, Save and every other chip in the plugin is. It was the bare
    // row at first, copied from CrashWidget, and that is the one place the squeeze
    // is deliberate: it exists so the widget tiles at the same height as Speed and
    // Gear beside it, and its comment says so. Nothing here tiles with anything.
    const BaseHud::PlanButtonTerms bt = planButtonTerms(dim);
    const float buttonH = bt.insetT + dim.lineHeightNormal + bt.insetB;

    BaseHud::PanelWant want;
    // Sized from the LONGEST message row plus a character of air each side. The
    // buttons sit inside that, so nothing here is sized from a label the way a
    // chip usually is - the text is what must be readable, so the text wins.
    const int widestChars = static_cast<int>(std::max(std::strlen(kQ1), std::strlen(kQ2))) + 2;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(widestChars, dim.fontSize);
    want.sectionH = { dim.lineHeightNormal * 2.0f + dim.lineHeightNormal * 0.4f + barH +
                      dim.lineHeightNormal * 0.5f + buttonH };

    // TitleTier::Large, like every full HUD's caption - measured and drawn at the
    // same size, which is what check_title_tier is guarding.
    want.captionW = planTitleWidth(dim, "Confirm", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& p = planPanel(dim, want);

    // CENTRED ON SCREEN, every rebuild. This HUD has no saved position and is not
    // draggable: it is a modal, it must be where the eye already is, and a user
    // who cannot read it certainly cannot be asked to go looking for it.
    //
    // Centred through the PLAN'S ORIGIN, not by rewriting m_fOffsetX. Both put the
    // panel in the same place, and the offset version is wrong for a reason worth
    // recording: the offset would then depend on the panel's own width, so a theme
    // that changed the panel's size would move every DRAWN string while the plan's
    // card stayed where it was. Anchoring is checked as (string - card), and that
    // difference is only meaningful while the two live in one space. The
    // card-anchor sweep caught it the moment the prompt was armed for it.
    const float originX = (1.0f - p.width()) * 0.5f;
    const float originY = (1.0f - p.height()) * 0.5f;
    m_fOffsetX = 0.0f;
    m_fOffsetY = 0.0f;

    addPlanBackground(p, originX, originY);
    addPlanTitle(p, "Confirm", this->getFont(FontCategory::TITLE),
                 this->getColor(ColorSlot::PRIMARY));

    const float centerX = p.sectionBoxCenterX();
    const float contentX = p.contentX();
    float y = p.contentY();

    addString(kQ1, centerX, y, Justify::CENTER, this->getFont(FontCategory::NORMAL),
              this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    y += dim.lineHeightNormal;
    addString(kQ2, centerX, y, Justify::CENTER, this->getFont(FontCategory::NORMAL),
              this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    y += dim.lineHeightNormal + dim.lineHeightNormal * 0.4f;

    // THE COUNTDOWN, as a bar rather than a number of seconds. A digit is the one
    // thing on this panel that a broken glyph path could turn to mush, and the
    // whole point of the prompt is to work when the glyphs do not. A shrinking
    // block still reads as "something is running out" with no text at all.
    const float frac = remainingFraction();
    {
        SPluginQuad_t track;
        float tx = contentX, ty = y;
        applyOffset(tx, ty);
        setQuadPositions(track, tx, ty, p.contentW(), barH);
        track.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        track.m_ulColor = this->getColor(ColorSlot::MUTED);
        m_quads.push_back(track);

        if (frac > 0.0f) {
            SPluginQuad_t fill;
            float fx = contentX, fy = y;
            applyOffset(fx, fy);
            setQuadPositions(fill, fx, fy, p.contentW() * frac, barH);
            fill.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            // Warm as it empties: ACCENT while there is time, NEGATIVE at the end,
            // so the last few seconds are legible as urgency without a number.
            fill.m_ulColor = frac < 0.25f ? this->getColor(ColorSlot::NEGATIVE)
                                          : this->getColor(ColorSlot::ACCENT);
            m_quads.push_back(fill);
        }
    }
    y += barH + dim.lineHeightNormal * 0.5f;

    // Two chips splitting the content box, a character of gap between them.
    const float gap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    const float chipW = (p.contentW() - gap) * 0.5f;
    m_keepLeft = contentX;             m_keepTop = y;
    m_keepW = chipW;                   m_keepH = buttonH;
    m_revertLeft = contentX + chipW + gap; m_revertTop = y;
    m_revertW = chipW;                 m_revertH = buttonH;

    // Label at insetT, the way every other chip places it, rather than
    // ink-centred: that solve belongs to CrashWidget's insetless squeeze.
    addStateButton(m_keepLeft, m_keepTop, m_keepW, m_keepH, "Keep",
                   m_keepTop + bt.insetT, dim.fontSize,
                   this->getColor(ColorSlot::ACCENT),
                   m_keepHovered ? ButtonState::Hovered : ButtonState::Idle);
    addStateButton(m_revertLeft, m_revertTop, m_revertW, m_revertH, "Turn off",
                   m_revertTop + bt.insetT, dim.fontSize,
                   this->getColor(ColorSlot::NEGATIVE),
                   m_revertHovered ? ButtonState::Hovered : ButtonState::Idle);

    setBounds(originX, originY, originX + p.width(), originY + p.height());
}

void GlConfirmHud::handleClickDetection() {
    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled()) return;

    const bool isLeftPressed = input.getLeftButton().isPressed;
    const bool isLeftClick = isLeftPressed && !m_wasLeftPressed;
    m_wasLeftPressed = isLeftPressed;

    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);

    const bool oldKeep = m_keepHovered, oldRevert = m_revertHovered;
    m_keepHovered = m_revertHovered = false;
    if (cursor.isValid) {
        auto inside = [&](float l, float t, float w, float h) {
            const float x0 = l + m_fOffsetX, y0 = t + m_fOffsetY;
            return cursor.x >= x0 && cursor.x <= x0 + w &&
                   cursor.y >= y0 && cursor.y <= y0 + h;
        };
        m_keepHovered = inside(m_keepLeft, m_keepTop, m_keepW, m_keepH);
        m_revertHovered = inside(m_revertLeft, m_revertTop, m_revertW, m_revertH);
    }
    if (m_keepHovered != oldKeep || m_revertHovered != oldRevert) setDataDirty();

    if (isLeftClick) {
        if (m_keepHovered) keep();
        else if (m_revertHovered) revertNow();
    }
}
