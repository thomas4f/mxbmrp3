// ============================================================================
// MXBMRP3 Web Overlay — Configuration, settings persistence & constants
// Part 01/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";


// Register the service worker so the overlay shell is cached for offline
// use. This lets OBS load the UI even when the plugin HTTP server isn't
// running yet (e.g. after a PC restart, before MX Bikes has launched).
// Service workers require a secure context — http://localhost qualifies,
// but file:// does not, so we skip registration there.
if ("serviceWorker" in navigator && location.protocol !== "file:") {
    window.addEventListener("load", function () {
        navigator.serviceWorker.register("sw.js").then(function (reg) {
            dlog("service worker registered, scope:", reg.scope);
        }).catch(function (err) {
            // Overlay still works without offline caching.
            console.warn("[MXBMRP3] Service worker registration failed:", err);
        });
        // The SW posts this when a plugin version bump invalidates the cached overlay
        // shell (a new CACHE_NAME purges the old one). Surface it in the page console so
        // it's visible where the overlay is open; reload to pick up any UI changes.
        navigator.serviceWorker.addEventListener("message", function (e) {
            if (e.data && e.data.type === "mxbmrp3-cache-updated") {
                console.log("[MXBMRP3] New plugin version detected — cached overlay refreshed to " +
                    e.data.cache + " (cleared " + (e.data.cleared || []).join(", ") +
                    "). Reload the overlay / restart the OBS browser source to pick up UI changes.");
            }
        });
    });
} else if (location.protocol === "file:") {
    console.warn("[MXBMRP3] Offline caching disabled: service workers " +
                 "require http(s)://. Load via http://localhost:PORT instead.");
}

// =========================================================================
// OVERLAY SETTINGS — edit these to customize the web overlay.
// Colors and fonts sync automatically from in-game settings.
// For layout tweaks (width, spacing), edit style.css variables.
// =========================================================================

var CONFIG = {
    // --- Connection ---
    host: "localhost",       // Server host
    port: 8080,              // Server port

    // --- Font size ---
    // Root font size in pixels. Controls the scale of the entire overlay.
    // All rem-based sizes (text, spacing, layout) scale with this value.
    // Increase for higher-res streams, decrease for compact overlays.
    fontSize: 28,

    // --- Event log ---
    maxEvents: 3,            // Max visible event log entries
    timestampMode: "clock",  // "off", "session" (MM:SS), or "clock" (HH:MM)

    // --- Event filters ---
    // Set to false to hide specific event types from the log.
    events: {
        session: true,       // Session started, ended, state changes
        fastestLap: true,    // Fastest lap set
        penalty: false,      // Penalty received, cleared, changed
        riderOut: true,      // Retired, DNS, DSQ
        overtime: true,      // Time expired, overtime
        finalLap: true,      // Final lap
        finished: true,      // Rider finished
        leaderChange: true,  // Lead change
        pit: false,          // Pit entry/exit
        director: false      // Auto-director decisions — a monitoring aid, off by default
                             // (matches the in-game default); opt in to show them on stream.
    },

    // --- Chip filters ---
    // Set to false to hide specific chip icons on standings rows.
    // (The spectated rider is shown as a row highlight, not a chip — see the
    // ".camera-row" tag in renderStandings.)
    chips: {
        finished: true,      // Checkered flag
        pit: false,          // Pit indicator
        penalty: true,       // Penalty seconds
        fastest: true        // Fastest lap
    },

    // --- Times ---
    // INHERITED from the in-game HUD (short time format) — no settings-panel control.
    // Value here is only a fallback used before the first snapshot arrives.
    compactTimes: false,     // Compact format: drop leading 0:, tenths for gaps

    // --- Tower ---
    // The positions-gained (+/-) column is fully overlay-controlled: showPosDelta is
    // its on/off, and posDeltaRef picks what the delta is measured against. The plugin
    // sends a delta for every reference, so this works even when the in-game column is
    // off.  posDeltaRef picks the scope: "start" (whole race = label "Race"), "sf" (the
    // current lap = "Lap"), "split" (the current sector = "Sector"). Keys stay
    // "start"/"sf"/"split" to match the plugin's JSON.
    showPosDelta: true,      // Show positions gained/lost column (races only)
    posDeltaRef: "sf",       // +/- reference: "start" | "sf" | "split" (default: Lap)
    hideDns: false,          // Hide DNS riders from standings
    maxRiders: 20,           // Max visible standings rows (0 = show all)
    slotRows: 8,             // Shared row count for every bottom-slot panel that
                             // rises over the tower (fastest last laps, fastest
                             // laps, best sectors, down the order) (3-20)
    slotDuration: 15,        // Shared seconds a bottom-slot panel holds the slot.
                             // Single-view boards (fastest last/session laps) stay
                             // up this long; the carousels (best sectors, down the
                             // order) split it across their pages; the battle holds
                             // as long as the camera is on it (3-30)
    slotRest: 3,             // Global rest (s) after ANY bottom-slot panel ends its
                             // turn before the next may start - breathing room so
                             // panels don't run back-to-back. 0 = off. A manual
                             // force ignores it; a pre-empt / battle-to-battle hop
                             // doesn't arm it (not the end of a turn) (0-30)
    nameChars: 10,           // Characters shown in the name column (3-30).
                             // Drives the overall tower width.
    towerX: 0,               // Tower position X in pixels
    towerY: 0,               // Tower position Y in pixels

    // --- Visibility ---
    hideInMenus: false,      // Hide overlay when no session is active

    // --- Logo slideshow ---
    logoSlideshow: true,     // Show sponsor/logo banner above standings
    logoInterval: 30,        // Seconds between slides (5-120)
    logoEnabled: {},         // Per-file toggles: { "file.png": true/false }. Unknown = on.

    // --- Focus card ---
    focusCard: true,         // Show/hide the rider focus card (on by default;
                             // it's a broadcast element — auto-hidden on the
                             // mobile fill-width layout, toggle off in settings)
    focusDuration: 8000,     // Auto-hide delay in ms (0 = stay visible)
    // Detail rows (toggleable, like the battle card). Ideal off by default.
    focusBike: true,         // Show the rider's bike
    focusLastLap: true,      // Show the rider's last lap
    focusBestLap: true,      // Show the rider's fastest (session best) lap
    focusIdeal: false,       // Show the rider's ideal lap (sum of best sectors)

    // --- Fastest last laps ---
    // Race-only board over the bottom rows of the tower. Event-driven: slides in
    // when a rider posts a new fastest recent lap, holds for its duration, then
    // hides. No cadence timer - the caster can also force it via hotkey.
    fastLap: true,           // Show the fastest-last-lap panel (races only)

    // --- Fastest laps (session best) ---
    // Same bottom slot, ranked by each rider's best lap of the session (rank 1 = the
    // overall fastest lap). Races only. Event-driven: shows on a new session-best lap.
    bestLap: true,           // Show the session-best board

    // --- Best sectors ---
    // Same bottom slot. A carousel that pages one sector at a time (like Down the
    // Order), each page a ranked mini-board of the fastest riders in that sector.
    // Non-race only (matches the in-game fastest-sectors story). Event-driven:
    // shows on a new best sector; also on a hotkey.
    sectors: true,           // Show the best-sectors carousel

    // --- Down the order (tail) ---
    // The riders hidden below the Max Riders cutoff, as one vertical list that scrolls
    // through once (down then back up) and hides. Coverage, not a story, so it has no
    // auto-trigger - the caster brings it up via the Down-the-order hotkey.
    tail: true,              // Enable the down-the-order scroller (hotkey-driven)

    // --- Battles ---
    // Spotlight a cluster of riders running nose-to-tail (e.g. "Battle for
    // 4th") in the same bottom slot as the fastlap/tail panels (races only).
    // Unlike the timed boards, the battle has NO appearance cadence: it slides
    // in the moment a battle exists and the slot is free, and is pre-empted by
    // the boards when their turn comes (reclaiming the slot afterwards).
    // The panel is ALWAYS synced to the in-game director: it shows ONLY the
    // battle the director is currently framing (its subject's group), so the
    // overlay never surfaces a battle the camera isn't on, and hides when the
    // director leaves battles (or is off/paused).
    battle: true,            // Show the battle panel
    // NOTE: what *counts* as a battle (the gap threshold) and which battles to
    // ignore (max leader position) are set IN-GAME (Director settings) and shipped
    // in the snapshot, so the in-game director and this panel share one definition.
    // Per-rider detail sub-rows in the battle card (each adds a line).
    battleBike: true,        // Show the rider's bike
    battleLastLap: true,     // Show the rider's last lap
    battleFastLap: true,     // Show the rider's fastest (session best) lap
    battleIdeal: false,      // Show the rider's ideal lap (sum of best sectors)
    // Real-time interval to the front of the battle where available (both
    // riders in the game's ~10-closest track-position batch), else the
    // official split. Presentation-only — the plugin always sends the live-gap
    // data; this just picks which the card shows. Off by default (opt-in): the
    // official split is the stable, familiar value; live gaps are for casters who
    // want the real-time feel and accept the extra motion.
    battleLiveGaps: false,

    // --- Session charts ---
    // Same bottom slot as the other boards. A carousel that pages one race-
    // progression chart at a time (each an inline SVG line chart), like the best-
    // sectors carousel. Races only: the panel auto-shows once when the leader
    // finishes, and a caster can force it anytime via the Charts hotkey. Which
    // charts appear is chosen here — each enabled chart is one carousel page, in
    // the order lap -> trace -> gap -> pace. Race Trace needs a mass-start race and
    // is skipped in practice/qualifying even when enabled. Derived client-side
    // from the raw per-rider lap series the plugin sends (snapshot.laps), mirroring
    // the in-game Session Charts HUD.
    charts: true,            // Show the session-charts carousel (master on/off)
    chartLap: true,          // Page: track position per lap (the classic lap chart)
    chartTrace: false,       // Page: cumulative time vs a fixed reference pace (race only)
    chartGap: true,          // Page: gap behind the leader per lap
    chartPace: true          // Page: raw lap time per lap
    // Rider-line count reuses the shared Panel Rows (slotRows) knob — a taller panel
    // draws more lines — so there's no separate charts row-count setting (capped to
    // the 16-colour line palette).
};

// =========================================================================
// END OF SETTINGS — code below connects to the plugin and renders data.
// =========================================================================

// --- Settings persistence (localStorage) ---
var STORAGE_KEY = "mxbmrp3_settings";

// True while ?demo is replaying the synthetic race. Demo scales the panel
// cadence into CONFIG in memory, so persistence is suppressed — otherwise
// touching any setting in demo would write the scaled values to the user's
// real settings.
var demoActive = false;

function deepMerge(defaults, overrides) {
    var result = {};
    for (var key in defaults) {
        if (!defaults.hasOwnProperty(key)) continue;
        if (overrides && overrides.hasOwnProperty(key)) {
            if (typeof defaults[key] === "object" && defaults[key] !== null &&
                typeof overrides[key] === "object" && overrides[key] !== null) {
                result[key] = deepMerge(defaults[key], overrides[key]);
            } else if (typeof overrides[key] === typeof defaults[key]) {
                result[key] = overrides[key];
            } else {
                result[key] = defaults[key]; // type mismatch, use default
            }
        } else {
            result[key] = defaults[key];
        }
    }
    return result;
}

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// Read a CSS time custom property (e.g. --anim-slide: "1s" or "250ms") as
// milliseconds, so JS teardown timers track the themeable animation vars in
// style.css / custom.css instead of hard-coding a matching duration.
function cssTimeMs(name, fallbackMs) {
    var v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    var n = parseFloat(v);
    if (isNaN(n)) return fallbackMs;
    return /ms$/.test(v) ? n : n * 1000;
}

function loadSettings() {
    try {
        var stored = JSON.parse(localStorage.getItem(STORAGE_KEY));
        if (stored) {
            var merged = deepMerge(CONFIG, stored);
            for (var key in merged) {
                if (merged.hasOwnProperty(key)) CONFIG[key] = merged[key];
            }
            // logoEnabled has dynamic keys — deepMerge against {} would
            // discard them, so load directly from stored.
            if (stored.logoEnabled && typeof stored.logoEnabled === "object") {
                CONFIG.logoEnabled = stored.logoEnabled;
            }
        }
    } catch (e) { /* corrupted localStorage, use defaults */ }
    // Clamp numeric values to valid ranges
    CONFIG.fontSize = clamp(CONFIG.fontSize, 14, 56);
    CONFIG.maxRiders = clamp(CONFIG.maxRiders, 0, 40);
    CONFIG.slotRows = clamp(CONFIG.slotRows, 3, 20);
    CONFIG.nameChars = clamp(CONFIG.nameChars, 3, 30);
    if (["start", "sf", "split"].indexOf(CONFIG.posDeltaRef) < 0) {
        CONFIG.posDeltaRef = "sf";   // match the shipped default above
    }
    // Clamp the persisted tower position to the CURRENT viewport (minus a grab
    // margin): a position saved on a large display would otherwise load the tower
    // permanently off-screen on a smaller one — the drag handler only clamps
    // during a drag, which is unreachable if the tower is already off-screen.
    CONFIG.towerX = clamp(CONFIG.towerX, 0, Math.max(0, window.innerWidth - 40));
    CONFIG.towerY = clamp(CONFIG.towerY, 0, Math.max(0, window.innerHeight - 40));
    CONFIG.maxEvents = clamp(CONFIG.maxEvents, 0, 20);
    CONFIG.focusDuration = clamp(CONFIG.focusDuration, 0, 30000);
    CONFIG.slotDuration = clamp(CONFIG.slotDuration, 3, 30);
    CONFIG.slotRest = clamp(CONFIG.slotRest, 0, 30);
    CONFIG.logoInterval = clamp(CONFIG.logoInterval, 5, 120);
    if (["off", "session", "clock"].indexOf(CONFIG.timestampMode) < 0) {
        CONFIG.timestampMode = "clock";
    }
}

function saveSettings() {
    if (demoActive) return;   // don't persist demo's in-memory scaled timings
    var toSave = {
        fontSize: CONFIG.fontSize,
        compactTimes: CONFIG.compactTimes,
        showPosDelta: CONFIG.showPosDelta,
        posDeltaRef: CONFIG.posDeltaRef,
        hideDns: CONFIG.hideDns,
        maxRiders: CONFIG.maxRiders,
        slotRows: CONFIG.slotRows,
        nameChars: CONFIG.nameChars,
        towerX: CONFIG.towerX,
        towerY: CONFIG.towerY,
        maxEvents: CONFIG.maxEvents,
        timestampMode: CONFIG.timestampMode,
        events: {},
        chips: {},
        hideInMenus: CONFIG.hideInMenus,
        logoSlideshow: CONFIG.logoSlideshow,
        logoInterval: CONFIG.logoInterval,
        logoEnabled: CONFIG.logoEnabled,
        focusCard: CONFIG.focusCard,
        focusDuration: CONFIG.focusDuration,
        focusBike: CONFIG.focusBike,
        focusLastLap: CONFIG.focusLastLap,
        focusBestLap: CONFIG.focusBestLap,
        focusIdeal: CONFIG.focusIdeal,
        slotDuration: CONFIG.slotDuration,
        slotRest: CONFIG.slotRest,
        fastLap: CONFIG.fastLap,
        bestLap: CONFIG.bestLap,
        sectors: CONFIG.sectors,
        tail: CONFIG.tail,
        battle: CONFIG.battle,
        battleBike: CONFIG.battleBike,
        battleLastLap: CONFIG.battleLastLap,
        battleFastLap: CONFIG.battleFastLap,
        battleIdeal: CONFIG.battleIdeal,
        charts: CONFIG.charts,
        chartLap: CONFIG.chartLap,
        chartTrace: CONFIG.chartTrace,
        chartGap: CONFIG.chartGap,
        chartPace: CONFIG.chartPace
    };
    for (var k in CONFIG.events) toSave.events[k] = CONFIG.events[k];
    for (var k in CONFIG.chips) toSave.chips[k] = CONFIG.chips[k];
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(toSave));
    } catch (e) { /* quota exceeded */ }
}

loadSettings();

var BASE_URL = (location.protocol === "file:")
    ? "http://" + CONFIG.host + ":" + CONFIG.port
    : "";
var SSE_URL = BASE_URL + "/api/events";

var LAP_PLACEHOLDER = "-:--.---";

// Rider states (matches Unified::EntryState in plugin)
var STATE_DNS = 1;
var STATE_RETIRED = 3;
var STATE_DSQ = 4;

