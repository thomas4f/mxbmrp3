// ============================================================================
// hud/dash_hud_nowplaying.cpp
// Dash - the Now Playing page: the Now Playing widget's rows (title, artist with
// the track's progress, previous / play-pause / next and the player's volume),
// laid out by the same NowPlayingCard and replayed in the Dash's colours. See
// dash_hud.h and now_playing_card.h.
// ============================================================================
#include "dash_hud.h"

#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "settings_hud.h"

using namespace PluginConstants;

void DashHud::drawNowPlaying(float rowL, float rowR, float contentY, const ScaledDimensions& dim) {
    NowPlayingCard::Style style;
    style.primary = getColor(ColorSlot::PRIMARY);
    style.secondary = getColor(ColorSlot::SECONDARY);
    style.tertiary = getColor(ColorSlot::TERTIARY);
    style.muted = getColor(ColorSlot::MUTED);
    style.accent = getColor(ColorSlot::ACCENT);
    const int font = getFont(FontCategory::NORMAL);
    m_nowPlaying.layout(rowL, rowR, contentY, dim.lineHeightNormal, dim.fontSize, font, style);
    for (const NowPlayingCard::Prim& p : m_nowPlaying) {
        switch (p.kind) {
            case NowPlayingCard::Prim::Kind::BAR:
                addHorizontalGridLine(p.x, p.y + p.h * 0.5f, p.w, p.color, p.h);
                break;
            case NowPlayingCard::Prim::Kind::TEXT:
                addString(p.text, p.x, p.y, p.center ? Justify::CENTER : Justify::LEFT, font, p.color, dim.fontSize);
                break;
            case NowPlayingCard::Prim::Kind::ICON:
                addIcon(p.x, p.y, p.sprite, p.color, p.h);
                break;
        }
    }
}

void DashHud::followMediaInput() {
    // Live only on the page fully up (no fade, name or hint over it), with the
    // pointer showing, as the card's message click.
    const InputManager& input = InputManager::getInstance();
    const bool live = isVisibleAnySurface() && m_page == Page::NOW_PLAYING && m_fadeMs <= 0.0f && !m_nameUp && m_stepTried &&
                      !isHeldBack() && input.shouldShowCursor() &&
                      !HudManager::getInstance().getSettingsHud().isVisible();   // vis-gate: the menu's own state
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    const MouseButton& left = input.getLeftButton();
    if (m_nowPlaying.follow(live && cursor.isValid, cursor.x - getOffsetX(), cursor.y - getOffsetY(),
                            left.isClicked(), left.isPressed)) {
        setDataDirty();
    }
}
