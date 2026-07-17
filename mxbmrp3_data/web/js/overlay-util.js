// ============================================================================
// MXBMRP3 Web Overlay — Formatting, gap math, palette & font helpers
// Part 04/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

// --- Rendering helpers ---
function setText(el, text) {
    if (el.textContent !== text) el.textContent = text;
}

function setClass(el, cls) {
    if (el.className !== cls) el.className = cls;
}

// --- Timer interpolation ---
function formatMmSs(ms) {
    if (ms <= 0) return "00:00";
    var totalSec = Math.floor(ms / 1000);
    var m = Math.floor(totalSec / 60);
    var s = totalSec % 60;
    return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
}

// Format lap time from milliseconds
// Full:    "M:SS.mmm" (always shows minutes, 3 decimals)
// Compact: "SS.mmm" when < 1 min, "M:SS.mmm" otherwise
function formatLapTime(ms) {
    if (!ms || ms <= 0) return "";
    var minutes = Math.floor(ms / 60000);
    var seconds = Math.floor((ms % 60000) / 1000);
    var millis = ms % 1000;
    var mmm = (millis < 100 ? "0" : "") + (millis < 10 ? "0" : "") + millis;
    if (CONFIG.compactTimes && minutes === 0) {
        return seconds + "." + mmm;
    }
    return minutes + ":" + (seconds < 10 ? "0" : "") + seconds + "." + mmm;
}

// Format gap (time difference) from milliseconds
// Full:    "+M:SS.mmm" (3 decimals)
// Compact: "+SS.s" or "+M:SS.s" (1 decimal)
function formatGap(ms) {
    if (!ms || ms <= 0) return "";
    var sign = "+";
    var abs = Math.abs(ms);
    var minutes = Math.floor(abs / 60000);
    var seconds = Math.floor((abs % 60000) / 1000);
    if (CONFIG.compactTimes) {
        var tenths = Math.floor((abs % 1000) / 100);
        if (minutes > 0) {
            return sign + minutes + ":" + (seconds < 10 ? "0" : "") + seconds + "." + tenths;
        }
        return sign + seconds + "." + tenths;
    }
    var millis = abs % 1000;
    var mmm = (millis < 100 ? "0" : "") + (millis < 10 ? "0" : "") + millis;
    if (minutes > 0) {
        return sign + minutes + ":" + (seconds < 10 ? "0" : "") + seconds + "." + mmm;
    }
    return sign + seconds + "." + mmm;
}

// Resolve a rider's gap cell (text + CSS class) the same way for the
// standings tower and the tail carousel. Race: leader tag / lap gaps /
// relative time gaps; non-race: absolute best lap. DNS/RET/DSQ use the
// server-supplied label.
function computeGap(rider, session) {
    var isRace = session && session.isRace;
    var gap;
    if (rider.state === STATE_DNS || rider.state === STATE_RETIRED || rider.state === STATE_DSQ) {
        gap = rider.gap || "";
    } else if (isRace && rider.pos === 1) {
        gap = "Leader";
    } else if (isRace && rider.gapLaps > 0) {
        gap = "+" + rider.gapLaps + "L";
    } else if (isRace && rider.gapMs > 0) {
        gap = formatGap(rider.gapMs);
    } else if (!isRace && rider.bestLapMs > 0) {
        gap = formatLapTime(rider.bestLapMs);
    } else {
        gap = rider.gap || "";
    }
    var cls = "col-gap";
    if (gap === "Leader") cls += " gap-leader";
    else if (rider.gapLaps > 0) cls += " gap-laps";
    return { text: gap, cls: cls };
}

// Interval from the front of a battle to rider r, in ms. Prefer the real-time
// (live) gap when the overlay's Live Gaps toggle is on (CONFIG.battleLiveGaps)
// AND both the front rider and r report a valid live gap (both currently in the
// game's ~10-closest track-position batch, same lap, not lapped/finished);
// otherwise fall back to the official split. liveGapMs is leader-relative for
// everyone, so the difference is the rider-to-rider interval regardless of who
// leads the race. Returns { ms, live } so the card can mark a live value.
function battleInterval(front, r) {
    if (CONFIG.battleLiveGaps && front && front.liveGapValid && r.liveGapValid) {
        var liveMs = (r.liveGapMs || 0) - (front.liveGapMs || 0);
        // The live gap jitters frame-to-frame as the game's ~10-closest batch is
        // recomputed and can momentarily invert (<= 0), which formatGap() renders as
        // an empty string -> a visible flash. A rider in a battle is behind the front
        // by definition, so treat a non-positive live interval as noise and fall
        // through to the stable official split (matches the in-game behaviour, which
        // never blanks a battling rider's gap).
        if (liveMs > 0) return { ms: liveMs, live: true };
    }
    return { ms: (r.gapMs || 0) - (front.gapMs || 0), live: false };
}

// --- Palette sync ---
// Map server palette keys to CSS variable names
var paletteMap = {
    primary: "--gp-primary", secondary: "--gp-secondary",
    tertiary: "--gp-tertiary", muted: "--gp-muted",
    background: "--gp-background", positive: "--gp-positive",
    warning: "--gp-warning", neutral: "--gp-neutral",
    negative: "--gp-negative", accent: "--gp-accent"
};

// Parse "#rrggbb" to [r, g, b]
function hexToRgb(hex) {
    if (!hex || hex.length < 7) return null;
    var r = parseInt(hex.substring(1, 3), 16);
    var g = parseInt(hex.substring(3, 5), 16);
    var b = parseInt(hex.substring(5, 7), 16);
    return [r, g, b];
}

// True when "#rrggbb" is dark enough that white text reads better on it
// (BT.601 luma, standard YIQ midpoint threshold). Mirrors
// PluginUtils::isColorDark in the plugin so a plate flips the number to white
// the same way it does in-game (e.g. a red or blue tracked plate).
function isColorDark(hex) {
    var m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || "");
    if (!m) return false;
    var r = parseInt(m[1], 16), g = parseInt(m[2], 16), b = parseInt(m[3], 16);
    return (299 * r + 587 * g + 114 * b) / 1000 < 128;
}

// Apply a tracked rider's plate color to its number badge — background fill plus
// a readable number color (white on dark/saturated plates, themed default
// otherwise), matching the in-game tracked-rider plate. Inactive riders
// (DNS/RET/DSQ) keep the default greyed badge, as in-game; an empty/absent plate
// reverts to the themed default. Shared by every panel so tracked riders look
// identical in the tower, focus card, Down the Order, and battle cards. `badge`
// is whichever element carries the badge fill (.num-badge, or .focus-num in the
// focus card). The dataset guard skips redundant writes on persistent elements.
function applyPlateColor(badge, rider) {
    var inactive = rider.state === STATE_DNS || rider.state === STATE_RETIRED || rider.state === STATE_DSQ;
    var plateColor = (!inactive && rider.plateColor) ? rider.plateColor : "";
    if (badge.dataset.plate === plateColor) return;
    badge.dataset.plate = plateColor;
    badge.style.background = plateColor || "";
    badge.style.color = (plateColor && isColorDark(plateColor)) ? "var(--text)" : "";
}

function applyPalette(palette) {
    if (!palette) return;
    var root = document.documentElement.style;
    for (var key in paletteMap) {
        if (palette[key]) {
            root.setProperty(paletteMap[key], palette[key]);
        }
    }
    // Derive background colors from game background color.
    // Tower backgrounds are fully opaque (broadcaster hides in-game HUD behind it).
    // Focus card backgrounds are semi-transparent for compositing.
    if (palette.background) {
        var rgb = hexToRgb(palette.background);
        if (!rgb) return;
        // Lighten helper: blend toward white by factor (0.0 = original, 1.0 = white)
        var lighten = function(r, g, b, f) {
            return "rgb(" + Math.round(r + (255 - r) * f) + "," +
                   Math.round(g + (255 - g) * f) + "," +
                   Math.round(b + (255 - b) * f) + ")";
        };
        // Tower: fully opaque, subtle shade variation for visual rhythm
        root.setProperty("--bg", "rgb(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ")");
        root.setProperty("--bg-header", lighten(rgb[0], rgb[1], rgb[2], 0.04));
        root.setProperty("--bg-row-even", lighten(rgb[0], rgb[1], rgb[2], 0.08));
        root.setProperty("--bg-row-odd", lighten(rgb[0], rgb[1], rgb[2], 0.05));
        root.setProperty("--bg-highlight", lighten(rgb[0], rgb[1], rgb[2], 0.15));
        root.setProperty("--bg-inactive", lighten(rgb[0], rgb[1], rgb[2], 0.03));
        // Focus card: semi-transparent for compositing over gameplay
        var rgba = function(a) {
            return "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + "," + a + ")";
        };
        root.setProperty("--bg-focus", rgba(0.88));
    }
}

// --- Font sync ---
// CSS @font-face family names match the in-game .fnt filenames exactly,
// so we can use them directly as CSS font-family values.
// This set tracks which fonts have web versions bundled.
var availableFonts = {
    "Audiowide-Regular": true,
    "EnterSansman-Italic": true,
    "RobotoMono-Regular": true,
    "RobotoMono-Bold": true,
    "FuzzyBubbles-Regular": true,
    "Tiny5-Regular": true
};

function applyFonts(fonts) {
    if (!fonts) return;
    var root = document.documentElement.style;
    if (fonts.title && availableFonts[fonts.title]) {
        root.setProperty("--gf-title", "'" + fonts.title + "'");
    }
    if (fonts.normal && availableFonts[fonts.normal]) {
        root.setProperty("--gf-normal", "'" + fonts.normal + "'");
    }
    if (fonts.digits && availableFonts[fonts.digits]) {
        root.setProperty("--gf-digits", "'" + fonts.digits + "'");
    }
    if (fonts.small && availableFonts[fonts.small]) {
        root.setProperty("--gf-small", "'" + fonts.small + "'");
    }
    // No shipped rule consumes --gf-strong yet, but the plugin sends it and a
    // custom.css can use var(--font-strong) — keep the sync complete so the
    // in-game STRONG font choice follows through like the other categories.
    if (fonts.strong && availableFonts[fonts.strong]) {
        root.setProperty("--gf-strong", "'" + fonts.strong + "'");
    }
}

