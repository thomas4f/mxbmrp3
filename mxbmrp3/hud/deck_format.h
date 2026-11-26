// ============================================================================
// hud/deck_format.h
// The decks' shared value formats (deck_hud_gather.cpp and
// deck_hud_more.cpp): hundredths, deltas, seconds, upper case. Called on a
// rebuild, never per frame; quantize, on every update, is a divide.
// ============================================================================
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/font_codepage.h"
#include "../core/motion.h"
#include "../core/plugin_utils.h"

namespace DeckFormat {
using Motion::clamp01;
// "1:48.23": a lap time in hundredths, the last digit cut as the game cuts
// its thousandths; under a minute without the "0:" when shortForm (the
// General tab's short format, as PluginUtils::formatLapTime). The Lap Log
// deck's columns: in thousandths a monospaced face overflows them.
inline void formatLapHundredths(char* buf, size_t n, int ms, bool shortForm) {
    if (n == 0) return;
    if (ms < 0) { buf[0] = '\0'; return; }
    const int m = ms / 60000, s = (ms % 60000) / 1000, h = (ms % 1000) / 10;
    if (shortForm && m == 0) snprintf(buf, n, "%d.%02d", s, h);
    else snprintf(buf, n, "%d:%02d.%02d", m, s, h);
}
// "15.37": a sector, in hundredths; from a minute on, as a lap time
// ("1:02.37"). Sectors are mostly under one, so the "0:" always goes.
inline void formatSector(char* buf, size_t n, int ms) { formatLapHundredths(buf, n, ms, /*shortForm=*/true); }
// A value at the step it is shown at, for a deck's picture key: a change
// below the step must not rebuild.
inline int quantize(float v, float step) { return std::isfinite(v) ? static_cast<int>(std::floor(v / step)) : 0; }

// "0.21", "1:02.4": a gap in hundredths, unsigned (tenths past a minute; the
// sign is the caller's: a rival's gap, a stamp's GAINED or LOST). (The shared
// formats show thousandths or tenths; the decks show a live gap in hundredths.)
inline void formatHundredths(char* buf, size_t n, int ms) {
    const int a = ms == INT_MIN ? 599990 : std::min(std::abs(ms), 599990);
    if (a >= 60000) snprintf(buf, n, "%d:%02d.%d", a / 60000, (a % 60000) / 1000, (a % 1000) / 100);
    else snprintf(buf, n, "%d.%02d", a / 1000, (a % 1000) / 10);
}

// "+0.21", "-0.05": a live delta, signed.
inline void formatDelta(char* buf, size_t n, int ms) {
    if (n < 2) return;
    buf[0] = ms < 0 ? '-' : '+';
    formatHundredths(buf + 1, n - 1, ms);
}

// Upper case in place: trick names, rider and track names and translated
// words come mixed case, a deck shouts. The text is the font code page's
// (CP1252 + the Turkish slots), so its accented letters shout too.
inline void upper(char* s) {
    for (; *s; ++s) *s = static_cast<char>(FontCodePage::upper(static_cast<unsigned char>(*s)));
}

inline void formatSeconds(char* buf, size_t n, float s) {
    snprintf(buf, n, "%.1f s", std::isfinite(s) ? std::clamp(s, 0.0f, 999.9f) : 0.0f);
}
}  // namespace DeckFormat
