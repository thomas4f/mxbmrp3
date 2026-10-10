// ============================================================================
// hud/digit_roll.h
// Digit roll, part of Motion (Settings > Appearance > Motion): when the gear
// or the speedo's odometer or trip meter changes, the old text slides out and
// fades while the new one slides in from the other side and fades in.
// Counting up, the old rises and the new comes up from below, like an
// odometer drum; counting down, the reverse. The gear rolls the other way (up
// a gear, the new one comes down from above), so it passes the opposite dir.
//
// NOTHING IS CLIPPED. The game draws a string whole, in one colour, so a digit
// cannot be cut at a panel's edge or faded across its height. Instead each
// text is gone - alpha 0 - before it has travelled a third of the font size,
// so it stays close to where it sits.
//
// A WHOLE STRING ROLLS, never part of one: placing part of a string means
// knowing the font's advances, and the shipped fonts do not share one digit
// width. The odometer keeps its last digit in a string of its own already, so
// that digit rolls on every step and the rest only on a carry.
//
// Pure timing and state; each widget draws the Pieces it is handed, so a
// settled roll is the one string the widget always drew.
//
// Pinned by tests/unit/test_digit_roll.cpp; the widget side by
// tests/integration/tests/digit_roll_test.cpp.
// ============================================================================
#pragma once

#include <cstring>

namespace DigitRoll {

constexpr long long DURATION_US = 200000;
// How far a text travels over the whole roll, as a share of its font size.
constexpr float TRAVEL = 0.45f;
// The old text fades out over the first FADE share of the roll, the new in
// over the last FADE share, so they cross in the middle.
constexpr float FADE = 0.6f;
// Until the two fades cross, halfway, the new text is faint at most, so a
// further change takes its place rather than starting a roll from it.
constexpr long long SWAP_US = DURATION_US / 2;
// Longest text a Roller holds, terminator included.
constexpr int MAX_TEXT = 16;

constexpr float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
constexpr float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Where a gear label sits in the shift order, for the roll's direction: the
// pattern is 1-N-2-3..., so N ranks between 1 and 2. -1 = not a gear (the
// placeholder, "D").
inline int gearRank(const char* label) {
    if (!label || !label[0]) return -1;
    if (label[0] == 'N' && label[1] == '\0') return 3;
    int v = 0;
    for (const char* c = label; *c; ++c) {
        if (*c < '0' || *c > '9') return -1;
        v = v * 10 + (*c - '0');
    }
    return 2 * v;
}

// The two texts at progress p (0..1). dir: +1 counting up (the old rises, the
// new comes from below), -1 counting down. Offsets are in font sizes, positive
// down the screen.
struct Frame { float outDy, outAlpha, inDy, inAlpha; };

constexpr Frame at(float p, int dir) {
    const float t = clamp01(p);
    const float e = smooth(t);
    const float d = -static_cast<float>(dir) * TRAVEL;
    return Frame{ d * e, 1.0f - clamp01(t / FADE), -d * (1.0f - e), clamp01((t - (1.0f - FADE)) / FADE) };
}

// One string to draw: y offset in font sizes, alpha 0..1, and its colour.
struct Piece { const char* text; float dy; float alpha; unsigned long color; };
constexpr int MAX_PIECES = 2;

// The text a readout shows, and the roll from the text it showed before.
// update() once per rebuild, then draw what pieces() returns.
class Roller {
public:
    // `text` is what to show now, in `color`. A change from what is on show
    // rolls in direction `dir` (+1/-1), or switches at once when dir is 0 - the
    // caller passes 0 for a placeholder, a different rider, a jump, or Motion
    // off. The text rolling out keeps the colour it last had: the gear turns
    // red at the shift point and the RPM drops the moment you shift, so this
    // frame's colour would turn the old digit white as it leaves.
    void update(const char* text, int dir, long long nowUs, unsigned long color) {
        if (std::strncmp(text, m_shown, MAX_TEXT - 1) == 0) {
            m_color = color;
            return;
        }
        if (dir != 0 && m_shown[0] != '\0') {
            // A change before the text it replaces has shown (two quick shifts):
            // the old text keeps rolling out and the newest takes the faint
            // one's place, so no text pops in opaque. Back to the text still
            // rolling out, the roll ends and that text stays.
            const int d = dir > 0 ? 1 : -1;
            const bool early = active(nowUs) && nowUs - m_startUs < SWAP_US;
            if (early && std::strncmp(text, m_leaving, MAX_TEXT - 1) == 0) {
                m_startUs = -1;
            } else if (!early || d != m_dir) {
                std::memcpy(m_leaving, m_shown, sizeof(m_leaving));
                m_leavingColor = m_color;
                m_dir = d;
                m_startUs = nowUs;
            }
        } else {
            m_startUs = -1;
        }
        // A bounded copy by hand: strncpy is C4996 (an error) in the MSVC
        // build, and strncpy_s does not exist in the Linux unit build.
        int i = 0;
        for (; i < MAX_TEXT - 1 && text[i]; ++i) m_shown[i] = text[i];
        m_shown[i] = '\0';
        m_color = color;
    }

    bool active(long long nowUs) const {
        return m_startUs >= 0 && nowUs >= m_startUs && nowUs - m_startUs < DURATION_US;
    }

    // Forget what was shown: the next update() switches.
    void reset() { m_shown[0] = '\0'; m_startUs = -1; }

    const char* shown() const { return m_shown; }

    // The strings to draw now, into out[MAX_PIECES]; returns how many. Settled,
    // that is the shown text alone, in place and opaque.
    int pieces(long long nowUs, Piece* out) const {
        if (!active(nowUs)) {
            out[0] = Piece{ m_shown, 0.0f, 1.0f, m_color };
            return 1;
        }
        const Frame f = at(static_cast<float>(nowUs - m_startUs) / static_cast<float>(DURATION_US), m_dir);
        int n = 0;
        if (f.outAlpha > 0.0f) out[n++] = Piece{ m_leaving, f.outDy, f.outAlpha, m_leavingColor };
        if (f.inAlpha > 0.0f) out[n++] = Piece{ m_shown, f.inDy, f.inAlpha, m_color };
        return n;
    }

private:
    char m_shown[MAX_TEXT] = "";
    char m_leaving[MAX_TEXT] = "";
    unsigned long m_color = 0;
    unsigned long m_leavingColor = 0;
    int m_dir = 1;
    long long m_startUs = -1;
};

// Whether Motion is on, so readouts roll (defined in hud_manager_motion.cpp).
bool enabled();
// Motion's clock, in microseconds (the one HudManager fades on; tests drive it
// with MXBMRP3_Test_SetMotionNowUs).
long long nowUs();

}  // namespace DigitRoll
