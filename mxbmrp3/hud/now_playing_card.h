// ============================================================================
// hud/now_playing_card.h
// The Now Playing rows, shared by the Now Playing widget and the Dash's Now
// Playing page (what Windows' media controls are playing; core/media_manager.h):
// the title; the artist with the track's progress along the row's foot, where
// the settings sliders sit; then previous / play-pause / next and the player's
// volume as a slider like the settings one.
//
// ONE LAYOUT, TWO PANELS. layout() runs on a rebuild only: it copies the media
// snapshot, cuts the text to the row (by the panel font's glyph widths,
// AssetManager::fitChars) and records the rows as a short list of
// primitives (bars, strings, icons) plus the buttons' hit boxes. Each panel
// replays the list through its own add calls in its own colours and font, so
// the widget and the page cannot drift apart. follow() reads the clicks and the
// volume drag once a frame against the boxes the last layout recorded; nothing
// is formatted there.
// ============================================================================
#pragma once

#include <cstdint>

#include "../core/media_manager.h"

class NowPlayingCard {
public:
    // The rows the card takes, each one normal line high.
    static constexpr int ROWS = 3;

    // The panel's colours, read through its own getColor (so its overrides hold).
    struct Style {
        unsigned long primary = 0, secondary = 0, tertiary = 0, muted = 0, accent = 0;
    };

    struct Prim {
        enum class Kind : uint8_t { BAR, TEXT, ICON };
        Kind kind = Kind::BAR;
        float x = 0.0f, y = 0.0f;   // BAR: top-left; TEXT: anchor, row top; ICON: centre
        float w = 0.0f, h = 0.0f;   // BAR: size; ICON: h = size
        unsigned long color = 0;
        int sprite = 0;             // ICON
        const char* text = nullptr; // TEXT: the card's own buffer or a literal
        bool center = false;        // TEXT: centred on x, else left from x
    };

    // True when MediaManager's snapshot moved since the last layout.
    bool changed() const { return MediaManager::getInstance().revision() != m_shownRevision; }

    // A rebuild: the rows from rowL to rowR, from contentY down, at this line
    // height and font size, the text in `font` (the engine index the panel
    // draws it in, so the title and artist are cut by its glyphs' widths).
    void layout(float rowL, float rowR, float contentY, float lineH, float fontSize, int font, const Style& style);

    // Once a frame, in the panel's build space. live: the panel is showing the
    // card, the pointer is up and nothing covers it. Sends a click on a button,
    // and the volume under the pointer while a press that began on the slider
    // lasts. Returns true when the panel should rebuild (the slider moved).
    bool follow(bool live, float x, float y, bool clicked, bool pressed);

    const Prim* begin() const { return m_prims; }
    const Prim* end() const { return m_prims + m_primCount; }
    int primCount() const { return m_primCount; }

private:
    enum Hit : uint8_t { HIT_PREV, HIT_PLAY, HIT_NEXT, HIT_VOLUME, HIT_COUNT };
    struct HitBox { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
    static constexpr int MAX_PRIMS = 16;

    void addBar(float x, float y, float w, float h, unsigned long color);
    void addText(const char* text, float x, float y, bool center, unsigned long color);
    void addIcon(int sprite, const char* mark, float cx, float cy, float size, unsigned long color);
    bool inside(Hit hit, float x, float y) const;

    Prim m_prims[MAX_PRIMS];
    int m_primCount = 0;
    HitBox m_hit[HIT_COUNT];
    uint32_t m_shownRevision = 0xFFFFFFFFu;
    int m_dragVolume = -1;   // the slider's value while a drag lasts, -1 = none
    int m_sprites[5] = {};   // previous, play, pause, next, volume
    unsigned int m_spriteGen = 0xFFFFFFFFu;
    char m_title[sizeof(MediaManager::Snapshot::title)] = "";
    char m_artist[sizeof(MediaManager::Snapshot::artist)] = "";
};
