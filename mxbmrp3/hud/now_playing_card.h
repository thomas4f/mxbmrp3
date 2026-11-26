// ============================================================================
// hud/now_playing_card.h
// The Now Playing rows, shared by the Now Playing widget and the Dash's Now
// Playing page (what Windows' media controls are playing; core/media_manager.h):
// the title; the artist with the track's progress along the row's foot, where
// the settings sliders sit (a drag there seeks, sent on release); then
// shuffle / previous / play-pause / next / loop and the player's volume as a
// slider like the settings one. Shuffle and loop are lit in the accent when on
// (loop over one track adds a small raised "1"), and muted, with no button, when the player
// does not take them; the progress line seeks only when it takes a position,
// and then carries a knob like the volume's. What the pointer is over is drawn
// in the accent, lightened, as the settings' links are.
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

// The controls' clicks and drags, against hit boxes a layout placed: shared by
// the card (the widget and the Dash page) and the Now Playing deck, whose pack
// places them instead. follow() reads the pointer once a frame; nothing is
// formatted there.
class NowPlayingControls {
public:
    // The buttons in their row's order, then the two sliders.
    enum Hit : uint8_t { SHUFFLE, PREV, PLAY, NEXT, LOOP, VOLUME, SEEK, COUNT };

    // A layout: forget the boxes, then place the ones the player takes, in the
    // panel's build space. durationMs: the track's length, for the seek.
    void clear(int durationMs);
    void place(Hit hit, float x, float y, float w, float h);
    // Once every box is placed: a drag on a slider that is no longer there ends.
    void placedAll();
    bool placed(Hit hit) const { return m_hit[hit].w > 0.0f; }

    // Once a frame. live: the panel is showing the controls, the pointer is up
    // and nothing covers it. Sends a click on a button, the volume under the
    // pointer while a press that began on the slider lasts, and the position
    // under it when a press that began on the progress line ends. Returns true
    // when the panel should rebuild (a slider moved, or the pointer came onto
    // or off a control).
    bool follow(bool live, float x, float y, bool clicked, bool pressed);

    // What the pointer is over (COUNT: nothing), and a slider's value while a
    // drag on it lasts (-1: none), for the layout to draw.
    int hover() const { return m_hover; }
    int dragVolume() const { return m_dragVolume; }
    float dragSeek() const { return m_dragSeek; }

private:
    struct HitBox { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
    bool inside(Hit hit, float x, float y) const;
    float seekShareAt(float x) const;   // the progress line's share under x, 0..1

    HitBox m_hit[COUNT];
    int m_dragVolume = -1;      // the slider's value while a drag lasts, -1 = none
    float m_dragSeek = -1.0f;   // the progress line's share while a drag lasts, -1 = none
    int m_hover = COUNT;        // the control under the pointer, COUNT = none
    int m_durationMs = 0;       // the track's length at the last layout, for the seek
};

// The controls' icons, looked up once per theme (a lookup builds a string).
struct NowPlayingIcons {
    enum Icon : int { PREV = 0, PLAY, PAUSE, NEXT, VOLUME, SHUFFLE, LOOP, COUNT };
    int sprite[COUNT] = {};
    // The text each draws in place of an icon this install lacks
    static const char* mark(Icon icon);
    void refresh();
private:
    unsigned int m_gen = 0xFFFFFFFFu;
};

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
        bool smallText = false;     // TEXT: in the panel's small size, not its normal one
    };

    // True when MediaManager's snapshot moved since the last layout.
    bool changed() const { return MediaManager::getInstance().revision() != m_shownRevision; }

    // A rebuild: the rows from rowL to rowR, from contentY down, at this line
    // height and font size, the text in `font` (the engine index the panel
    // draws it in, so the title and artist are cut by its glyphs' widths).
    void layout(float rowL, float rowR, float contentY, float lineH, float fontSize, int font, const Style& style);

    // Once a frame, in the panel's build space (NowPlayingControls::follow).
    bool follow(bool live, float x, float y, bool clicked, bool pressed) {
        return m_controls.follow(live, x, y, clicked, pressed);
    }

    const Prim* begin() const { return m_prims; }
    const Prim* end() const { return m_prims + m_primCount; }
    int primCount() const { return m_primCount; }

private:
    static constexpr int MAX_PRIMS = 20;

    void addBar(float x, float y, float w, float h, unsigned long color);
    void addText(const char* text, float x, float y, bool center, unsigned long color, bool smallText = false);
    void addIcon(int sprite, const char* mark, float cx, float cy, float size, unsigned long color);

    Prim m_prims[MAX_PRIMS];
    int m_primCount = 0;
    NowPlayingControls m_controls;
    NowPlayingIcons m_icons;
    uint32_t m_shownRevision = 0xFFFFFFFFu;
    char m_title[sizeof(MediaManager::Snapshot::title)] = "";
    char m_artist[sizeof(MediaManager::Snapshot::artist)] = "";
};

// The rules the card and the Now Playing deck both read, so the two panels
// cannot tell one player apart.
namespace NowPlaying {
// A player is up to show and steer: the integration on, a track playing or paused.
bool hasPlayer(const MediaManager& media, const MediaManager::Snapshot& np);
// The line in the track's place with no player, and why: the integration off
// (`off`: the panel adds where to turn it on), not available, no player, or
// not polled yet.
const char* emptyLine(const MediaManager& media, const MediaManager::Snapshot& np, bool& off);
// The share of the track played, from the drag's position while one lasts
// (the track has a length).
float seekShare(const NowPlayingControls& controls, const MediaManager::Snapshot& np);
// A control's icon (play turns to pause while playing), SHUFFLE..VOLUME.
NowPlayingIcons::Icon icon(int hit, const MediaManager::Snapshot& np);
// Whether the player takes a control (shuffle, loop, a volume to set).
bool takes(int hit, const MediaManager::Snapshot& np);
// What the pointer is over: the accent, lightened, as the settings' links are.
unsigned long hoverInk(const NowPlayingCard::Style& s);
// A control's ink: muted where it is not taken, the accent lightened under the
// pointer, primary for a plain one, a toggle the accent when on and secondary
// when off.
unsigned long ink(int hit, bool taken, bool hovered, const MediaManager::Snapshot& np,
                  const NowPlayingCard::Style& s);
}  // namespace NowPlaying
