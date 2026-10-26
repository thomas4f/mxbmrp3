// ============================================================================
// hud/now_playing_card.cpp
// The Now Playing rows - see now_playing_card.h.
// ============================================================================
#include "now_playing_card.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/asset_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;

namespace {
// Indexed as NowPlayingCard::m_sprites; the text each draws when its icon is missing.
constexpr const char* kIcons[5] = {
    "hud-media-prev", "hud-media-play", "hud-media-pause", "hud-media-next", "hud-media-volume" };
constexpr const char* kMarks[5] = { "|<", ">", "||", ">|", "Vo" };
// The settings slider's track and knob, as a share of a row (SettingsLayoutContext::addSliderTrack).
constexpr float TRACK_SHARE = 0.08f;
constexpr float TRACK_FOOT = 0.90f;
constexpr float KNOB_H_SHARE = 0.30f;
constexpr float KNOB_W_CHARS = 0.45f;
constexpr float TRACK_OPACITY = 0.45f;
// The buttons: icons this share of a row, one every BUTTON_PITCH_CHARS.
constexpr float BUTTON_ICON_SHARE = 0.75f;
constexpr float BUTTON_PITCH_CHARS = 3.0f;
constexpr float VOLUME_SLIDER_CHARS = 9.0f;
}  // namespace

void NowPlayingCard::addBar(float x, float y, float w, float h, unsigned long color) {
    if (m_primCount >= MAX_PRIMS || w <= 0.0f) return;
    Prim& p = m_prims[m_primCount++];
    p = Prim{};
    p.kind = Prim::Kind::BAR;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.color = color;
}

void NowPlayingCard::addText(const char* text, float x, float y, bool center, unsigned long color) {
    if (m_primCount >= MAX_PRIMS) return;
    Prim& p = m_prims[m_primCount++];
    p = Prim{};
    p.kind = Prim::Kind::TEXT;
    p.x = x; p.y = y;
    p.text = text;
    p.center = center;
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
    for (HitBox& b : m_hit) b = HitBox{};

    const float rowW = rowR - rowL;
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    const unsigned long track = PluginUtils::applyOpacity(s.muted, TRACK_OPACITY);

    const bool hasPlayer = media.isEnabled() &&
        (np.state == MediaManager::State::PLAYING || np.state == MediaManager::State::PAUSED);
    if (!hasPlayer) {
        m_dragVolume = -1;
        // Why there is nothing: the integration off (and where to turn it on), no
        // player, or not polled yet.
        const float centerX = (rowL + rowR) / 2.0f;
        const bool off = MediaManager::isSupported() && !media.isEnabled();
        const char* line = off ? "Now playing is off"
                         : !media.isEnabled() || np.state == MediaManager::State::UNAVAILABLE
                             ? Placeholders::NOT_AVAILABLE
                         : np.state == MediaManager::State::NO_PLAYER ? "No player" : Placeholders::GENERIC;
        addText(line, centerX, contentY + lh * (off ? 0.5f : 1.0f), true, s.muted);
        if (off) addText("Settings > General", centerX, contentY + lh * 1.5f, true, s.tertiary);
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
        const float share = std::clamp(static_cast<float>(np.positionMs) / static_cast<float>(np.durationMs),
                                       0.0f, 1.0f);
        addBar(rowL, progressY, rowW * share, trackH, s.accent);
    }

    // The buttons: icons looked up once per theme (a lookup builds a string).
    if (m_spriteGen != assets.themeGeneration()) {
        m_spriteGen = assets.themeGeneration();
        for (int i = 0; i < 5; ++i) m_sprites[i] = assets.getIconSpriteIndex(kIcons[i]);
    }
    const float rowY = contentY + lh * 2.0f;
    const float midY = rowY + lh * 0.5f;
    const float iconSize = lh * BUTTON_ICON_SHARE;
    const float pitch = charW * BUTTON_PITCH_CHARS;
    const int buttonIcon[3] = { 0, np.state == MediaManager::State::PLAYING ? 2 : 1, 3 };
    for (int i = 0; i < 3; ++i) {
        const float cx = rowL + pitch * (static_cast<float>(i) + 0.5f);
        addIcon(m_sprites[buttonIcon[i]], kMarks[buttonIcon[i]], cx, midY, iconSize, s.primary);
        m_hit[i] = HitBox{ cx - pitch * 0.5f, rowY, pitch, lh };
    }

    // The player's volume: an icon, then the settings slider's track, fill and
    // knob; muted, with no box, when Windows has no audio session for the player.
    const int volume = m_dragVolume >= 0 ? m_dragVolume : np.volume;
    const float sliderW = charW * VOLUME_SLIDER_CHARS;
    const float sliderX = rowR - sliderW;
    const unsigned long volumeInk = volume >= 0 ? s.primary : s.muted;
    addIcon(m_sprites[4], kMarks[4], sliderX - charW * 1.5f, midY, iconSize, volumeInk);
    addBar(sliderX, midY - trackH * 0.5f, sliderW, trackH, track);
    if (volume >= 0) {
        const float share = static_cast<float>(volume) / 100.0f;
        addBar(sliderX, midY - trackH * 0.5f, sliderW * share, trackH, s.accent);
        const float knobW = charW * KNOB_W_CHARS;
        const float knobH = lh * KNOB_H_SHARE;
        const float knobX = std::clamp(sliderX + sliderW * share - knobW * 0.5f, sliderX, sliderX + sliderW - knobW);
        addBar(knobX, midY - knobH * 0.5f, knobW, knobH, s.accent);
        m_hit[HIT_VOLUME] = HitBox{ sliderX, rowY, sliderW, lh };
    }
}

bool NowPlayingCard::inside(Hit hit, float x, float y) const {
    const HitBox& b = m_hit[hit];
    return b.w > 0.0f && x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

bool NowPlayingCard::follow(bool live, float x, float y, bool clicked, bool pressed) {
    if (!live || (m_dragVolume >= 0 && !pressed)) {
        if (m_dragVolume < 0) return false;
        m_dragVolume = -1;   // the drag is over: back to the player's own reading
        return true;
    }
    if (m_dragVolume < 0) {
        if (!clicked) return false;
        MediaManager& media = MediaManager::getInstance();
        if (inside(HIT_PREV, x, y)) media.sendCommand(MediaManager::Command::PREVIOUS);
        else if (inside(HIT_PLAY, x, y)) media.sendCommand(MediaManager::Command::TOGGLE_PLAY);
        else if (inside(HIT_NEXT, x, y)) media.sendCommand(MediaManager::Command::NEXT);
        if (!inside(HIT_VOLUME, x, y)) return false;
    }
    // A press on the slider, or a drag that began on it: the volume under the
    // pointer, sent when it changes (the worker applies only the newest).
    const HitBox& v = m_hit[HIT_VOLUME];
    if (v.w <= 0.0f) return false;
    const int percent = std::clamp(static_cast<int>(std::lround((x - v.x) / v.w * 100.0f)), 0, 100);
    if (percent == m_dragVolume) return false;
    m_dragVolume = percent;
    MediaManager::getInstance().setVolume(percent);
    return true;
}
