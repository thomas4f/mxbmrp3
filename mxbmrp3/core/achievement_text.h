// ============================================================================
// core/achievement_text.h
// HOW AN ACHIEVEMENT IS WRITTEN FOR A HUMAN: the unit formatter every number
// passes through, and the two sentence builders over it (a tier's description,
// and the "340 / 1,000 km" progress pair). Split out of achievements.h when the
// catalogue passed its budget, which is the split that header's own note asks
// for: the table and the tier arithmetic are one thing to read, the text is
// another, and only the settings tab and the toast need the text.
//
// Header-only and dependency-free beyond the catalogue itself, so
// test_achievements.cpp keeps compiling the real formatters - every row's
// sentence and its widest progress numbers are held under TAB_ROW_CHARS there.
//
// THE SPACING AND FIGURE RULES these implement are stated once, at Unit in
// achievements.h, because they bind the catalogue's sentences as tightly as
// they bind this code.
// ============================================================================
#pragma once

#include "achievements.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Achievements {

// Bounded append: `text` onto the C string already in `buffer`, never past
// bufferSize, always terminated. The formatters below are built from this
// rather than from snprintf("%s%s") chains, which the compiler cannot prove
// fit and which -Wformat-truncation therefore rejects under -Werror.
inline void appendText(char* buffer, size_t bufferSize, const char* text) {
    if (bufferSize == 0 || !text) return;
    size_t len = std::strlen(buffer);
    if (len >= bufferSize) len = bufferSize - 1;
    for (; *text && len + 1 < bufferSize; ++text) buffer[len++] = *text;
    buffer[len] = '\0';
}

// "1234567" -> "1,234,567". Integer part only; `value` is truncated. Small
// helper the unit formatter below builds on.
// TRUNCATES, never rounds: the tier is decided by `value >= threshold`, and a
// display that rounded 999.6 km up would read "1,000 / 1,000 km" beside a
// still-locked Bronze. The same rule holds for the tenths below.
inline void formatWithCommas(double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (value < 0.0) value = 0.0;
    char raw[32];
    snprintf(raw, sizeof(raw), "%.0f", std::floor(value));
    const int len = static_cast<int>(std::strlen(raw));
    size_t out = 0;
    for (int i = 0; i < len && out + 1 < bufferSize; ++i) {
        if (i > 0 && (len - i) % 3 == 0) {
            if (out + 2 >= bufferSize) break;
            buffer[out++] = ',';
        }
        buffer[out++] = raw[i];
    }
    buffer[out] = '\0';
}

// A metric value in its unit, for the description and the progress text.
// The epsilon absorbs the binary artifact of the x10 (2.3 * 10 is 22.999...),
// which would otherwise print a stored 2.3 as "2.2".
inline double truncTenths(double value) {
    return std::floor(value * 10.0 + 1e-6) / 10.0;
}

inline void formatValue(Unit unit, double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (value < 0.0) value = 0.0;
    switch (unit) {
        case Unit::Count:
            formatWithCommas(value, buffer, bufferSize);
            return;
        case Unit::Km:
            if (value < 10.0) {
                snprintf(buffer, bufferSize, "%.1f", truncTenths(value));
            } else {
                formatWithCommas(value, buffer, bufferSize);
            }
            appendText(buffer, bufferSize, "km");
            return;
        case Unit::Hours:
            if (value < 10.0 && truncTenths(value) != static_cast<double>(static_cast<int>(value))) {
                snprintf(buffer, bufferSize, "%.1f", truncTenths(value));
            } else {
                formatWithCommas(value, buffer, bufferSize);
            }
            appendText(buffer, bufferSize, "h");
            return;
        case Unit::SecondsTenths:
            snprintf(buffer, bufferSize, "%.1fs", truncTenths(value));
            return;
        case Unit::Metres:
            // Whole metres: the rows using it are a jump's height and length,
            // where a tenth is below what the number means.
            formatWithCommas(value, buffer, bufferSize);
            appendText(buffer, bufferSize, "m");
            return;
        case Unit::Litres:
            // Litres, whatever the Fuel widget is set to show. The catalogue
            // has always fixed its units (every distance row is km while the
            // Speed widget defaults to MPH) - one number is read by the
            // description, the progress text and the evaluation alike.
            formatWithCommas(value, buffer, bufferSize);
            appendText(buffer, bufferSize, "L");
            return;
        case Unit::Seconds: {
            // SHORT FORM: the smaller component only when it is not zero, and
            // no zero padding. "1h 30min", "1h", "5min 30s", "30min", "45s" -
            // never "1h 00min" or "30min 00s", which read as a stopwatch rather
            // than a goal and cost characters of a 47-character row for nothing.
            // That saving is what lets the tightest rows say what they mean:
            // Steady Hands sat at exactly 47 and could not name its session.
            //
            // MINUTES ARE "min", NOT "m", AND THAT IS NOT A STYLE CHOICE. The
            // Air group renders metres as "60 m", so a bare "30m" differs from
            // a distance by one space -- and "Ride 30m in another rider's
            // roost" read as a distance to every reviewer who saw it. Three
            // letters cost two per row; being read as the wrong quantity costs
            // the row. Hours and seconds are unambiguous and stay single.
            const long long s = static_cast<long long>(value);
            const long long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
            if (s >= 3600) {
                if (m > 0) snprintf(buffer, bufferSize, "%lldh %lldmin", h, m);
                else       snprintf(buffer, bufferSize, "%lldh", h);
            } else if (s >= 60) {
                if (sec > 0) snprintf(buffer, bufferSize, "%lldmin %llds", m, sec);
                else         snprintf(buffer, bufferSize, "%lldmin", m);
            } else {
                snprintf(buffer, bufferSize, "%llds", s);
            }
            return;
        }
    }
    buffer[0] = '\0';
}

// "Finish 10 races" -- what reaching `tier` (1..TIER_COUNT) means, in words.
inline void formatDescription(const Entry& e, int tier, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (tier < 1) tier = 1;
    if (tier > e.tierCount) tier = e.tierCount;
    const double threshold = e.thresholds[tier - 1];
    // The dedication rides at the end of the sentence, "(@Name)": one string,
    // on the tab's task row, the toast and the doc alike. The row budget
    // (TAB_ROW_CHARS) is what keeps the credited sentences short.
    auto credit = [&]() {
        if (!e.credit) return;
        appendText(buffer, bufferSize, " (");
        appendText(buffer, bufferSize, e.credit);
        appendText(buffer, bufferSize, ")");
    };
    if (e.descOne && (threshold == 1.0 || !e.descMany)) {
        snprintf(buffer, bufferSize, "%s", e.descOne);
        credit();
        return;
    }
    char value[32];
    formatValue(e.unit, threshold, value, sizeof(value));
    // The template's one %s, substituted by hand: a non-literal format string
    // is a warning on every compiler, and the bounded copy cannot overrun.
    const char* marker = std::strstr(e.descMany, "%s");
    buffer[0] = '\0';
    if (!marker) {
        appendText(buffer, bufferSize, e.descMany);
        credit();
        return;
    }
    const size_t prefixLen = static_cast<size_t>(marker - e.descMany);
    size_t out = 0;
    for (size_t i = 0; i < prefixLen && out + 1 < bufferSize; ++i) buffer[out++] = e.descMany[i];
    buffer[out] = '\0';
    appendText(buffer, bufferSize, value);
    appendText(buffer, bufferSize, marker + 2);
    credit();
}

// "340 / 1,000 km" -- the progress text beside the bar. Both halves share the
// unit, so it is written once on the right; a complete row shows the last
// threshold twice, which reads as "done" rather than as a bar with no goal.
inline void formatProgress(const Entry& e, double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    // The current value is NEVER clamped to the target: past Platinum the row
    // keeps counting ("13,212 / 10,000 km"), which is the point of a lifetime
    // number. Only the bar saturates (progressFraction).
    //
    // A ONE-SHOT COUNTS TOO, including one whose bar is a single event. It used
    // to print nothing there, on the grounds that "1 / 1" says no more than the
    // Earned tag - but that threw away the interesting half: the row asked for
    // one endo and you have landed a hundred, or for five seconds of hang time
    // and you have held thirteen. "100 / 1" and "13.0s / 5.0s" are the only
    // place that number is ever shown, and they cost the row nothing it was
    // using. A flag row simply reads "0 / 1" until it reads "1 / 1", which is
    // the same thing its tag says and no less true.
    const double target = targetThreshold(e, tierFor(e, value));
    char have[32];
    char want[32];
    formatValue(Unit::Count, value, have, sizeof(have));
    // Only the target carries the unit suffix; the current value borrows it --
    // at the unit's own resolution, so a row is seen to move: the first hour
    // in minutes ("24min / 10h" - "min", never "m", which is metres), the first
    // ten in tenths ("1.4 / 10h"), a kilometre in tenths ("0.4 / 1.0km"). Whole
    // hours used to print as a count: "0 / 1h" for fifty-nine minutes.
    switch (e.unit) {
        case Unit::Count:
            formatValue(Unit::Count, target, want, sizeof(want));
            break;
        case Unit::Km:
            if (value < 10.0) snprintf(have, sizeof(have), "%.1f", truncTenths(value));
            formatValue(e.unit, target, want, sizeof(want));
            break;
        case Unit::Hours:
            if (value < 1.0) {
                snprintf(have, sizeof(have), "%dmin", static_cast<int>(value * 60.0));
            } else if (value < 10.0) {
                snprintf(have, sizeof(have), "%.1f", truncTenths(value));
            }
            formatValue(e.unit, target, want, sizeof(want));
            break;
        case Unit::Metres:
        case Unit::Litres:
            // Litres and metres are whole numbers here (a tank is tens of them, the row is
            // thousands), so the current value borrows the target's suffix as a
            // plain count, exactly like Unit::Count above.
            formatValue(e.unit, target, want, sizeof(want));
            break;
        case Unit::Seconds:
        case Unit::SecondsTenths:
            formatValue(e.unit, value, have, sizeof(have));
            formatValue(e.unit, target, want, sizeof(want));
            break;
    }
    buffer[0] = '\0';
    appendText(buffer, bufferSize, have);
    appendText(buffer, bufferSize, " / ");
    appendText(buffer, bufferSize, want);
}

}  // namespace Achievements
