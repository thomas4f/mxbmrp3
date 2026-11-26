// ============================================================================
// hud/deck_layout.h
// One theme's deck layout: its drawing of the decks (hud/deck_hud.h): for each
// deck, the boxes it draws, where each of its strings sits and its meters.
// Pure data and arithmetic -- no rendering, no PluginData.
//
// A DECK'S LOOK IS DATA, the argument pitboard_geometry.h makes for a board's
// rows and gauge_geometry.h for a dial's sweep: compiled in, the numbers only
// fit one look, and every other look needs a build. Here the whole drawing is
// numbers -- a rect (or four corners) per box with what paints it, a font, size
// and anchor per string, a rect or an arc per meter -- so a new look is a
// theme's decks.ini (and any deck_ pictures its boxes name).
//
// What stays compiled is what each deck SHOWS (deck_hud_gather.cpp fills
// the fields and meters) and the classic renderer's limits: one colour per
// quad, square pixel edges, every quad under every string. Rounded rims come
// from the active theme's slices (art = panel / card / button), so a layout
// follows the user's theme, dark or light, without art of its own; a lean
// comes from the layout's own white images (art = image), tinted.
//
// All positions here are design pixels at 1080p and scale 1, relative to the
// deck's top left; +x right, +y down. deck.ini states them in the units the
// other layouts use (theme.ini's grid cells, gauge.ini's angles) and the reader
// (asset_manager_decks.cpp) multiplies them out with the constants below;
// finalize() turns boxes into the corner lists the drawer reads, once per
// load, never per frame.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DeckLayout {

// deck.ini's units, in design px: a COLUMN is a grid cell across and a ROW one
// down (a character by half a text row, at the default font), a TEXT SIZE the
// normal text's (the HUDs' tiers are 0.75, 1.5 and 2 of it). They are
// LayoutMetrics' defaults at 1080p, the grid the drawer scales design px by
// (DeckHud::lx/ly), so a whole number of cells lands on the grid lines the
// classic HUDs snap to: test_deck_meter.cpp holds them to LayoutMetrics.
inline constexpr float COLUMN_PX = 10.56f;
inline constexpr float ROW_PX = 11.88f;
inline constexpr float TEXT_PX = 21.6f;
// An arc's angles as gauge.ini states them, 0 straight up and clockwise; the
// drawer's 0 points left (90 up), so the reader adds this.
inline constexpr float ARC_FROM_UP = 90.0f;

// EVERY LENGTH KEY'S UNIT, the one list the reader scales by (iniScale): a
// key's numbers take these units in turn, cycling ("cr": x, y pairs). c = a
// column, r = a row, t = a text size, l = a lean (columns across per row up,
// in px per px). A key not listed is not a length (an opacity, a count, an
// angle). The element is "" for the deck's own section. Pinned key by key in
// test_deck_meter.cpp; a meter's `gap` is the one length left out, its unit
// hanging on the kind (finalize()).
struct IniUnit { const char* element; const char* prop; const char* units; };
inline constexpr IniUnit kIniUnits[] = {
    { "", "size", "cr" }, { "", "row-pitch", "r" }, { "", "row-break", "r" }, { "", "row-lean", "l" },
    { "box", "rect", "cr" }, { "box", "corners", "cr" }, { "box", "skew", "c" }, { "box", "pad", "c" },
    { "box", "border", "r" }, { "box", "glow", "r" },
    { "text", "x", "c" }, { "text", "y", "r" }, { "text", "h", "r" }, { "text", "size", "t" },
    { "text", "fit", "c" }, { "text", "pill-pad", "c" }, { "text", "pill-gap", "r" }, { "text", "pill-h", "r" },
    { "text", "pill-border", "r" }, { "text", "stack-lean", "l" },
    { "meter", "rect", "cr" }, { "meter", "arc", "crrr" }, { "meter", "skew", "c" }, { "meter", "mid", "c" },
    { "meter", "mark", "c" }, { "meter", "over", "r" }, { "meter", "needle", "r" }, { "meter", "needle-in", "r" },
    { "meter", "line", "r" }, { "meter", "well-pad", "r" }, { "meter", "knob", "c" },
};

// What the reader multiplies a key's i-th number by: design px per unit, or
// 1 for a key that is not a length.
inline float iniScale(const char* element, const char* prop, int i) {
    for (const IniUnit& u : kIniUnits) {
        if (std::strcmp(u.element, element) != 0 || std::strcmp(u.prop, prop) != 0) continue;
        switch (u.units[i % static_cast<int>(std::strlen(u.units))]) {
            case 'c': return COLUMN_PX;
            case 'r': return ROW_PX;
            case 't': return TEXT_PX;
            case 'l': return COLUMN_PX / ROW_PX;
            default: return 1.0f;
        }
    }
    return 1.0f;
}

// The decks, in the order the ini's section prefixes name them. The order is
// DeckHud::Kind's, so a kind indexes its deck directly.
enum DeckId : int {
    DECK_SPEED = 0, DECK_RACE, DECK_TIMING, DECK_WHEELIE, DECK_SLIDE, DECK_COMBO,
    DECK_GAP, DECK_RIVAL, DECK_SPIN, DECK_INPUTS, DECK_STANDINGS, DECK_SESSION, DECK_NOTICE,
    DECK_FUEL, DECK_MEDIA, DECK_LAPLOG,
    DECK_COUNT
};
inline constexpr const char* kDeckNames[DECK_COUNT] = {
    "speed", "race", "timing", "wheelie", "slide", "combo",
    "gap", "rival", "spin", "inputs", "standings", "session", "notice", "fuel", "media", "laplog"
};

// The strings a deck can show, one list for every deck: each deck fills its
// own (deck_hud.h lists which). A layout text names one with `show`; a field
// its deck never fills is simply empty and draws nothing.
enum Field : int8_t {
    F_NONE = -1,
    TITLE = 0,      // the deck's name ("SPEED")
    STAMP,          // a word shown for a moment ("NEW BEST"); empty otherwise
    GEAR, SPEED, UNIT,
    // Speed: the rider's stance, a triangle each (an `arrow` text over a
    // one-space string), lit by tone; empty until the game sends a stance
    STANCE_UP, STANCE_DOWN,
    POS_LABEL, POS, LAP_LABEL, LAP,
    CURRENT, DELTA, TRAP, TRAP_DIFF, LAST_LABEL, LAST, BEST_LABEL, BEST,
    // The riding gauges: the angle under the needle, the spell's timer (MAX is
    // BEST_LABEL / BEST); the Combo deck's multiplier and its tricks (pills:
    // the field holds the newest, the words are Rows::pillWord, each its own
    // tone), and the trick's measurements: its time, distance, height and
    // rotation, each a label and a value (fmx_trick_stats.h)
    ANGLE, HOLD, MULT, CHIPS,
    TIME_LABEL, TIME, DIST_LABEL, DIST, HIGH_LABEL, HIGH, ROT_LABEL, ROT,
    // The later decks' shared shape: a caption over a hero value (with a small
    // line under it), and up to two label / value lines beside them
    CAPTION, HERO, SUB, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2,
    // The Notice deck: what is up, in big letters (note, with a detail under
    // it; note-alone, without one), and the detail over up to two lines
    NOTE, NOTE_ALONE, NOTE_DETAIL, NOTE_DETAIL2,
    // The Now Playing deck: the track and its artist (or why there is none),
    // the time played and the time left, and a small "1" by the loop button
    // while it repeats one track
    TRACK, ARTIST, ELAPSED, REMAIN, LOOP_ONE,
    // A list deck's row (Standings, Lap Log): what its row template shows,
    // filled once per row. ROW_PEN is the rider's time penalty ("+5s"), empty
    // without one; ROW_ME is set only on the player's row (Lap Log: the lap
    // running). Both suit a box's `when`. Lap Log's: the lap ("L7"), its
    // sectors (s3 the rest of the lap) and its time.
    ROW_POS, ROW_NUM, ROW_NAME, ROW_GAP, ROW_PEN, ROW_ME,
    ROW_LAP, ROW_S1, ROW_S2, ROW_S3, ROW_TIME,
    FIELD_COUNT
};
inline constexpr const char* kFieldNames[FIELD_COUNT] = {
    "title", "stamp",
    "gear", "speed", "unit",
    "stance-up", "stance-down",
    "pos-label", "pos", "lap-label", "lap",
    "current", "delta", "trap", "trap-diff", "last-label", "last", "best-label", "best",
    "angle", "hold", "mult", "chips",
    "time-label", "time", "dist-label", "dist", "high-label", "high", "rot-label", "rot",
    "caption", "hero", "sub", "line1-label", "line1", "line2-label", "line2",
    "note", "note-alone", "note-detail", "note-detail2",
    "track", "artist", "elapsed", "remain", "loop-one",
    "row-pos", "row-num", "row-name", "row-gap", "row-pen", "row-me",
    "row-lap", "row-s1", "row-s2", "row-s3", "row-time",
};
inline constexpr int ROW_FIELD_FIRST = ROW_POS;
inline constexpr int ROW_FIELD_COUNT = FIELD_COUNT - ROW_POS;

// The meters a deck can draw, the same way.
enum Meter : int8_t {
    M_NONE = -1,
    RPM = 0,        // Speed: revs, hot from the shift point
    PROGRESS,       // Race: the race so far, one part per lap in a lap race
    SECTORS,        // Timing: the lap, one part per sector, each toned when done
    TILT,           // Wheelie: the angle, with the balance zone
    SLIP,           // Slide: the signed angle from the centre, with the clean zone
    CHAIN_TIME,     // Combo: the chain's time left
    GAP_BAR,        // Gap: the gap from the middle, behind to the left, ahead to the right
    RIVALS,         // Rival: the riders either side as marks, by gap, the rival lit
    WHEELSPIN,      // Spin: the rear wheel's slip, the hooked-up zone marked
    THROTTLE,       // Inputs: the throttle now (column) and its history (trace)
    FRONT_BRAKE,    // Inputs: the front brake, the same
    TANK,           // Fuel: what is left of the tank, hot under two laps
    VOLUME,         // Now Playing: the player's volume; drags
    TRACK_POS,      // Now Playing: the track played so far; drags to seek
    METER_COUNT
};
// The bar a deck runs along its foot, which Settings > Decks > Bars can drop
// (DeckHud::setBars); every other meter is its deck's readout.
constexpr bool isBar(Meter m) {
    return m == PROGRESS || m == SECTORS || m == CHAIN_TIME || m == GAP_BAR || m == RIVALS || m == TANK ||
           m == TRACK_POS;
}
inline constexpr const char* kMeterNames[METER_COUNT] = {
    "rpm", "progress", "sectors", "tilt", "slip", "chain",
    "gap", "rivals", "spin",
    "throttle", "front-brake", "tank", "volume", "track"
};

// WHAT EACH DECK FILLS: its fields and its meters, a bit each, in DeckId
// order. The one list of them: a deck sets only these (DeckHud::setText
// drops any other), the reader warns on a layout naming any other, and the
// Cabinet Dark's decks.ini lists them per deck for a layout author, held to this
// table by test_deck_meter.cpp. Every deck has its title and a stamp.
using FieldMask = uint64_t;
using MeterMask = uint32_t;
static_assert(FIELD_COUNT <= 64 && METER_COUNT <= 32, "one bit per field and per meter");
template <typename... F>
constexpr FieldMask fields(F... f) { return (((FieldMask{1} << TITLE) | (FieldMask{1} << STAMP)) | ... | (FieldMask{1} << f)); }
template <typename... M>
constexpr MeterMask meters(M... m) { return (MeterMask{0} | ... | (MeterMask{1} << m)); }
inline constexpr FieldMask kDeckFields[DECK_COUNT] = {
    fields(GEAR, SPEED, UNIT, STANCE_UP, STANCE_DOWN),                                   // speed
    fields(POS_LABEL, POS, SUB, LAP_LABEL, LAP, LINE1),                                  // race
    fields(CAPTION, CURRENT, DELTA, TRAP, TRAP_DIFF, LAST_LABEL, LAST, BEST_LABEL, BEST), // timing
    fields(ANGLE, HOLD, BEST_LABEL, BEST),                                               // wheelie
    fields(ANGLE, HOLD, BEST_LABEL, BEST),                                               // slide
    fields(CAPTION, HERO, MULT, CHIPS, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2,           // combo
           TIME_LABEL, TIME, DIST_LABEL, DIST, HIGH_LABEL, HIGH, ROT_LABEL, ROT),
    fields(CAPTION, HERO, SUB, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2),                  // gap
    fields(CAPTION, HERO, SUB, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2),                  // rival
    fields(HERO, HOLD, BEST_LABEL, BEST),                                                // spin
    fields(),                                                                            // inputs
    fields(ROW_POS, ROW_NUM, ROW_NAME, ROW_GAP, ROW_PEN, ROW_ME),                        // standings
    fields(CAPTION, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2),                             // session
    fields(NOTE, NOTE_ALONE, NOTE_DETAIL, NOTE_DETAIL2),                                 // notice
    fields(CAPTION, HERO, LINE1_LABEL, LINE1, LINE2_LABEL, LINE2),                       // fuel
    fields(TRACK, ARTIST, ELAPSED, REMAIN, LOOP_ONE),                                    // media
    fields(ROW_LAP, ROW_S1, ROW_S2, ROW_S3, ROW_TIME, ROW_ME),                           // laplog
};
inline constexpr MeterMask kDeckMeters[DECK_COUNT] = {
    meters(RPM), meters(PROGRESS), meters(SECTORS), meters(TILT), meters(SLIP), meters(CHAIN_TIME),
    meters(GAP_BAR), meters(RIVALS), meters(WHEELSPIN), meters(THROTTLE, FRONT_BRAKE), meters(),
    meters(), meters(), meters(TANK), meters(VOLUME, TRACK_POS), meters(),
};
inline constexpr bool deckFills(int deck, int field) {
    return deck >= 0 && deck < DECK_COUNT && field >= 0 && field < FIELD_COUNT &&
           (kDeckFields[deck] >> field & 1u) != 0;
}
inline constexpr bool deckHasMeter(int deck, int meter) {
    return deck >= 0 && deck < DECK_COUNT && meter >= 0 && meter < METER_COUNT &&
           (kDeckMeters[deck] >> meter & 1u) != 0;
}

// The most words a `pills` text shows (the Combo deck's tricks, newest kept),
// and the bytes each holds (a whole trick name: test_deck_meter.cpp).
inline constexpr int MAX_PILLS = 8;
inline constexpr int PILL_WORD = 24;

// How a value is coloured beyond its text's own colour: the deck says what a
// value means, the layout says what that looks like.
enum Tone : uint8_t { TONE_NORMAL = 0, TONE_EMPTY, TONE_HOT, TONE_GOOD, TONE_BAD, TONE_BEST };
// A signed time delta's tone, deltaColorSlot's rule (color_config.h): ahead
// (negative) good, behind bad, even normal
constexpr Tone deltaTone(int deltaMs) { return deltaMs < 0 ? TONE_GOOD : (deltaMs > 0 ? TONE_BAD : TONE_NORMAL); }

enum Align : int8_t { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT };

// What paints a box. The theme's slices follow the user's theme; SOLID is a
// flat quad in the box's colour; IMAGE is one .tga from the layout's own folder
// stretched over the box and tinted with its colour (white art follows any
// palette). The game filters textures but draws quad edges hard, so a smooth
// slant is painted into an image on an upright box, never a skew. ICON is
// the deck's own icon (the Notice deck's: the notice's, the achievement's
// marker) stretched over the box in the deck's colour for it; nothing while
// the deck has none. TITLE is the theme's title-bar slices (its card's where
// it has none): a theme's own name tab. CONTROL is one of the Now Playing
// deck's buttons (Box::control): its icon over the box, which is also what
// a click lands on, in the colour the deck gives its state.
enum Art : int8_t { ART_SOLID = 0, ART_PANEL, ART_CARD, ART_BUTTON, ART_NONE, ART_IMAGE, ART_ICON, ART_TITLE, ART_CONTROL };

// The buttons an `art = control` box can be, in NowPlayingControls::Hit's
// order; volume is its slider's icon, drawn but not a button.
enum Control : int8_t { CONTROL_NONE = -1, CONTROL_SHUFFLE = 0, CONTROL_PREV, CONTROL_PLAY, CONTROL_NEXT,
                        CONTROL_LOOP, CONTROL_VOLUME, CONTROL_COUNT };
inline constexpr const char* kControlNames[CONTROL_COUNT] = { "shuffle", "prev", "play", "next", "loop", "volume" };

// An image as the ini names it (its .tga stem in the layout folder) and the
// sprite the reader resolved it to (0 = none: drawn solid instead).
inline constexpr int IMAGE_NAME = 32;

// trace: the meter's history (the Telemetry HUD's, 2 s) as a line across the
// rect, the newest at the right. column: a bar filled from the bottom.
enum MeterKind : int8_t { METER_BAR = 0, METER_BLOCKS, METER_ARC, METER_TRACE, METER_COLUMN };

// A small triangle drawn before a text, in its colour.
enum Arrow : int8_t { ARROW_NONE = 0, ARROW_UP, ARROW_DOWN };

inline constexpr int MAX_BOXES = 16;
inline constexpr int MAX_TEXTS = 24;
inline constexpr int MAX_METERS = 4;
inline constexpr int MAX_ROWS = 12;
// A section's name ([speed-box-plate]): drawn in the file's order, a name
// taking the next place after every element before it. Bytes, with the end.
inline constexpr int SECTION_NAME = 24;

// A colour as the ini names it: a palette slot (ColorSlot index, so it follows
// the user's colours and theme) or a fixed #rrggbb. Resolved by the reader.
struct Color {
    int slot = -1;           // >= 0: a ColorSlot
    uint32_t abgr = 0;       // slot < 0: this, alpha full
    bool set = false;        // false: the field's default applies
};

struct Box {
    bool present = false;
    // rect = x, y, w, h; skew = px leans it like italic type (top edge right).
    // corners = eight numbers instead states TL, BL, BR, TR outright.
    float rect[4] = {};
    float skew = 0.0f;
    bool hasCorners = false;
    float corners[8] = {};
    Art art = ART_SOLID;
    char image[IMAGE_NAME] = {};   // art = image: the .tga stem
    int sprite = 0;
    Color color;             // default: background (button: accent)
    float opacity = 1.0f;    // its share of the deck's opacity setting
    // when = a field: drawn only while that field shows something (a stamp's
    // plate, a delta's chip). tint = 1 colours it with that field's tone.
    Field when = F_NONE;
    bool tint = false;
    // hug = a field: the box spans that field's drawn string plus `pad` each
    // side (its rect's y and h stay), so a badge fits whatever word it holds.
    // An upright solid box only.
    Field hug = F_NONE;
    float pad = 0.0f;
    // hug-to = a field drawn after `hug`: the box runs on to that one's end
    // while it shows (a chip holding two strings, "84 KM/H" and "+2").
    Field hugTo = F_NONE;
    // A rim: `border` px inside the box's edge, in border-color (default
    // muted) at border-opacity. An upright box only.
    float border = 0.0f;
    Color borderColor;
    float borderOpacity = 1.0f;
    // row = 1: part of the deck's row template (Deck::rows), drawn once per
    // row, row-pitch px further down each time.
    bool row = false;
    // control = a button (art = control): which one
    Control control = CONTROL_NONE;
    // glow = px: a halo reaching that far past the box while the deck raises
    // one (the Notice deck, under a notice that calls for action), in the
    // deck's colour for it; Appearance > Glow switches it.
    float glow = 0.0f;
    // caps = 1: where the theme has slanted frame ends (ThemeAsset::hasSlantCaps)
    // the box draws the theme's frame leaning, its ends those pictures, instead
    // of its own art; uncapped = 1: drawn only where the theme has none (a
    // layout's own outline, which the theme's frame replaces). Upright boxes only.
    bool caps = false;
    bool uncapped = false;

    // finalize(): TL, BL, BR, TR as x,y pairs.
    float quad[8] = {};
    bool upright = true;     // finalize(): an axis-aligned rect (theme art needs one)
};

inline constexpr float FIT_MIN_SHARE = 0.6f;
inline constexpr float TAIL_OPACITY = 0.7f;

struct Text {
    bool present = false;
    Field field = F_NONE;
    // The anchor: x is where the string starts, centres or ends (align); the
    // string's ink is centred in the band y .. y + h, and a face whose figures
    // stand taller than h (AssetManager::digitInk) is drawn smaller to fit it.
    float x = 0.0f, y = 0.0f, h = 0.0f;
    float size = 20.0f;      // font size, design px
    // The widest the string may run, design px (0 = no limit): a longer one,
    // a translation say, shrinks to as little as FIT_MIN_SHARE of `size`, and
    // past that is cut. On a rebuild, never per frame.
    float fit = 0.0f;
    Align align = ALIGN_LEFT;
    // tail = a share of size: the string from its first '/' or letter on
    // ("3/22", "2.4s") is drawn that much smaller, on the same baseline, at
    // TAIL_OPACITY. 0 = one size.
    float tail = 0.0f;
    Arrow arrow = ARROW_NONE;
    // after = a field drawn earlier in the list: this text starts `x` px past
    // that one's end ("1,840" then "x3"), left-aligned. Where that field is
    // empty, x is from the deck's left as usual.
    Field after = F_NONE;
    // before = a field anywhere in the list: this text ends `x` px short of
    // that one's start ("MAX" then "2.4s"), placed once every other text is.
    // Where that field is empty, it stands where x and align put it.
    Field before = F_NONE;
    // pills = 1: the deck's words (the Combo deck's tricks, oldest first),
    // each on its own pill: pill-color (default muted) at pill-opacity, or
    // pill-image tinted so, with a pill-border rim (pill-border-color,
    // pill-border-opacity) on a plain one; pill-pad each side, pill-h tall,
    // centred in the band. A word the deck tones (the trick turning, a chain
    // landed or lost) takes its tone's colour, and its pill takes that
    // colour at pill-tint over the fill (0: none).
    //   In a row (stack = 0), pill-gap apart: with a `fit`, the newest that
    // fit are shown. stack = N: a column instead, the newest in the band and
    // each older one pill-h + pill-gap higher, stack-lean px further right
    // per px up (a leaning deck's edge). N whole pills stand; the next fades
    // out as it rises past them. A new one rises in at the bottom, sliding
    // the rest up with Motion on (snapping with it off).
    bool pills = false;
    Color pillColor;
    float pillOpacity = 1.0f;
    float pillTint = 0.0f;
    float pillPad = 6.0f, pillGap = 5.0f, pillH = 18.0f;
    char pillImage[IMAGE_NAME] = {};
    int pillSprite = 0;                // pillImage's, resolved by the reader
    float pillBorder = 0.0f;
    Color pillBorderColor;
    float pillBorderOpacity = 1.0f;
    int stack = 0;
    float stackLean = 0.0f;
    // font = a category: title, normal, strong, digits, marker or small. Its
    // face is the player's pick for it where they made one (Appearance, or
    // the deck's own), else the layout's [fonts] face for it, else the theme's.
    int fontCategory = 0;    // FontCategory
    Color color;             // default: primary
    Color emptyColor;        // TONE_EMPTY; default muted
    Color hotColor;          // TONE_HOT; default negative
    Color goodColor;         // TONE_GOOD; default positive
    Color badColor;          // TONE_BAD; default negative
    Color bestColor;         // TONE_BEST; default positive
    bool row = false;        // part of the row template, as Box::row
};

struct MeterEl {
    bool present = false;
    Meter meter = M_NONE;
    MeterKind kind = METER_BAR;
    // bar, blocks, trace and column: the strip's rect. arc: unused.
    float rect[4] = {};
    int count = 0;           // blocks / arc: how many; 0 = one per part the deck gives
    float gap = 3.0f;        // between parts or blocks, px (arc: degrees)
    // gap as deck.ini states it: columns, or degrees on an arc; < 0 = unset.
    // finalize() multiplies it out once the kind is known (a skin may change
    // either).
    float gapAsWritten = -1.0f;
    // bar, blocks, trace and column: px the top edge is moved right of the bottom, so
    // the parts lean and still meet. Solid: a strip a few px tall steps too
    // little to need painted art.
    float skew = 0.0f;
    // arc: centre, outer radius and thickness (px), the start angle and the
    // sweep (degrees: 0 = pointing left, 90 = up, 180 = right; deck.ini's
    // `from` is 0 up, ARC_FROM_UP less).
    float arc[4] = {};       // cx, cy, radius, thickness
    float from = 0.0f, sweep = 180.0f;
    Color color;             // lit; default accent
    Color dimColor;          // unlit; default muted
    float dimOpacity = 0.35f;
    Color hotColor;          // past the hot point; default negative
    Color zoneColor;         // inside the zone; default positive
    Color goodColor;         // a part toned good; default positive
    Color badColor;          // a part toned bad; default negative
    // bar: a tick across the middle of a centred meter, `mid` px wide (0 =
    // none), in mid-color (default primary); it and the deck's marks stand
    // `over` px past the strip's top and bottom. A mark is `mark` px wide
    // (0 = as wide as the strip is tall), lit in color, unlit in dim-color.
    float mid = 0.0f;
    Color midColor;
    float mark = 0.0f;
    float over = 2.0f;
    // arc: a band meter's needle at the value, `needle` px wide (0 = none),
    // from `needle-in` px inside the inner edge to `over` px past the outer;
    // in the colour of the band it is in, needle-color (default primary) in
    // the plain run. Past the danger point: danger-color (default negative).
    float needle = 0.0f;
    float needleIn = 0.0f;
    Color needleColor;
    Color dangerColor;
    // trace: the line's width, px.
    float line = 2.0f;
    // bar and blocks: a knob `knob` px wide (0 = none) at the value, standing
    // `over` past the strip, while the deck says the meter drags (the Now
    // Playing deck's volume, and its track when the player takes a seek), in
    // knob-color (default accent; lightened under the pointer)
    float knob = 0.0f;
    Color knobColor;
    // well = art under the strip (bar, blocks, trace, column): its rect and
    // lean grown by well-pad px, in that art (card, title, button, panel, solid
    // in well-color; well-image = a layout image, art = image). Drawn after the
    // boxes and before the meter, at the deck's opacity. none = no well.
    Art wellArt = ART_NONE;
    char wellImage[IMAGE_NAME] = {};
    int wellSprite = 0;
    float wellPad = 2.0f;
    Color wellColor;         // default background
};

struct Deck {
    float width = 0.0f, height = 0.0f;   // the panel; a deck without a size is not drawn
    // A list deck (Standings): up to `rows` rows, each the row template (the
    // boxes and texts with row = 1) drawn row-pitch px below the last. Where
    // the list skips places (the leader, then the riders around you), the rows
    // after the skip move a further row-break px down. Rows on one leaning
    // card (Slanted) follow its lean: each row-lean px left per px down.
    int rows = 0;
    float rowPitch = 0.0f, rowBreak = 0.0f, rowLean = 0.0f;
    Box boxes[MAX_BOXES];
    Text texts[MAX_TEXTS];
    MeterEl meters[MAX_METERS];
    int boxCount = 0;        // highest number used; gaps are not present
    int textCount = 0;
    int meterCount = 0;
};

// Each place's section name, "" for a numbered one: a skin's section of the
// same name changes that element, a new name adds one after the rest. Only
// the reader needs them, so they stay out of Deck (which a list deck copies
// on every rebuild): DeckGeometry holds them, so a skin starts from its
// base's, and a reload resets them with the rest.
struct SectionNames {
    char box[DECK_COUNT][MAX_BOXES][SECTION_NAME] = {};
    char text[DECK_COUNT][MAX_TEXTS][SECTION_NAME] = {};
    char meter[DECK_COUNT][MAX_METERS][SECTION_NAME] = {};
};

// FontCategory's count, without the header (asset_manager_decks.cpp asserts it)
inline constexpr int FONT_CATEGORIES = 6;

struct DeckGeometry {
    Deck decks[DECK_COUNT];
    SectionNames names;
    // [fonts] <category> = <font>: the layout's own face for each category, a
    // font index (0 = none: the theme's). A theme's font set, for the decks.
    int fonts[FONT_CATEGORIES] = {};
};

// A row's y offset in a list deck: `row` rows down, plus the break once past
// the skip (breakAt: the first row after it, -1 none).
inline float rowOffset(const Deck& d, int row, int breakAt) {
    return static_cast<float>(row) * d.rowPitch + (breakAt >= 0 && row >= breakAt ? d.rowBreak : 0.0f);
}

// Corner lists from the raw fields, and each meter's gap in its kind's unit.
// Idempotent: it reads only raw fields, so a skin that overrides a rect (or a
// meter's kind) re-finalizes cleanly.
inline void finalize(DeckGeometry& g) {
    for (Deck& d : g.decks) {
        for (int i = 0; i < d.boxCount; ++i) {
            Box& b = d.boxes[i];
            if (!b.present) continue;
            if (b.hasCorners) {
                for (int k = 0; k < 8; ++k) b.quad[k] = b.corners[k];
                b.upright = b.quad[0] == b.quad[2] && b.quad[4] == b.quad[6] &&
                            b.quad[1] == b.quad[7] && b.quad[3] == b.quad[5];
                continue;
            }
            const float x = b.rect[0], y = b.rect[1], w = b.rect[2], h = b.rect[3];
            // Inside the rect's width: a right lean puts the top edge at
            // x + skew .. x + w and the bottom at x .. x + w - skew.
            const float top = b.skew > 0.0f ? b.skew : 0.0f;
            const float bottom = b.skew < 0.0f ? -b.skew : 0.0f;
            const float c[8] = { x + top, y, x + bottom, y + h, x + w - top, y + h, x + w - bottom, y };
            for (int k = 0; k < 8; ++k) b.quad[k] = c[k];
            b.upright = b.skew == 0.0f;
        }
        for (int i = 0; i < d.meterCount; ++i) {
            MeterEl& m = d.meters[i];
            if (m.gapAsWritten >= 0.0f) m.gap = m.kind == METER_ARC ? m.gapAsWritten : m.gapAsWritten * COLUMN_PX;
        }
    }
}

}  // namespace DeckLayout
