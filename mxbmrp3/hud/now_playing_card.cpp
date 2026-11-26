// ============================================================================
// hud/now_playing_card.cpp
// The Now Playing rows - see now_playing_card.h.
// ============================================================================
#include "now_playing_card.h"
#include "../core/i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/asset_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;

namespace {
// Indexed as NowPlayingIcons::Icon; the text each draws when its icon is missing.
constexpr const char* kIcons[NowPlayingIcons::COUNT] = {
    "hud-media-prev", "hud-media-play", "hud-media-pause", "hud-media-next", "hud-media-volume",
    "hud-media-shuffle", "hud-media-loop" };
constexpr const char* kMarks[NowPlayingIcons::COUNT] = { "|<", ">", "||", ">|", "Vo", "Sh", "Lp" };
// The settings slider's track and knob, as a share of a row (SettingsLayoutContext::addSliderTrack).
constexpr float TRACK_SHARE = 0.08f;
constexpr float TRACK_FOOT = 0.90f;
constexpr float KNOB_H_SHARE = 0.30f;
constexpr float KNOB_W_CHARS = 0.45f;
constexpr float TRACK_OPACITY = 0.45f;
// The buttons: icons this share of a row, one every BUTTON_PITCH_CHARS.
constexpr float BUTTON_ICON_SHARE = 0.6f;
constexpr float BUTTON_PITCH_CHARS = 3.0f;
constexpr float VOLUME_SLIDER_CHARS = 7.0f;
// The progress line takes a press from the artist row's lower half down to the buttons.
constexpr float SEEK_HIT_FROM = 1.5f;
}  // namespace

const char* NowPlayingIcons::mark(Icon icon) { return kMarks[icon]; }

void NowPlayingIcons::refresh() {
    const AssetManager& assets = AssetManager::getInstance();
    if (m_gen == assets.themeGeneration()) return;
    m_gen = assets.themeGeneration();
    for (int i = 0; i < COUNT; ++i) sprite[i] = assets.getIconSpriteIndex(kIcons[i]);
}

void NowPlayingControls::clear(int durationMs) {
    for (HitBox& b : m_hit) b = HitBox{};
    m_durationMs = durationMs;
}

void NowPlayingControls::place(Hit hit, float x, float y, float w, float h) {
    m_hit[hit] = HitBox{ x, y, w, h };
}

void NowPlayingControls::placedAll() {
    if (!placed(SEEK)) m_dragSeek = -1.0f;
    if (!placed(VOLUME)) m_dragVolume = -1;
}

void NowPlayingCard::addBar(float x, float y, float w, float h, unsigned long color) {
    if (m_primCount >= MAX_PRIMS || w <= 0.0f) return;
    Prim& p = m_prims[m_primCount++];
    p = Prim{};
    p.kind = Prim::Kind::BAR;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.color = color;
}

void NowPlayingCard::addText(const char* text, float x, float y, bool center, unsigned long color, bool smallText) {
    if (m_primCount >= MAX_PRIMS) return;
    Prim& p = m_prims[m_primCount++];
    p = Prim{};
    p.kind = Prim::Kind::TEXT;
    p.x = x; p.y = y;
    p.text = text;
    p.center = center;
    p.smallText = smallText;
    p.color = color;
}

void NowPlayingCard::addIcon(int sprite, const char* mark, float cx, float cy, float size, unsigned long color) {
    if (sprite <= 0) {
        // No icon in this install: its mark, on the row the icon sits in.
        addText(mark, cx, cy - size / BUTTON_ICON_SHARE * 0.5f, true, color);
        return;
    }
    if (m_primCount >= MAX_PRIMS) return;
    Prim& p = m_prims[m_primCount++];
    p = Prim{};
    p.kind = Prim::Kind::ICON;
    p.x = cx; p.y = cy; p.h = size;
    p.sprite = sprite;
    p.color = color;
}

void NowPlayingCard::layout(float rowL, float rowR, float contentY, float lh, float fontSize, int font, const Style& s) {
    const MediaManager& media = MediaManager::getInstance();
    m_shownRevision = media.revision();
    MediaManager::Snapshot np;
    media.getSnapshot(np);
    m_primCount = 0;

    const float rowW = rowR - rowL;
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    const unsigned long track = PluginUtils::applyOpacity(s.muted, TRACK_OPACITY);
    const unsigned long hoverInk = NowPlaying::hoverInk(s);
    const float knobW = charW * KNOB_W_CHARS;
    const float knobH = lh * KNOB_H_SHARE;

    const bool hasPlayer = NowPlaying::hasPlayer(media, np);
    m_controls.clear(hasPlayer ? np.durationMs : 0);
    if (!hasPlayer) {
        m_controls.placedAll();   // none: any drag ends
        const float centerX = (rowL + rowR) / 2.0f;
        bool off = false;
        const char* line = NowPlaying::emptyLine(media, np, off);
        addText(line, centerX, contentY + lh * (off ? 0.5f : 1.0f), true, s.muted);
        if (off) addText(TR("Settings > General"), centerX, contentY + lh * 1.5f, true, s.tertiary);
        return;
    }

    // Title and artist, cut at the row's width (never an ellipsis).
    const AssetManager& assets = AssetManager::getInstance();
    const char* title = np.title[0] ? np.title : Placeholders::GENERIC;
    snprintf(m_title, sizeof(m_title), "%.*s", assets.fitChars(font, title, rowW, fontSize), title);
    snprintf(m_artist, sizeof(m_artist), "%.*s", assets.fitChars(font, np.artist, rowW, fontSize), np.artist);
    addText(m_title, rowL, contentY, false, s.primary);
    addText(m_artist, rowL, contentY + lh, false, s.secondary);

    // The progress along the artist row's foot.
    const float trackH = lh * TRACK_SHARE;
    const float progressY = contentY + lh * (1.0f + TRACK_FOOT);
    addBar(rowL, progressY, rowW, trackH, track);
    if (np.durationMs > 0) {
        const float share = NowPlaying::seekShare(m_controls, np);
        addBar(rowL, progressY, rowW * share, trackH, s.accent);
        if (np.canSeek) {
            // The volume's knob, as the hint that the line drags too.
            const float knobX = std::clamp(rowL + rowW * share - knobW * 0.5f, rowL, rowR - knobW);
            addBar(knobX, progressY + trackH * 0.5f - knobH * 0.5f, knobW, knobH,
                   m_controls.hover() == NowPlayingControls::SEEK ? hoverInk : s.accent);
            m_controls.place(NowPlayingControls::SEEK, rowL, contentY + lh * SEEK_HIT_FROM, rowW, lh * (2.0f - SEEK_HIT_FROM));
        }
    }

    // The buttons: icons looked up once per theme
    m_icons.refresh();
    const float rowY = contentY + lh * 2.0f;
    const float midY = rowY + lh * 0.5f;
    const float iconSize = lh * BUTTON_ICON_SHARE;
    const float pitch = charW * BUTTON_PITCH_CHARS;
    // In Hit order; no button where the player does not take one.
    for (int i = NowPlayingControls::SHUFFLE; i <= NowPlayingControls::LOOP; ++i) {
        const float cx = rowL + pitch * (static_cast<float>(i) + 0.5f);
        const bool takes = NowPlaying::takes(i, np);
        const NowPlayingIcons::Icon icon = NowPlaying::icon(i, np);
        addIcon(m_icons.sprite[icon], kMarks[icon], cx, midY, iconSize,
                NowPlaying::ink(i, takes, m_controls.hover() == i, np, s));
        if (takes) m_controls.place(static_cast<NowPlayingControls::Hit>(i), cx - pitch * 0.5f, rowY, pitch, lh);
    }
    // Loop over one track: a small "1" raised against the loop icon's top right.
    if (np.loop == MediaManager::Loop::TRACK) {
        addText("1", rowL + pitch * (static_cast<float>(NowPlayingControls::LOOP) + 0.5f) + iconSize * 0.4f,
                midY - iconSize * 0.95f, false, np.canLoop ? s.accent : s.muted, true);
    }

    // The player's volume: an icon, then the settings slider's track, fill and
    // knob; muted, with no box, when Windows has no audio session for the player.
    const int volume = m_controls.dragVolume() >= 0 ? m_controls.dragVolume() : np.volume;
    const float sliderW = charW * VOLUME_SLIDER_CHARS;
    const float sliderX = rowR - sliderW;
    const unsigned long volumeInk = NowPlaying::ink(NowPlayingControls::VOLUME, volume >= 0, false, np, s);
    addIcon(m_icons.sprite[NowPlayingIcons::VOLUME], kMarks[NowPlayingIcons::VOLUME], sliderX - charW * 1.5f, midY, iconSize, volumeInk);
    addBar(sliderX, midY - trackH * 0.5f, sliderW, trackH, track);
    if (volume >= 0) {
        const float share = static_cast<float>(volume) / 100.0f;
        addBar(sliderX, midY - trackH * 0.5f, sliderW * share, trackH, s.accent);
        const float knobX = std::clamp(sliderX + sliderW * share - knobW * 0.5f, sliderX, sliderX + sliderW - knobW);
        addBar(knobX, midY - knobH * 0.5f, knobW, knobH, m_controls.hover() == NowPlayingControls::VOLUME ? hoverInk : s.accent);
        m_controls.place(NowPlayingControls::VOLUME, sliderX, rowY, sliderW, lh);
    }
    m_controls.placedAll();
}

bool NowPlayingControls::inside(Hit hit, float x, float y) const {
    const HitBox& b = m_hit[hit];
    return b.w > 0.0f && x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

float NowPlayingControls::seekShareAt(float x) const {
    const HitBox& p = m_hit[SEEK];
    return p.w > 0.0f ? std::clamp((x - p.x) / p.w, 0.0f, 1.0f) : 0.0f;
}

bool NowPlayingControls::follow(bool live, float x, float y, bool clicked, bool pressed) {
    const bool dragging = m_dragVolume >= 0 || m_dragSeek >= 0.0f;
    // What the pointer is over (a drag keeps its slider lit); a change rebuilds.
    int hover = COUNT;
    if (live) {
        if (m_dragVolume >= 0) hover = VOLUME;
        else if (m_dragSeek >= 0.0f) hover = SEEK;
        else
            for (int i = 0; i < COUNT; ++i)
                if (inside(static_cast<Hit>(i), x, y)) { hover = i; break; }
    }
    const bool hoverMoved = hover != m_hover;
    m_hover = hover;
    if (!live || (dragging && !pressed)) {
        if (!dragging) return hoverMoved;
        // The drag is over. A seek goes out once, on release (not on every move,
        // which would stutter the player); covered or closed, it is dropped.
        if (live && m_dragSeek >= 0.0f && m_durationMs > 0) {
            MediaManager::getInstance().seekTo(
                static_cast<int>(std::lround(m_dragSeek * static_cast<float>(m_durationMs))));
        }
        m_dragVolume = -1;   // back to the player's own reading
        m_dragSeek = -1.0f;
        return true;
    }
    if (!dragging) {
        if (!clicked) return hoverMoved;
        MediaManager& media = MediaManager::getInstance();
        if (inside(SHUFFLE, x, y)) media.sendCommand(MediaManager::Command::TOGGLE_SHUFFLE);
        else if (inside(PREV, x, y)) media.sendCommand(MediaManager::Command::PREVIOUS);
        else if (inside(PLAY, x, y)) media.sendCommand(MediaManager::Command::TOGGLE_PLAY);
        else if (inside(NEXT, x, y)) media.sendCommand(MediaManager::Command::NEXT);
        else if (inside(LOOP, x, y)) media.sendCommand(MediaManager::Command::CYCLE_LOOP);
        if (inside(SEEK, x, y)) {
            m_dragSeek = seekShareAt(x);
            return true;
        }
        if (!inside(VOLUME, x, y)) return hoverMoved;
    }
    if (m_dragSeek >= 0.0f) {
        // A drag that began on the progress line: the fill follows the pointer;
        // the player hears of it on release.
        const float share = seekShareAt(x);
        if (share == m_dragSeek) return hoverMoved;
        m_dragSeek = share;
        return true;
    }
    // A press on the slider, or a drag that began on it: the volume under the
    // pointer, sent when it changes (the worker applies only the newest).
    const HitBox& v = m_hit[VOLUME];
    if (v.w <= 0.0f) return hoverMoved;
    const int percent = std::clamp(static_cast<int>(std::lround((x - v.x) / v.w * 100.0f)), 0, 100);
    if (percent == m_dragVolume) return hoverMoved;
    m_dragVolume = percent;
    MediaManager::getInstance().setVolume(percent);
    return true;
}

// ---- The rules the deck reads too ----------------------------------------------

namespace NowPlaying {

bool hasPlayer(const MediaManager& media, const MediaManager::Snapshot& np) {
    return media.isEnabled() &&
           (np.state == MediaManager::State::PLAYING || np.state == MediaManager::State::PAUSED);
}

const char* emptyLine(const MediaManager& media, const MediaManager::Snapshot& np, bool& off) {
    off = MediaManager::isSupported() && !media.isEnabled();
    return off ? TR("Now playing is off")
         : !media.isEnabled() || np.state == MediaManager::State::UNAVAILABLE ? Placeholders::NOT_AVAILABLE
         : np.state == MediaManager::State::NO_PLAYER ? TR("No player") : Placeholders::GENERIC;
}

float seekShare(const NowPlayingControls& controls, const MediaManager::Snapshot& np) {
    if (controls.dragSeek() >= 0.0f) return controls.dragSeek();
    if (np.durationMs <= 0) return 0.0f;
    return std::clamp(static_cast<float>(np.positionMs) / static_cast<float>(np.durationMs), 0.0f, 1.0f);
}

NowPlayingIcons::Icon icon(int hit, const MediaManager::Snapshot& np) {
    switch (hit) {
        case NowPlayingControls::SHUFFLE: return NowPlayingIcons::SHUFFLE;
        case NowPlayingControls::PREV:    return NowPlayingIcons::PREV;
        case NowPlayingControls::PLAY:
            return np.state == MediaManager::State::PLAYING ? NowPlayingIcons::PAUSE : NowPlayingIcons::PLAY;
        case NowPlayingControls::NEXT:    return NowPlayingIcons::NEXT;
        case NowPlayingControls::LOOP:    return NowPlayingIcons::LOOP;
        default:                          return NowPlayingIcons::VOLUME;
    }
}

bool takes(int hit, const MediaManager::Snapshot& np) {
    switch (hit) {
        case NowPlayingControls::SHUFFLE: return np.canShuffle;
        case NowPlayingControls::LOOP:    return np.canLoop;
        case NowPlayingControls::VOLUME:  return np.volume >= 0;
        default:                          return true;
    }
}

unsigned long hoverInk(const NowPlayingCard::Style& s) { return PluginUtils::lightenColor(s.accent, 0.25f); }

unsigned long ink(int hit, bool taken, bool hovered, const MediaManager::Snapshot& np,
                  const NowPlayingCard::Style& s) {
    if (!taken) return s.muted;
    if (hovered) return hoverInk(s);
    if (hit == NowPlayingControls::SHUFFLE) return np.shuffle ? s.accent : s.secondary;
    if (hit == NowPlayingControls::LOOP) return np.loop != MediaManager::Loop::OFF ? s.accent : s.secondary;
    return s.primary;
}

}  // namespace NowPlaying
