// ============================================================================
// hud/deck_hud_media.cpp
// The Now Playing deck: what the player plays (MediaManager's snapshot), and
// its controls. The clicks and drags are NowPlayingControls, the same as the
// Now Playing widget's and the Dash page's; only the boxes they land on come
// from the layout (its `control` boxes and its volume and track meters) instead
// of a layout of their own.
// ============================================================================
#include "deck_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/i18n.h"
#include "../core/input_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;
using namespace DeckLayout;

namespace {
// The layout's controls are the card's, in its order: one index reads both.
constexpr bool sameOrder() {
    const int deck[] = { CONTROL_SHUFFLE, CONTROL_PREV, CONTROL_PLAY, CONTROL_NEXT, CONTROL_LOOP, CONTROL_VOLUME };
    const int card[] = { NowPlayingControls::SHUFFLE, NowPlayingControls::PREV, NowPlayingControls::PLAY,
                         NowPlayingControls::NEXT, NowPlayingControls::LOOP, NowPlayingControls::VOLUME };
    for (int i = 0; i < CONTROL_COUNT; ++i) if (deck[i] != card[i]) return false;
    return true;
}
}  // namespace
static_assert(sameOrder(), "DeckLayout::Control follows NowPlayingControls::Hit");

void DeckHud::gatherMedia(const Deck* deck) {
    setText(TITLE, title(Kind::Media));
    const MediaManager& media = MediaManager::getInstance();
    MediaManager::Snapshot np;
    media.getSnapshot(np);
    const bool player = NowPlaying::hasPlayer(media, np);
    NowPlayingControls& controls = m_media.controls;
    controls.clear(player ? np.durationMs : 0);

    // The buttons' icons, in the card's inks (NowPlaying::ink), every one muted
    // with no player; the volume's hover is its knob's
    m_media.icons.refresh();
    NowPlayingCard::Style style;
    style.primary = getColor(ColorSlot::PRIMARY);
    style.secondary = getColor(ColorSlot::SECONDARY);
    style.muted = getColor(ColorSlot::MUTED);
    style.accent = getColor(ColorSlot::ACCENT);
    bool takes[CONTROL_COUNT] = {};
    for (int i = 0; i < CONTROL_COUNT; ++i) {
        takes[i] = player && NowPlaying::takes(i, np);
        m_rows.controlSprite[i] = m_media.icons.sprite[NowPlaying::icon(i, np)];
        m_rows.controlInk[i] = NowPlaying::ink(i, takes[i], i != CONTROL_VOLUME && controls.hover() == i, np, style);
    }

    if (!player) {
        controls.placedAll();   // none: any drag ends
        bool off = false;
        setText(TRACK, NowPlaying::emptyLine(media, np, off), TONE_EMPTY);
        if (off) setText(ARTIST, TR("Settings > General"), TONE_EMPTY);
        return;
    }

    setText(TRACK, np.title[0] ? np.title : Placeholders::GENERIC);
    setText(ARTIST, np.artist);
    if (np.loop == MediaManager::Loop::TRACK) setText(LOOP_ONE, "1", np.canLoop ? TONE_NORMAL : TONE_EMPTY);
    if (np.state == MediaManager::State::PAUSED) setText(STAMP, TR("PAUSED"), TONE_EMPTY);

    // The track: played and left, from the drag's position while one lasts
    const bool canSeek = np.canSeek && np.durationMs > 0;
    if (np.durationMs > 0) {
        const float share = NowPlaying::seekShare(controls, np);
        const int at = static_cast<int>(std::lround(share * static_cast<float>(np.durationMs)));
        char buf[24];
        MediaManager::formatClock(buf, sizeof(buf), at);
        setText(ELAPSED, buf);
        buf[0] = '-';
        MediaManager::formatClock(buf + 1, sizeof(buf) - 1, np.durationMs - at);
        setText(REMAIN, buf);
        DeckMeter::Data& m = m_rows.meter[TRACK_POS];
        m.show = true;
        m.value = share;
        m.knob = canSeek;
        m.knobHot = canSeek && controls.hover() == NowPlayingControls::SEEK;
    }
    const int volume = controls.dragVolume() >= 0 ? controls.dragVolume() : np.volume;
    if (volume >= 0) {
        DeckMeter::Data& m = m_rows.meter[VOLUME];
        m.show = true;
        m.value = static_cast<float>(volume) / 100.0f;
        m.knob = true;
        m.knobHot = controls.hover() == NowPlayingControls::VOLUME;
    }

    // Where a click lands: the layout's buttons, and its two strips grown by
    // their height each way (a strip is thin to aim at)
    if (deck) {
        for (int i = 0; i < deck->boxCount; ++i) {
            const Box& b = deck->boxes[i];
            if (!b.present || b.art != ART_CONTROL || b.control < CONTROL_SHUFFLE || b.control > CONTROL_LOOP) continue;
            if (!takes[b.control]) continue;
            const float l = std::min(b.quad[0], b.quad[2]), t = std::min(b.quad[1], b.quad[7]);
            const float r = std::max(b.quad[4], b.quad[6]), u = std::max(b.quad[3], b.quad[5]);
            controls.place(static_cast<NowPlayingControls::Hit>(b.control), lx(l), ly(t), lx(r - l), ly(u - t));
        }
        for (int i = 0; i < deck->meterCount; ++i) {
            const MeterEl& el = deck->meters[i];
            const bool seek = el.meter == TRACK_POS && canSeek;
            if (!el.present || !(seek || (el.meter == VOLUME && volume >= 0))) continue;
            const float grow = std::max(el.over, el.rect[3]);
            controls.place(seek ? NowPlayingControls::SEEK : NowPlayingControls::VOLUME, lx(el.rect[0]),
                           ly(el.rect[1] - grow), lx(el.rect[2]), ly(el.rect[3] + grow * 2.0f));
        }
    }
    controls.placedAll();
}

// Every frame, shown or not (update()): the controls follow the pointer, gated
// like the Now Playing widget's. A press starts a control only when the click
// is this deck's (ownsClick), so one on the settings menu, or on a HUD stacked
// over it, never reaches it.
void DeckHud::followMedia() {
    const InputManager& input = InputManager::getInstance();
    const bool live = isVisibleAnySurface() && !isHeldBack() && input.shouldShowCursor();
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    const MouseButton& left = input.getLeftButton();
    if (m_media.controls.follow(live && cursor.isValid, cursor.x - getOffsetX(), cursor.y - getOffsetY(),
                                left.isClicked() && ownsClick(), left.isPressed)) {
        setDataDirty();
    }
}
