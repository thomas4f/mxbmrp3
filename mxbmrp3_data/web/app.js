// ============================================================================
// MXBMRP3 Web Overlay - Client Application
// Connects to the plugin's HTTP server via Server-Sent Events (SSE).
// Customize freely - the plugin serves these files from disk.
// ============================================================================

(function () {
    "use strict";

    // Register the service worker so the overlay shell is cached for offline
    // use. This lets OBS load the UI even when the plugin HTTP server isn't
    // running yet (e.g. after a PC restart, before MX Bikes has launched).
    // Service workers require a secure context — http://localhost qualifies,
    // but file:// does not, so we skip registration there.
    if ("serviceWorker" in navigator && location.protocol !== "file:") {
        window.addEventListener("load", function () {
            navigator.serviceWorker.register("sw.js").then(function (reg) {
                console.log("[MXBMRP3] Service worker registered, scope:", reg.scope);
            }).catch(function (err) {
                // Overlay still works without offline caching.
                console.warn("[MXBMRP3] Service worker registration failed:", err);
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
            pit: false           // Pit entry/exit
        },

        // --- Chip filters ---
        // Set to false to hide specific chip icons on standings rows.
        chips: {
            finished: true,      // Checkered flag
            pit: false,          // Pit indicator
            penalty: true,       // Penalty seconds
            fastest: true,       // Fastest lap
            camera: true         // Spectated rider
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
        focusCard: false,        // Show/hide the rider focus card (off by default —
                                 // it's a broadcast element that doesn't fit the
                                 // mobile fill-width layout; enable in settings)
        focusDuration: 8000,     // Auto-hide delay in ms (0 = stay visible)

        // --- Fastest last laps ---
        // Race-only panel that slides up over the bottom rows of the tower on a
        // timer, listing riders by their most recent lap, then slides away.
        fastLap: true,           // Show the fastest-last-lap panel (races only)
        fastLapInterval: 145,    // Seconds between appearances (10-300). Duration and
                                 // interval are scaled together (here 2.5x the old
                                 // 6s/58s) so the panel shows for >=15s while keeping
                                 // the same on-screen vs at-rest ratio for the slot
        fastLapDuration: 15000,  // How long it stays visible in ms (1000-30000)
        fastLapCount: 8,         // Riders listed (3-20)

        // --- Fastest laps (session best) ---
        // Same bottom slot, but ranked by each rider's best lap of the session
        // (rank 1 = the overall fastest lap). Races only.
        bestLap: true,           // Part of the default rotation; appears less often
                                 // than the last-lap board (longer interval below)
        bestLapInterval: 283,    // Seconds between appearances (10-300). Long so it
                                 // de-phases from the other panels and rarely collides;
                                 // 2.5x the old 6s/113s to hold a >=15s display at the
                                 // same at-rest ratio
        bestLapDuration: 15000,  // How long it stays visible in ms (1000-30000)
        bestLapCount: 8,         // Riders listed (3-20)

        // --- Down the order (tail) ---
        // When Max Riders crops the tower, periodically page through the hidden
        // riders below the cutoff in the same bottom slot as the fastlap panel.
        tail: true,              // Show the down-the-order carousel
        tailInterval: 99,        // Seconds between cycles (10-300). Scaled with the page
                                 // hold below (1.25x the old 79s) to keep the at-rest ratio
        tailPageHold: 5,         // Seconds each page is shown (1-15)
        tailPageSize: 5,         // Riders per page (3-10)

        // --- Battles ---
        // Spotlight a cluster of riders running nose-to-tail (e.g. "Battle for
        // 4th") in the same bottom slot as the fastlap/tail panels (races only).
        // Unlike the timed boards, the battle has NO appearance cadence: it slides
        // in the moment a battle exists and the slot is free, and is pre-empted by
        // the boards when their turn comes (reclaiming the slot afterwards).
        battle: true,            // Show the battle panel
        battleInterval: 30,      // Rest (s) after the panel has cycled through every
                                 // active battle once, so a sustained set doesn't park
                                 // forever (within a pass they show back-to-back) (10-300)
        battleDuration: 15000,   // Max time a battle holds the slot in ms (1000-30000)
        battleGap: 2.5,          // Max interval (s) between two riders to count
                                 // as battling (0.5-5.0)
        // Per-rider detail sub-rows in the battle card (each adds a line).
        battleBike: true,        // Show the rider's bike
        battleLastLap: true,     // Show the rider's last lap
        battleFastLap: true      // Show the rider's fastest (session best) lap
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
        CONFIG.nameChars = clamp(CONFIG.nameChars, 3, 30);
        if (["start", "sf", "split"].indexOf(CONFIG.posDeltaRef) < 0) {
            CONFIG.posDeltaRef = "start";
        }
        CONFIG.towerX = Math.max(0, CONFIG.towerX);
        CONFIG.towerY = Math.max(0, CONFIG.towerY);
        CONFIG.maxEvents = clamp(CONFIG.maxEvents, 0, 20);
        CONFIG.focusDuration = clamp(CONFIG.focusDuration, 0, 30000);
        CONFIG.fastLapInterval = clamp(CONFIG.fastLapInterval, 10, 300);
        CONFIG.fastLapDuration = clamp(CONFIG.fastLapDuration, 1000, 30000);
        CONFIG.fastLapCount = clamp(CONFIG.fastLapCount, 3, 20);
        CONFIG.bestLapInterval = clamp(CONFIG.bestLapInterval, 10, 300);
        CONFIG.bestLapDuration = clamp(CONFIG.bestLapDuration, 1000, 30000);
        CONFIG.bestLapCount = clamp(CONFIG.bestLapCount, 3, 20);
        CONFIG.tailInterval = clamp(CONFIG.tailInterval, 10, 300);
        CONFIG.tailPageHold = clamp(CONFIG.tailPageHold, 1, 15);
        CONFIG.tailPageSize = clamp(CONFIG.tailPageSize, 3, 10);
        CONFIG.battleInterval = clamp(CONFIG.battleInterval, 10, 300);
        CONFIG.battleDuration = clamp(CONFIG.battleDuration, 1000, 30000);
        CONFIG.battleGap = clamp(CONFIG.battleGap, 0.5, 5.0);
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
            fastLap: CONFIG.fastLap,
            fastLapInterval: CONFIG.fastLapInterval,
            fastLapDuration: CONFIG.fastLapDuration,
            fastLapCount: CONFIG.fastLapCount,
            bestLap: CONFIG.bestLap,
            bestLapInterval: CONFIG.bestLapInterval,
            bestLapDuration: CONFIG.bestLapDuration,
            bestLapCount: CONFIG.bestLapCount,
            tail: CONFIG.tail,
            tailInterval: CONFIG.tailInterval,
            tailPageHold: CONFIG.tailPageHold,
            tailPageSize: CONFIG.tailPageSize,
            battle: CONFIG.battle,
            battleInterval: CONFIG.battleInterval,
            battleDuration: CONFIG.battleDuration,
            battleGap: CONFIG.battleGap,
            battleBike: CONFIG.battleBike,
            battleLastLap: CONFIG.battleLastLap,
            battleFastLap: CONFIG.battleFastLap
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

    // --- DOM References ---
    var overlay = document.getElementById("overlay");
    var sessionTime = document.getElementById("session-time");
    var sessionType = document.getElementById("session-type");
    var sessionInfo = document.getElementById("session-info");
    var standingsBody = document.getElementById("standings-body");
    var headerTitle = document.getElementById("header-title");
    var headerGap = document.getElementById("header-gap");
    var eventLog = document.getElementById("event-log");
    var fastLapPanel = document.getElementById("fastlap-panel");
    var fastLapList = document.getElementById("fastlap-list");
    var fastLapTitle = fastLapPanel.querySelector(".fastlap-title");
    var bestLapPanel = document.getElementById("bestlap-panel");
    var bestLapList = document.getElementById("bestlap-list");
    var bestLapTitle = bestLapPanel.querySelector(".bestlap-title");
    var tailPanel = document.getElementById("tail-panel");
    var tailTitle = tailPanel.querySelector(".tail-title");
    var tailViewport = tailPanel.querySelector(".tail-viewport");
    var tailTrack = tailPanel.querySelector(".tail-track");
    var battlePanel = document.getElementById("battle-panel");
    var battleTitle = battlePanel.querySelector(".battle-title");
    var battleList = document.getElementById("battle-list");
    var focusCard = document.getElementById("focus-card");
    var focusAhead = document.getElementById("focus-ahead");
    var focusMain = document.getElementById("focus-main");
    var focusBehind = document.getElementById("focus-behind");

    // --- Root sizing (font size + name-column width) ---
    // On phone-sized touch screens the overlay drops its fixed corner-widget size
    // and fills the viewport width instead (see the matching @media block in
    // style.css). Everything in the tower is proportional to the root font-size,
    // so a single scalar fills the width: pick font-size so the tower's computed
    // width equals window.innerWidth. offsetWidth is linear in font-size, which
    // makes this a stable fixed point (re-running it leaves the value unchanged).
    // OBS browser sources report pointer:fine, so this never triggers there.
    var mobileFitMQ = window.matchMedia("(pointer: coarse) and (max-width: 820px)");
    function applyRootSizing() {
        var root = document.documentElement.style;
        root.setProperty("--name-chars", CONFIG.nameChars);
        root.fontSize = CONFIG.fontSize + "px";
        if (mobileFitMQ.matches && overlay.offsetWidth > 0) {
            root.fontSize = (CONFIG.fontSize * window.innerWidth / overlay.offsetWidth) + "px";
        }
    }
    applyRootSizing();
    window.addEventListener("resize", applyRootSizing);
    if (mobileFitMQ.addEventListener) {
        mobileFitMQ.addEventListener("change", applyRootSizing);
    } else if (mobileFitMQ.addListener) {
        mobileFitMQ.addListener(applyRootSizing); // older WebKit
    }

    // --- Logo slideshow ---
    var logoBanner = document.getElementById("logo-banner");
    var logoTrack = document.getElementById("logo-track");
    var logoFiles = [];
    var logoIndex = 0;
    var logoTimer = null;
    var logoSnapTimer = null;

    function fetchLogos() {
        fetch(BASE_URL + "/api/logos", { mode: "cors" }).then(function (r) {
            return r.json();
        }).then(function (files) {
            logoFiles = files || [];
            buildLogoTrack();
            buildLogoSettings();
        }).catch(function () {
            logoFiles = [];
            buildLogoTrack();
            buildLogoSettings();
        });
    }

    function getEnabledLogos() {
        var result = [];
        for (var i = 0; i < logoFiles.length; i++) {
            if (CONFIG.logoEnabled[logoFiles[i]] !== false) result.push(logoFiles[i]);
        }
        return result;
    }

    function buildLogoTrack() {
        logoTrack.style.transition = "none";
        logoTrack.textContent = "";
        logoIndex = 0;
        if (logoTimer) { clearInterval(logoTimer); logoTimer = null; }
        if (logoSnapTimer) { clearTimeout(logoSnapTimer); logoSnapTimer = null; }

        var enabled = getEnabledLogos();
        if (!CONFIG.logoSlideshow || enabled.length === 0) {
            logoBanner.classList.add("hidden");
            return;
        }

        logoBanner.classList.remove("hidden");
        for (var i = 0; i < enabled.length; i++) {
            var img = document.createElement("img");
            img.src = "logos/" + enabled[i];
            img.alt = enabled[i];
            img.draggable = false;
            logoTrack.appendChild(img);
        }
        if (enabled.length > 1) {
            var clone = document.createElement("img");
            clone.src = "logos/" + enabled[0];
            clone.alt = enabled[0];
            clone.draggable = false;
            logoTrack.appendChild(clone);
        }
        logoTrack.style.transform = "translateX(0)";
        void logoTrack.offsetWidth;
        logoTrack.style.transition = "";

        if (enabled.length > 1) {
            logoTimer = setInterval(advanceLogo, CONFIG.logoInterval * 1000);
        }
    }

    function advanceLogo() {
        var count = getEnabledLogos().length;
        if (count <= 1) return;
        logoIndex++;
        logoTrack.style.transform = "translateX(-" + (logoIndex * 100) + "%)";

        // After sliding to the clone of the first logo, wait for the
        // transition to finish then instantly snap back to the real first.
        if (logoIndex >= count) {
            logoSnapTimer = setTimeout(function () {
                logoSnapTimer = null;
                logoTrack.style.transition = "none";
                logoIndex = 0;
                logoTrack.style.transform = "translateX(0)";
                // Force reflow so the jump is applied before re-enabling transition
                void logoTrack.offsetWidth;
                logoTrack.style.transition = "";
            }, cssTimeMs("--anim-logo", 800) + 50); // just past the slide transition
        }
    }

    function buildLogoSettings() {
        var container = document.getElementById("logo-settings");
        if (!container) return;
        container.textContent = "";
        for (var i = 0; i < logoFiles.length; i++) {
            (function (file) {
                var label = file.replace(/\.png$/i, "");
                var enabled = CONFIG.logoEnabled[file] !== false;
                addRow(container, label, createToggle(enabled, function (v) {
                    CONFIG.logoEnabled[file] = v;
                    buildLogoTrack();
                    saveSettings();
                }));
            })(logoFiles[i]);
        }
    }

    fetchLogos();

    // --- Tower position & drag ---
    var header = document.getElementById("header");

    function applyTowerPosition() {
        overlay.style.left = CONFIG.towerX + "px";
        overlay.style.top = CONFIG.towerY + "px";
    }
    applyTowerPosition();

    (function () {
        var dragging = false;
        var startMouseX, startMouseY, startElX, startElY;

        header.addEventListener("mousedown", function (e) {
            if (e.button !== 0) return;
            dragging = true;
            startMouseX = e.clientX;
            startMouseY = e.clientY;
            startElX = CONFIG.towerX;
            startElY = CONFIG.towerY;
            e.preventDefault();
        });

        document.addEventListener("mousemove", function (e) {
            if (!dragging) return;
            var maxX = Math.max(0, window.innerWidth - overlay.offsetWidth);
            var maxY = Math.max(0, window.innerHeight - overlay.offsetHeight);
            CONFIG.towerX = clamp(startElX + e.clientX - startMouseX, 0, maxX);
            CONFIG.towerY = clamp(startElY + e.clientY - startMouseY, 0, maxY);
            applyTowerPosition();
        });

        document.addEventListener("mouseup", function () {
            if (!dragging) return;
            dragging = false;
            saveSettings();
        });
    })();

    // --- State ---
    var eventSource = null;
    var isConnected = false;   // currently connected
    var lastError = "";        // last error message (suppress duplicates)
    var serverReachable = false; // fetch probe succeeded

    // Focus card state
    var focusLastSeenNum = -1;    // Last camera rider we saw (persists after hide)
    var focusVisible = false;     // Whether card is currently shown
    var focusHideTimer = null;    // Auto-hide timeout

    var versionAnnounced = false;  // banner shown once on first connect

    // --- Status messages via event log ---
    function clockNow() {
        var d = new Date();
        var h = d.getHours(), m = d.getMinutes();
        return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m;
    }
    function clockNowFull() {
        var d = new Date();
        var h = d.getHours(), m = d.getMinutes(), s = d.getSeconds();
        return (h < 10 ? "0" : "") + h + ":" +
               (m < 10 ? "0" : "") + m + ":" +
               (s < 10 ? "0" : "") + s;
    }

    // Cap status lines so a flaky connection can't accumulate enough of them
    // to starve real events out of the maxEvents budget.
    var MAX_STATUS_LINES = 3;

    // Status lines (connection history) are kept in memory so they survive
    // re-renders and toggling maxEvents to 0 and back. They are rendered
    // through renderEventLog() like regular events.
    var statusLines = [];
    var lastEvents = [];
    var lastIdle = false;
    var lastData = null;

    // status: "info" (default), "ok", "error"
    function appendStatusLine(message, status) {
        statusLines.push({
            time: clockNow(),
            sortKey: clockNowFull(),
            message: message,
            status: status || "info"
        });
        while (statusLines.length > MAX_STATUS_LINES) {
            statusLines.shift();
        }
        renderEventLog(lastEvents);
    }

    var RETRY_INTERVAL = 3000; // ms between manual reconnection attempts
    var retryTimer = null;
    var STATE_URL = BASE_URL + "/api/state";

    // --- SSE Connection ---
    // We probe with fetch first to avoid noisy browser console errors
    // from EventSource connecting to an unreachable server.
    function logError(msg) {
        if (msg === lastError) return; // suppress duplicate
        lastError = msg;
        appendStatusLine(msg, "error");
    }

    function connect() {
        if (retryTimer) {
            clearTimeout(retryTimer);
            retryTimer = null;
        }

        if (!lastError) {
            appendStatusLine("Trying " + CONFIG.host + ":" + CONFIG.port);
        }

        // Probe server availability before opening SSE
        fetch(STATE_URL, { mode: "cors" }).then(function () {
            serverReachable = true;
            openEventSource();
        }).catch(function () {
            serverReachable = false;
            overlay.classList.add("disconnected");
            logError("Failed. Server enabled?");
            retryTimer = setTimeout(connect, RETRY_INTERVAL);
        });
    }

    function openEventSource() {
        if (eventSource) {
            eventSource.close();
        }

        eventSource = new EventSource(SSE_URL);

        eventSource.onopen = function () {
            isConnected = true;
            lastError = "";
            overlay.classList.remove("disconnected");
            // "Connected" line is appended from render() once we know the
            // plugin version, so it can be shown in a single combined message.

            // Refresh logo list (may have failed before server was up)
            if (logoFiles.length === 0) fetchLogos();
        };

        eventSource.onmessage = function (event) {
            try {
                var data = JSON.parse(event.data);
                render(data);
            } catch (e) {
                console.error("Failed to parse SSE data:", e);
            }
        };

        eventSource.onerror = function () {
            overlay.classList.add("disconnected");

            if (isConnected) {
                isConnected = false;
                serverReachable = false;
                versionAnnounced = false;  // re-announce on next reconnect
                logError("Connection lost");
            } else if (serverReachable) {
                // Fetch probe succeeded but SSE was rejected (503)
                logError("Too many connections");
            }

            // EventSource gave up (CLOSED) — schedule manual retry
            if (eventSource.readyState === 2) {
                retryTimer = setTimeout(connect, RETRY_INTERVAL);
            }
        };
    }

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
            root.setProperty("--bg-focus-highlight", rgba(0.95));
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
    }

    // --- Rendering ---
    // Call with a fresh snapshot from SSE, or with no args to re-render
    // the cached snapshot (used by applySettings()).
    function render(data) {
        if (data) lastData = data;
        else data = lastData;
        if (!data) return;
        if (data.session) {
            applyPalette(data.session.palette);
            applyFonts(data.session.fonts);

            // Compact time format is mirrored from the in-game HUD ("configure once
            // in-game"); the plugin sends it in the session block and the overlay has no
            // local control for it. Older plugins omit the field (typeof guard).
            // (The +/- column is fully overlay-controlled — both its on/off and its
            // reference are local settings — so nothing about it is overridden here.)
            if (typeof data.session.compactTimes === "boolean") {
                CONFIG.compactTimes = data.session.compactTimes;
            }

            // One-time "Connected" banner, with plugin name + version if available.
            if (!versionAnnounced) {
                versionAnnounced = true;
                var label = data.session.pluginVersion
                    ? "Connected (MXBMRP3 v" + data.session.pluginVersion + ")"
                    : "Connected";
                appendStatusLine(label, "ok");
            }
        }
        // Hide overlay when idle (no active session) if configured
        var idle = !data.session || !data.session.type;
        lastIdle = idle;
        overlay.classList.toggle("idle", idle && CONFIG.hideInMenus);

        renderHeader(data.session);
        renderStandings(data.standings, data.session);
        renderEventLog(data.events);
        renderFocusCard(data.standings, data.session);
        handleOverlayCommand(data);   // broadcaster force (before scheduled rotation)
        manageSlots(data.session);

        // Refit the mobile fill-width scale: the tower width can change between
        // renders (idle->visible, the ± column showing/hiding). No-op on desktop.
        if (mobileFitMQ.matches) applyRootSizing();
    }

    function renderHeader(session) {
        if (!session) return;

        setText(sessionTime, session.time || "--:--");
        // Empty type = in menus (plugin's idle snapshot); label it client-side.
        var inMenus = !session.type;
        // "Session (Format)" - shares the format syntax with in-game/Discord/Steam.
        // State stays on the info line below; the full "..., In Progress" string is
        // too long for this header slot.
        var typeText = "In Menus";
        if (!inMenus) {
            typeText = session.type;
            if (session.format) typeText += " (" + session.format + ")";
        }
        setText(sessionType, typeText);

        // Info line (below session type, right-aligned):
        //   Menus:                    "Waiting"
        //   Race (time+lap or timed): "Lap X"
        //   Race (lap-only):          "Lap X/Y"
        //   Non-race:                 state (e.g. "In Progress")
        var info = "";
        if (inMenus) {
            info = "Waiting";
        } else if (session.isRace) {
            if (session.numLaps > 0 && session.sessionLength <= 0 && session.leaderLap >= 0) {
                // Pure lap race: show Lap X/Y, or "CHECKERED" once the leader
                // completes the final lap (leaderLap = completed laps). Mirrors the
                // in-game StandingsHud session-info row so both read identically.
                info = (session.leaderLap >= session.numLaps)
                    ? "CHECKERED"
                    : "Lap " + Math.min(session.leaderLap + 1, session.numLaps) + "/" + session.numLaps;
            } else if (session.leaderLap >= 0) {
                // Timed or time+lap race: show Lap X only
                info = "Lap " + (session.leaderLap + 1);
            }
        } else {
            info = session.state || "";
        }
        setText(sessionInfo, info);
        // Simplified header strap: race shows "Behind Leader"/"Gap", non-race
        // shows "Lap Times" with the right slot cleared. (CSS uppercases.)
        setText(headerTitle, session.isRace ? "Behind Leader" : "Lap Times");
        setText(headerGap, session.isRace ? "Gap" : "");
    }

    // Row structure:
    //   div.standings-row
    //     div.row-main            [0] - has background
    //       span.col-pos            [0][0]
    //       span.col-posdelta       [0][1]
    //       span.col-num            [0][2]
    //         span.num-badge
    //         span.brand-strip
    //       span.col-name           [0][3]
    //       span.col-gap            [0][4]
    //     div.row-chips            [1] - no background, hangs outside
    function createStandingsRow() {
        var row = document.createElement("div");
        row.className = "standings-row";

        var main = document.createElement("div");
        main.className = "row-main";

        var pos = document.createElement("span");
        pos.className = "col-pos";
        main.appendChild(pos);

        var posdelta = document.createElement("span");
        posdelta.className = "col-posdelta";
        main.appendChild(posdelta);

        var num = document.createElement("span");
        num.className = "col-num";
        var numBadge = document.createElement("span");
        numBadge.className = "num-badge";
        num.appendChild(numBadge);
        var brandStrip = document.createElement("span");
        brandStrip.className = "brand-strip";
        num.appendChild(brandStrip);
        main.appendChild(num);

        var name = document.createElement("span");
        name.className = "col-name";
        main.appendChild(name);

        var gap = document.createElement("span");
        gap.className = "col-gap";
        main.appendChild(gap);

        row.appendChild(main);

        var chips = document.createElement("div");
        chips.className = "row-chips";
        row.appendChild(chips);

        return row;
    }

    // Rows keyed by rider number for stable identity across position changes
    var riderRows = {};    // raceNum -> DOM element
    var prevPositions = {}; // raceNum -> index (for detecting changes)
    // Resolve --row-height to pixels (handles both rem and px values). Memoized on
    // the only inputs that change its value — the root font-size and --row-height
    // itself — so the (layout-forcing) probe runs once per change instead of on
    // every measureRowHeight() call (several per render, across panels). The probe
    // is a fixed-height div, so it's font-metric independent.
    var rowHeightProbe = document.createElement("div");
    rowHeightProbe.style.height = "var(--row-height)";
    rowHeightProbe.style.position = "absolute";
    rowHeightProbe.style.visibility = "hidden";
    var cachedRowHeight = 0, cachedRowHeightSig = "";

    function measureRowHeight() {
        var cs = getComputedStyle(document.documentElement);
        var sig = cs.fontSize + "|" + cs.getPropertyValue("--row-height").trim();
        if (cachedRowHeight && sig === cachedRowHeightSig) return cachedRowHeight;
        document.body.appendChild(rowHeightProbe);
        var h = rowHeightProbe.offsetHeight;
        document.body.removeChild(rowHeightProbe);
        cachedRowHeightSig = sig;
        cachedRowHeight = h || 28;
        return cachedRowHeight;
    }

    // Measure the gap column to the *rendered* width of the worst-case string in
    // the actual synced font. A fixed ch/rem value can't be tight here: ch sizes
    // to the width of "0", but time strings are full of narrow glyphs (':' '.'
    // '+'), so any value wide enough to clear a monospace font leaves big dead
    // space in a wide proportional face (e.g. Audiowide). Measuring the real
    // glyphs removes that slack. Mirrors measureRowHeight(): a hidden probe,
    // re-measured each render since the font/size sync from the palette.
    // Always sizes to the GLOBAL worst case (full-precision gap + "Leader"), not
    // the per-session worst case, so the tower width depends only on the font and
    // never reflows between sessions / compact toggles.
    var gapProbe = document.createElement("span");
    gapProbe.style.position = "absolute";
    gapProbe.style.visibility = "hidden";
    gapProbe.style.whiteSpace = "nowrap";
    gapProbe.style.paddingLeft = "var(--sp-md)";  // matches .col-gap (border-box)
    gapProbe.style.fontSize = "var(--fs)";

    // Returns the worst-case gap width in px (sub-pixel float via
    // getBoundingClientRect, so the rem ratio derived from it is exactly linear in
    // font-size — see the render call). The caller adds a small rem margin.
    function measureGapWidth() {
        document.body.appendChild(gapProbe);
        // Widest time string the column can ever show: "+M:SS.mmm" ("8" stands in
        // for every digit; punctuation is measured as rendered). Uses --font-digits.
        gapProbe.style.fontFamily = "var(--font-digits)";
        gapProbe.textContent = "+8:88.888";
        var w = gapProbe.getBoundingClientRect().width;
        // P1's "Leader" label uses --font (gap-leader) and may be wider.
        gapProbe.style.fontFamily = "var(--font)";
        gapProbe.textContent = "Leader";
        var lw = gapProbe.getBoundingClientRect().width;
        if (lw > w) w = lw;
        document.body.removeChild(gapProbe);
        return w;
    }

    // Measure the number-plate badge to its widest (3-digit) content in the
    // badge's own font/size. Same rationale as the gap: --badge-w is in ch, but
    // the #overlay width formula resolves it at the overlay's 1rem font while the
    // badge renders at fs-sm (0.75rem), so the formula over-reserves the tower
    // and the surplus pools after the last column — leaving the gap values short
    // of the right edge. A measured px value is identical in both places, so the
    // tower width matches the columns exactly and gaps line up with the labels.
    var badgeProbe = document.createElement("span");
    badgeProbe.className = "num-badge";          // inherit badge font/size/padding
    badgeProbe.style.position = "absolute";
    badgeProbe.style.visibility = "hidden";
    badgeProbe.style.width = "auto";             // size to content, not --badge-w

    function measureBadgeWidth() {
        badgeProbe.textContent = "888";          // widest 3-digit plate
        document.body.appendChild(badgeProbe);
        var w = badgeProbe.getBoundingClientRect().width;  // sub-pixel; border-box incl. padding
        document.body.removeChild(badgeProbe);
        return w;
    }

    // Last measured column widths (rem strings), so we only rewrite the vars and
    // re-fit the mobile layout when they actually change (i.e. on a font change),
    // not on every standings snapshot.
    var lastMeasuredBadgeW = "", lastMeasuredGapW = "";

    // Signature of everything that affects the badge/gap probe widths: the root
    // font-size and the synced fonts. The probes each force a synchronous layout
    // and renderStandings runs on every snapshot, so we re-measure only when this
    // signature changes instead of every frame. Any cause of a font change — the
    // plugin sync (--gf-*), a custom.css override (resolved --font*), or a manual
    // var override — moves the signature, so the columns still auto-fit.
    var lastMeasureSig = "";

    // Web fonts load async (font-display: swap), so the first measure for a face
    // may use fallback metrics. When a face finishes loading, drop the cached
    // signature so the next render re-measures with the real glyphs.
    if (document.fonts && document.fonts.addEventListener) {
        document.fonts.addEventListener("loadingdone", function () {
            lastMeasureSig = "";
            if (lastData) render();
        });
    }

    function renderStandings(standings, session) {
        if (!standings) return;

        // Positions gained/lost column only makes sense during a race, and only when
        // enabled. Toggling a single class on the overlay hides both header and cells,
        // keeping the column out of the layout entirely otherwise.
        var showPosDelta = CONFIG.showPosDelta && session && session.isRace;
        overlay.classList.toggle("hide-posdelta", !showPosDelta);

        // Auto-fit the badge and gap columns: write the exact measured widths to
        // the *-measured vars that --badge-w / --col-gap-w fall back to (see
        // style.css). These MUST be set on :root (document.documentElement), not
        // #overlay: --col-gap-w / --badge-w are declared on :root, and var()
        // substitution resolves at the declaring element, so a measured value set
        // lower down (#overlay) would never be seen and the column would stay at
        // the fallback. Setting them on :root makes the formula match the columns
        // exactly (gaps reach the right edge, no dead space), stays constant
        // across sessions, and still lets a custom.css :root override of
        // --badge-w / --col-gap-w win (it replaces the whole declaration).
        //
        // Stored in REM (measured px / root font-size), NOT px: the mobile
        // fill-width fit (applyRootSizing) scales the root font-size to make the
        // tower span the viewport, which only works if every column is
        // proportional to font-size. A fixed-px column adds a constant term that
        // breaks that linearity and overflows. rem keeps it proportional, so the
        // measured value is identical on desktop yet still scales on mobile.
        // px -> rem ratio (font-size independent) + a small rem margin in place of
        // the old +1px guard, rounded so float noise doesn't churn the cache.
        var cs = getComputedStyle(document.documentElement);
        var rootPx = parseFloat(cs.fontSize) || 16;
        // Skip the (layout-forcing) probe measurements unless the font-size or a
        // synced font actually changed since last time. The signature includes
        // BOTH the --font/--font-digits properties AND the raw --gf-* values they
        // resolve through, on purpose: getComputedStyle may report a custom
        // property either substituted or as its declaration text depending on the
        // engine, so covering both guarantees the signature moves whether the font
        // change came from the plugin sync (--gf-*) or a custom.css override of
        // --font* directly. Don't drop --gf-* — that can miss the plugin font sync
        // on engines that return declaration text.
        var measureSig = rootPx + "|" +
            cs.getPropertyValue("--font").trim() + "|" +
            cs.getPropertyValue("--font-digits").trim() + "|" +
            cs.getPropertyValue("--gf-normal").trim() + "|" +
            cs.getPropertyValue("--gf-digits").trim();
        if (measureSig !== lastMeasureSig) {
            lastMeasureSig = measureSig;
            var MARGIN_REM = 0.08;
            var badgeW = (measureBadgeWidth() / rootPx + MARGIN_REM).toFixed(4) + "rem";
            var gapW = (measureGapWidth() / rootPx + MARGIN_REM).toFixed(4) + "rem";
            if (badgeW !== lastMeasuredBadgeW || gapW !== lastMeasuredGapW) {
                lastMeasuredBadgeW = badgeW;
                lastMeasuredGapW = gapW;
                var rootStyle = document.documentElement.style;
                rootStyle.setProperty("--badge-w-measured", badgeW);
                rootStyle.setProperty("--col-gap-w-measured", gapW);
                // Tower width changed — re-fit the mobile fill-width scale so it
                // still spans the viewport (no-op on desktop/OBS).
                if (mobileFitMQ.matches) applyRootSizing();
            }
        }

        // Filter DNS riders client-side if enabled (state 1 = DNS)
        if (CONFIG.hideDns) {
            var filtered = [];
            for (var f = 0; f < standings.length; f++) {
                if (standings[f].state !== STATE_DNS) filtered.push(standings[f]);
            }
            standings = filtered;
        }

        // Limit visible rows if configured (0 = show all)
        var visibleCount = standings.length;
        if (CONFIG.maxRiders > 0 && CONFIG.maxRiders < visibleCount) {
            visibleCount = CONFIG.maxRiders;
        }

        // Measure row height (may change if root font-size was updated)
        var rowHeight = measureRowHeight();

        // Set container height to fit visible rows
        standingsBody.style.height = (visibleCount * rowHeight) + "px";

        // Track which riders are in this update
        var activeNums = {};

        for (var i = 0; i < standings.length; i++) {
            var rider = standings[i];
            var num = String(rider.num);
            activeNums[num] = true;

            // Get or create row for this rider
            var row = riderRows[num];
            if (!row) {
                row = createStandingsRow();
                standingsBody.appendChild(row);
                riderRows[num] = row;
            }

            var main = row.children[0];
            var chipsEl = row.children[1];
            var cols = main.children;

            // Hide rows beyond the visible limit
            row.style.display = (i < visibleCount) ? "" : "none";

            // Hide chips on rows the active bottom-slot panel covers. Chips hang
            // outside the tower to the right, so the tower-width panel doesn't
            // cover them — they'd otherwise poke out beside it. Clamp so a panel
            // never hides the top (leader) row's chips: a bottom-slot panel
            // slides up from the bottom and can't reach the leader, and an
            // over-tall cover (e.g. a forced "No data" board when alone, covered
            // > visibleCount) would otherwise drive the threshold negative and
            // hide every row's chips.
            var hiddenChipCount = Math.min(slotCoveredRows(), Math.max(0, visibleCount - 1));
            // Fade chips out/in (opacity transition in CSS) rather than popping
            // them with display, so a panel covering/uncovering a row reads
            // smoothly. Chips are absolutely positioned, so opacity is layout-safe.
            chipsEl.classList.toggle("chips-covered",
                hiddenChipCount > 0 && i >= visibleCount - hiddenChipCount);

            // Slide to position
            row.style.transform = "translateY(" + (i * rowHeight) + "px)";

            // Detect position changes (skip first render)
            if (prevPositions.hasOwnProperty(num) && prevPositions[num] !== i) {
                var tintClass = (i < prevPositions[num]) ? "promoted" : "demoted";
                row.classList.remove("promoted", "demoted");
                // Force reflow to restart animation
                void row.offsetWidth;
                row.classList.add(tintClass);
            }

            // Row classes
            var rowCls = "standings-row";
            if (rider.chips && rider.chips.indexOf("camera") !== -1) rowCls += " camera-row";
            if (rider.state === STATE_DNS || rider.state === STATE_RETIRED || rider.state === STATE_DSQ) rowCls += " state-inactive";
            // Preserve tint class if active
            if (row.classList.contains("promoted")) rowCls += " promoted";
            if (row.classList.contains("demoted")) rowCls += " demoted";
            setClass(row, rowCls);

            // Alternating background based on visual index
            var bg = (i % 2 === 0) ? "var(--bg-row-even)" : "var(--bg-row-odd)";
            if (row.dataset.bg !== bg) {
                row.dataset.bg = bg;
                row.style.setProperty("--row-bg", bg);
                row.style.background = bg;
            }

            // Position
            setText(cols[0], String(rider.pos));

            // Positions gained/lost (small-triangle caret + count) vs the locally chosen
            // reference. The plugin sends a delta per reference; pick the one for
            // CONFIG.posDeltaRef. No change (held) or no reference yet → leave the cell
            // blank; only actual gains/losses get a caret + count.
            var pdEl = cols[1];
            var pd = CONFIG.posDeltaRef === "sf" ? rider.posDeltaSf
                   : CONFIG.posDeltaRef === "split" ? rider.posDeltaSplit
                   : rider.posDeltaStart;
            if (typeof pd === "number" && pd > 0) {
                setText(pdEl, "▴" + pd);
                setClass(pdEl, "col-posdelta up");
            } else if (typeof pd === "number" && pd < 0) {
                setText(pdEl, "▾" + (-pd));
                setClass(pdEl, "col-posdelta down");
            } else {
                setText(pdEl, "");
                setClass(pdEl, "col-posdelta held");
            }

            // Number badge + brand strip
            var numBadge = cols[2].children[0];
            var brandStrip = cols[2].children[1];
            setText(numBadge, String(num));

            var brandColor = rider.brandColor || "";
            if (brandStrip.dataset.color !== brandColor) {
                brandStrip.dataset.color = brandColor;
                brandStrip.style.background = brandColor || "transparent";
            }

            // Tracked-rider plate color: tint the number badge to match the in-game
            // plate (e.g. a red tracked-rider plate). Shared helper so every panel
            // renders tracked riders identically.
            applyPlateColor(numBadge, rider);

            // Name — truncate fullName to CONFIG.nameChars characters client-side.
            // (rider.name is a pre-truncated fallback at the in-game standings width;
            // using fullName lets the user configure wider name columns in the overlay.)
            var fullName = rider.fullName || rider.name || "";
            setText(cols[3], fullName.substring(0, CONFIG.nameChars));

            // Gap — format client-side from raw ms values (shared with tail panel)
            var g = computeGap(rider, session);
            setClass(cols[4], g.cls);
            setText(cols[4], g.text);

            // Chips - filter client-side based on CONFIG.chips
            var chipKey = "";
            if (rider.chips) {
                for (var j = 0; j < rider.chips.length; j++) {
                    var chip = rider.chips[j];
                    if (!CONFIG.chips[chip]) continue;
                    chipKey += chip + (chip === "penalty" ? rider.penalty : "") + ";";
                }
            }
            if (chipsEl.dataset.key !== chipKey) {
                chipsEl.dataset.key = chipKey;
                chipsEl.textContent = "";
                if (rider.chips) {
                    for (var k = 0; k < rider.chips.length; k++) {
                        var c = rider.chips[k];
                        if (!CONFIG.chips[c]) continue;
                        var chipSpan = document.createElement("span");
                        if (c === "penalty") {
                            chipSpan.className = "chip chip-penalty chip-wide";
                            chipSpan.textContent = "+" + (rider.penalty || 0) + "s";
                        } else {
                            chipSpan.className = "chip chip-icon chip-" + c;
                        }
                        chipsEl.appendChild(chipSpan);
                    }
                }
            }

            prevPositions[num] = i;
        }

        // Remove rows for riders no longer in standings
        for (var key in riderRows) {
            if (!activeNums[key]) {
                standingsBody.removeChild(riderRows[key]);
                delete riderRows[key];
                delete prevPositions[key];
            }
        }
    }

    // Event log entry structure:
    //   div.event-entry
    //     span.event-time       [0]
    //     span.event-message    [1]
    //     span.event-detail     [2] (optional)
    function createEventEntry() {
        var div = document.createElement("div");
        div.className = "event-entry";
        var timeSpan = document.createElement("span");
        timeSpan.className = "event-time";
        div.appendChild(timeSpan);
        var msgSpan = document.createElement("span");
        msgSpan.className = "event-message";
        div.appendChild(msgSpan);
        return div;
    }

    // Map server event type integers to CONFIG.events keys. The server emits the
    // raw EventLogType enum value as `type` (http_server.cpp), so this array
    // indexes that enum POSITIONALLY — its order must stay in lockstep with
    // EventLogType in event_log_types.h, which is append-only for this reason.
    // Inserting a value mid-list there silently shifts every later event to the
    // wrong filter/label here.
    var EVENT_TYPE_MAP = [
        "session",      // 0  SessionStarted
        "session",      // 1  SessionStateChange
        "session",      // 2  SessionPreStart
        "session",      // 3  SessionComplete
        "fastestLap",   // 4  FastestLap
        "penalty",      // 5  Penalty
        "penalty",      // 6  PenaltyClear
        "penalty",      // 7  PenaltyChange
        "riderOut",     // 8  RiderRetired
        "riderOut",     // 9  RiderDSQ
        "riderOut",     // 10 RiderDNS
        "overtime",     // 11 OvertimeStarted
        "overtime",     // 12 SessionTimeExpired
        "finalLap",     // 13 FinalLap
        "finished",     // 14 RiderFinished
        "leaderChange", // 15 LeaderChange
        "pit",          // 16 PitEntry
        "pit"           // 17 PitExit
    ];

    function isEventEnabled(type) {
        var key = EVENT_TYPE_MAP[type];
        return key ? CONFIG.events[key] !== false : false;
    }

    function renderEventLog(events) {
        if (events) lastEvents = events;
        events = lastEvents || [];

        // Filter events client-side
        var filtered = [];
        for (var i = 0; i < events.length; i++) {
            if (isEventEnabled(events[i].type)) filtered.push(events[i]);
        }

        // Combined display list: status lines (oldest first) followed by
        // events. Status lines are kept in memory in `statusLines` so they
        // can reappear when maxEvents is raised back from 0 — but they
        // share the maxEvents budget like any other entry, so maxEvents=0
        // hides them along with everything else.
        var combined = [];
        for (var s = 0; s < statusLines.length; s++) {
            var sl = statusLines[s];
            combined.push({
                isStatus: true,
                sortKey: sl.sortKey || sl.time,
                time: sl.time,
                message: sl.message,
                status: sl.status
            });
        }
        for (var e = 0; e < filtered.length; e++) {
            combined.push({
                isStatus: false,
                sortKey: filtered[e].clockTime || "",
                evt: filtered[e]
            });
        }
        // Stable chronological sort so status lines and events interleave
        // by wall-clock time instead of all statuses appearing first.
        combined.sort(function (a, b) {
            if (a.sortKey < b.sortKey) return -1;
            if (a.sortKey > b.sortKey) return 1;
            return 0;
        });

        var max = CONFIG.maxEvents > 0 ? CONFIG.maxEvents : 0;
        var display = max > 0 ? combined.slice(-max) : [];

        // Sync DOM children count to display length
        while (eventLog.children.length > display.length) {
            eventLog.removeChild(eventLog.lastChild);
        }
        while (eventLog.children.length < display.length) {
            eventLog.appendChild(createEventEntry());
        }

        var tsOff = CONFIG.timestampMode === "off";
        var tsSession = CONFIG.timestampMode === "session";

        for (var i = 0; i < display.length; i++) {
            var item = display[i];
            var div = eventLog.children[i];

            if (item.isStatus) {
                div.dataset.status = "1";
                setText(div.children[0], item.time);
                div.children[0].style.display = "";
                setText(div.children[1], item.message);
                if (item.status === "ok") div.children[1].style.color = "var(--green)";
                else if (item.status === "error") div.children[1].style.color = "var(--red)";
                else div.children[1].style.color = "";
                if (div.children.length >= 3) div.removeChild(div.children[2]);
            } else {
                div.removeAttribute("data-status");
                var evt = item.evt;
                var ts = "";
                if (tsSession) ts = (evt.sessionTime || "").substring(0, 5);
                else if (!tsOff) ts = (evt.clockTime || "").substring(0, 5);
                setText(div.children[0], ts);
                div.children[0].style.display = tsOff ? "none" : "";
                div.children[1].style.color = "";
                setText(div.children[1], evt.message || "");

                if (evt.detail) {
                    if (div.children.length < 3) {
                        var detailSpan = document.createElement("span");
                        detailSpan.className = "event-detail";
                        div.appendChild(detailSpan);
                    }
                    setText(div.children[2], evt.detail);
                } else if (div.children.length >= 3) {
                    div.removeChild(div.children[2]);
                }
            }
        }
    }

    // --- Rider Focus Card ---
    // Cache querySelector results (DOM structure is static)
    function cacheFocusRow(row) {
        return {
            pos: row.querySelector(".focus-pos"),
            num: row.querySelector(".focus-num"),
            strip: row.querySelector(".focus-strip"),
            name: row.querySelector(".focus-name"),
            interval: row.querySelector(".focus-interval")
        };
    }
    var fc = {
        ahead: cacheFocusRow(focusAhead),
        main: cacheFocusRow(focusMain),
        behind: cacheFocusRow(focusBehind),
        bike: focusMain.querySelector(".focus-bike"),
        lastLap: focusMain.querySelector(".focus-last-lap"),
        bestLap: focusMain.querySelector(".focus-best-lap")
    };

    function setLapTime(el, label, value, isPlaceholder) {
        el.textContent = "";
        var lbl = document.createElement("span");
        lbl.className = "focus-lap-label";
        lbl.textContent = label + " ";
        var val = document.createElement("span");
        val.className = "focus-lap-value" + (isPlaceholder ? " focus-lap-placeholder" : "");
        val.textContent = value;
        el.appendChild(lbl);
        el.appendChild(val);
    }

    function ordinal(n) {
        var s = ["th", "st", "nd", "rd"];
        var v = n % 100;
        return n + (s[(v - 20) % 10] || s[v] || s[0]);
    }

    function showFocusCard() {
        focusVisible = true;
        focusCard.classList.remove("hidden");
        focusCard.classList.remove("focus-out");
        void focusCard.offsetHeight;
        focusCard.classList.add("focus-in");
    }

    var focusHideAnimTimer = null;

    function hideFocusCard() {
        focusVisible = false;
        focusCard.classList.add("focus-out");
        focusCard.classList.remove("focus-in");
        if (focusHideAnimTimer) clearTimeout(focusHideAnimTimer);
        focusHideAnimTimer = setTimeout(function() {
            focusHideAnimTimer = null;
            if (focusCard.classList.contains("focus-out")) {
                focusCard.classList.add("hidden");
            }
        }, cssTimeMs("--anim-focus", 400));
    }

    function resetFocusTimer() {
        if (focusHideTimer) clearTimeout(focusHideTimer);
        if (CONFIG.focusDuration > 0) {
            focusHideTimer = setTimeout(hideFocusCard, CONFIG.focusDuration);
        }
    }

    function renderFocusCard(standings, session) {
        if (!CONFIG.focusCard || !standings || standings.length === 0
            || !session || !session.isSpectating) {
            if (focusVisible) hideFocusCard();
            return;
        }

        // Find the spectated rider (has "camera" chip)
        var idx = -1;
        for (var i = 0; i < standings.length; i++) {
            if (standings[i].chips) {
                for (var j = 0; j < standings[i].chips.length; j++) {
                    if (standings[i].chips[j] === "camera") { idx = i; break; }
                }
            }
            if (idx >= 0) break;
        }

        if (idx < 0) {
            if (focusVisible) hideFocusCard();
            return;
        }

        var rider = standings[idx];
        var isRiderChange = (rider.num !== focusLastSeenNum);
        focusLastSeenNum = rider.num;

        // Only show/re-show when the spectated rider actually changes
        if (isRiderChange) {
            console.log("[mxbmrp3] spectated rider changed: #" + rider.num, rider.name || "");
            showFocusCard();
            resetFocusTimer();
        }

        // Update content silently (gaps/laps change even if rider doesn't)
        if (!focusVisible) return;

        var ahead = idx > 0 ? standings[idx - 1] : null;
        var behind = idx < standings.length - 1 ? standings[idx + 1] : null;

        populateFocusNeighbor(focusAhead, fc.ahead, ahead, rider);
        populateFocusNeighbor(focusBehind, fc.behind, behind, rider);

        setText(fc.main.pos, ordinal(rider.pos));
        setText(fc.main.num, String(rider.num));
        applyPlateColor(fc.main.num, rider);
        fc.main.strip.style.background = rider.brandColor || "transparent";
        setText(fc.main.name, rider.fullName || rider.name || "");
        var bikeText = rider.bike || "";
        if (rider.brand && bikeText.indexOf(rider.brand) < 0) bikeText = rider.brand + " " + bikeText;
        setText(fc.bike, bikeText);
        var lastStr = rider.lastLapMs > 0 ? formatLapTime(rider.lastLapMs) : "";
        var bestStr = rider.bestLapMs > 0 ? formatLapTime(rider.bestLapMs) : "";
        setLapTime(fc.lastLap, "Last", lastStr || LAP_PLACEHOLDER, !lastStr);
        setLapTime(fc.bestLap, "Best", bestStr || LAP_PLACEHOLDER, !bestStr);
    }

    // Compute interval between neighbor and spectated rider from their leader gaps
    function formatInterval(neighborMs, spectatedMs, neighborLaps, spectatedLaps) {
        // Lap gap takes priority
        var lapDiff = (neighborLaps || 0) - (spectatedLaps || 0);
        if (lapDiff !== 0) return (lapDiff > 0 ? "+" : "") + lapDiff + "L";

        // Time interval: positive = neighbor is behind, negative = neighbor is ahead
        // Leader has gapMs=0 which is valid (they're the reference point)
        if (typeof neighborMs === "number" && typeof spectatedMs === "number" &&
            (neighborMs > 0 || spectatedMs > 0)) {
            var diffMs = neighborMs - spectatedMs;
            var absDiff = Math.abs(diffMs);
            var sign = diffMs >= 0 ? "+" : "-";
            var secs = Math.floor(absDiff / 1000);
            var tenths = Math.floor((absDiff % 1000) / 100);
            if (secs >= 60) {
                var mins = Math.floor(secs / 60);
                secs = secs % 60;
                return sign + mins + ":" + (secs < 10 ? "0" : "") + secs + "." + tenths;
            }
            return sign + secs + "." + tenths;
        }
        return "";
    }

    function populateFocusNeighbor(row, els, neighbor, spectated) {
        if (!neighbor) {
            row.style.display = "none";
            return;
        }
        row.style.display = "";
        setText(els.pos, ordinal(neighbor.pos));
        setText(els.num, String(neighbor.num));
        applyPlateColor(els.num, neighbor);
        els.strip.style.background = neighbor.brandColor || "transparent";
        setText(els.name, neighbor.fullName || neighbor.name || "");

        // Compute interval relative to spectated rider
        var interval = formatInterval(neighbor.gapMs, spectated.gapMs, neighbor.gapLaps, spectated.gapLaps);
        setText(els.interval, interval);
    }

    // --- Bottom-slot panels ---
    // The standings tower has one shared "bottom slot" used by several broadcast
    // panels (fastest-last-lap, fastest-laps, down-the-order, battle). Only one is
    // visible at a time. Each panel is a small spec around createSlotPanel(), which
    // owns the gate slide, the masking flag (held through the slide-out), mutual
    // exclusion, the appearance interval + auto-hide, and reporting how many bottom
    // tower rows it covers (so renderStandings can hide the chips hanging off them).
    var slotPanels = [];

    // True while forceSlot() is mid-hand-off (it has slid the current panel out
    // and is waiting to open the forced one). show() checks this so the auto-filler
    // (battle, triggerOnEligible) can't re-grab the slot in the outgoing panel's
    // slide-out gap before the forced panel opens.
    var slotForcing = false;

    // Priority-aware slot arbitration. Each panel has a priority; the timed
    // boards (default 1) outrank the battle filler (0). A panel may take the slot
    // only if no EQUAL-or-higher-priority panel is masking OR mid-hand-off
    // (pending) — counting pending is what stops the filler from re-grabbing the
    // slot during a board's evict→open gap (the evicted panel's slide-out render()
    // would otherwise re-show the cooldown-free battle just before the board
    // opens, leaving both masking). The caller isn't masking/pending yet when it
    // checks, so it never blocks itself.
    function activeSlotPriority() {
        var p = -1;
        for (var i = 0; i < slotPanels.length; i++) {
            var sp = slotPanels[i];
            if (sp.masking() || sp.pending()) {
                // A broadcaster-forced panel holds the slot at top priority for its
                // duration, so a board's scheduled turn can't evict it mid-show.
                var pr = sp.forced() ? Infinity : sp.priority;
                if (pr > p) p = pr;
            }
        }
        return p;
    }
    // Slide out any masking panel below `pri` (a board pre-empting the battle
    // filler). Returns true if one was evicted, so the caller can wait for the
    // slide-out before opening. No rest cooldown is applied to an evicted panel,
    // so the battle reclaims the slot as soon as the board finishes.
    function evictBelow(pri) {
        var any = false;
        for (var i = 0; i < slotPanels.length; i++) {
            var sp = slotPanels[i];
            if (sp.masking() && sp.priority < pri) { sp.hide(); any = true; }
        }
        return any;
    }

    // Bottom rows covered by the active panel (0 if none) — renderStandings hides
    // the chips on those rows, since they hang outside the tower-width panel.
    function slotCoveredRows() {
        for (var i = 0; i < slotPanels.length; i++) {
            var c = slotPanels[i].covered();
            if (c) return c;
        }
        return 0;
    }

    function manageSlots(session) {
        for (var i = 0; i < slotPanels.length; i++) slotPanels[i].manage(session);
    }

    // Broadcaster force: bring the named bottom-slot panel in at top priority,
    // sliding out whatever's active first. Aborts quietly if the panel is off or
    // has nothing to show (e.g. "force battle" with no battle), leaving the
    // current panel untouched.
    function forceSlot(name) {
        var target = null;
        for (var i = 0; i < slotPanels.length; i++) {
            if (slotPanels[i].name === name) { target = slotPanels[i]; break; }
        }
        if (!target) { console.log("[mxbmrp3] forceSlot: no panel named", name); return; }
        // Manual force is intentionally gated on NEITHER enabled() nor
        // eligible(): disabling only stops auto-rotation, and a broadcaster can
        // force a panel even outside its usual conditions (e.g. the lap boards
        // during qualifying). The lap boards even show an empty title strip when
        // there's no data yet (showEmptyWhenForced) so the caster sees the hotkey
        // worked; panels that can't be faked (tail) still no-op via build().
        if (target.masking()) { console.log("[mxbmrp3] forceSlot:", name, "already on screen"); return; }
        console.log("[mxbmrp3] forceSlot: forcing", name, "in");

        // Slide out any other panel holding the slot, then force the target in
        // once it has cleared so the active board visibly slides back first.
        var busy = false;
        for (var j = 0; j < slotPanels.length; j++) {
            if (slotPanels[j] !== target && slotPanels[j].masking()) {
                slotPanels[j].hide();
                busy = true;
            }
        }
        if (busy) {
            // Reserve the slot through the slide-out gap so the auto-filler can't
            // re-grab it before the forced panel opens (cleared once it has).
            slotForcing = true;
            setTimeout(function () {
                slotForcing = false;   // clear first so a throw in force() can't strand it
                target.force();
            }, cssTimeMs("--anim-slide", 1000) + 70);
        } else {
            target.force();
        }
    }

    // Edge-triggered on the plugin's monotonic seq so each in-game keypress
    // fires once. lastForcedSeq starts unset (-1) and adopts the first seq seen
    // so connecting mid-session doesn't replay a stale command.
    var lastForcedSeq = -1;
    function handleOverlayCommand(data) {
        if (!data || !data.overlayCmd) return;
        var seq = data.overlayCmd.seq | 0;
        if (lastForcedSeq < 0) {
            lastForcedSeq = seq;
            console.log("[mxbmrp3] overlayCmd present in snapshot; baseline seq =", seq);
            return;
        }
        if (seq === lastForcedSeq) return;
        lastForcedSeq = seq;
        console.log("[mxbmrp3] overlay force command received: panel =", data.overlayCmd.panel, "seq =", seq);
        if (data.overlayCmd.panel) forceSlot(data.overlayCmd.panel);
    }

    // spec: { panel, name, enabled(), eligible(session), interval(),
    //         build() -> coveredRows (0 aborts), autoHide() -> ms (0 = self-hiding),
    //         refresh()? (called while visible), onShow()?/onHide()? (panel timers) }
    function createSlotPanel(spec) {
        var visible = false, masking = false, covered = 0;
        var forcedShow = false;  // true while held open by a broadcaster force
        var nextShowAt = 0, hideTimer = null, animTimer = null;
        var priority = (spec.priority != null) ? spec.priority : 1;
        var pending = false;     // true during an evict-then-open slide hand-off
        var cooldownUntil = 0;   // earliest re-show after hitting the max cap
        var inClass = spec.name + "-in", outClass = spec.name + "-out";

        // Slide the panel in. Returns false if build() had nothing to show.
        // Shared by the scheduled show() and the broadcaster-forced force();
        // `forced` protects it from manage()'s eligibility hide (see manage()).
        function openPanel(forced) {
            var n = spec.build();
            var emptyForced = false;
            if (!n) {
                // Nothing to show. A scheduled showing defers. A force on a panel
                // that opts in (showEmptyWhenForced) shows its title strip anyway,
                // so a caster gets visible confirmation the hotkey fired before
                // data exists (e.g. forcing the lap boards before the first lap).
                // Panels that can't be faked (tail = no backmarkers) still no-op.
                if (!forced || !spec.showEmptyWhenForced) return false;
                emptyForced = true;
                // Render the panel's "no data" placeholder in its normal row
                // style so the caster sees a real board, not a bare title.
                n = spec.renderEmpty ? spec.renderEmpty() : 1;
            }
            forcedShow = !!forced;
            covered = n;
            visible = true;
            masking = true;
            if (animTimer) { clearTimeout(animTimer); animTimer = null; }
            spec.panel.classList.remove("hidden", outClass);
            void spec.panel.offsetHeight;  // restart the slide-in from hidden
            spec.panel.classList.add(inClass);
            if (spec.onShow && !emptyForced) spec.onShow();
            render();                      // apply the chip mask to covered rows
            if (hideTimer) { clearTimeout(hideTimer); hideTimer = null; }
            var ms = spec.autoHide ? spec.autoHide() : 0;
            if (ms > 0) hideTimer = setTimeout(function () {
                // Reached the max on-screen cap. Rest for restAfter() (if any)
                // before this panel may re-appear, so a sustained battle doesn't
                // park in the slot permanently.
                if (spec.restAfter) cooldownUntil = Date.now() + spec.restAfter();
                hide();
            }, ms);
            return true;
        }

        function show() {
            if (slotForcing) return;   // a manual force owns the slot for now
            if (visible || pending || !lastData || !lastData.standings) return;
            if (activeSlotPriority() >= priority) return;   // equal/higher panel holds the slot
            if (evictBelow(priority)) {
                // A lower-priority filler (battle) holds the slot — slide it out,
                // then open once it has cleared (mirrors forceSlot's hand-off).
                pending = true;
                setTimeout(function () {
                    pending = false;
                    if (!visible && !slotForcing && activeSlotPriority() < priority) openPanel(false);
                }, cssTimeMs("--anim-slide", 1000) + 70);
                return;
            }
            openPanel(false);
        }

        // Broadcaster force: bypass the priority gate and the cadence timer
        // (the forceSlot dispatcher has already slid out any active panel).
        // Momentary - re-arms the cadence so the normal rotation resumes after
        // the forced showing auto-hides.
        function force() {
            if (visible || !lastData || !lastData.standings) return;
            if (openPanel(true) && spec.interval) nextShowAt = Date.now() + spec.interval() * 1000;
        }

        function hide() {
            visible = false;
            forcedShow = false;
            if (hideTimer) { clearTimeout(hideTimer); hideTimer = null; }
            if (spec.onHide) spec.onHide();
            spec.panel.classList.add(outClass);
            spec.panel.classList.remove(inClass);
            if (animTimer) clearTimeout(animTimer);
            animTimer = setTimeout(function () {
                animTimer = null;
                if (spec.panel.classList.contains(outClass)) {
                    spec.panel.classList.add("hidden");
                    masking = false;       // slide-out done — restore the chips
                    render();
                }
            }, cssTimeMs("--anim-slide", 1000) + 50); // just past the slide transition
        }

        function manage(session) {
            if (spec.enabled() && spec.eligible(session)) {
                var now = Date.now();
                if (spec.triggerOnEligible) {
                    // No cadence (the battle filler): appear as soon as a battle is
                    // eligible and the slot is free of equal/higher-priority panels,
                    // after any post-cap rest. Boards still pre-empt it via show().
                    if (!visible && !pending && now >= cooldownUntil) show();
                } else {
                    // Timestamp-based cadence (not a setInterval): arm the next
                    // showing one interval out and only advance it once a showing
                    // actually happens. An eligibility that flickers between
                    // snapshots must NOT keep resetting the clock, or the panel
                    // would be starved of its turn in exactly the busy races it
                    // exists for. manage() runs every render(), so it polls.
                    if (nextShowAt === 0) nextShowAt = now + spec.interval() * 1000;
                    if (!visible && !pending && now >= nextShowAt) {
                        show();
                        if (visible || pending) nextShowAt = now + spec.interval() * 1000;
                    }
                }
                if (visible && spec.refresh) spec.refresh();
            } else if (visible) {
                // Not enabled/eligible. A manually-forced panel stays up (and keeps
                // refreshing) until its own auto-hide - force is intentionally
                // condition-agnostic. A scheduled/triggered panel that just lost
                // eligibility is hidden with NO rest cooldown, so a fresh battle can
                // appear promptly. Cadence clock is left as-is across flickers.
                if (forcedShow) {
                    if (spec.refresh) spec.refresh();
                } else {
                    hide();
                }
            }
        }

        // applySettings: re-arm the cadence so the next manage() applies the
        // current interval, and hide immediately if the panel was switched off.
        function refreshTimer() {
            nextShowAt = 0;  // re-armed (now + interval) on the next manage()
            if (!spec.enabled() && visible) hide();
        }

        var ctrl = {
            name: spec.name,
            priority: priority,
            manage: manage,
            hide: hide,
            force: force,
            refreshTimer: refreshTimer,
            masking: function () { return masking; },
            pending: function () { return pending; },
            forced: function () { return forcedShow; },
            covered: function () { return masking ? covered : 0; }
        };
        slotPanels.push(ctrl);
        return ctrl;
    }


    // Shared lap-time board: a rank / name / time list used by both the
    // fastest-last-lap and fastest-lap (session best) panels.
    function createBoardRow() {
        var row = document.createElement("div");
        row.className = "board-row";
        var rank = document.createElement("span");
        rank.className = "board-rank";
        row.appendChild(rank);
        var name = document.createElement("span");
        name.className = "board-name";
        row.appendChild(name);
        var time = document.createElement("span");
        time.className = "board-time";
        row.appendChild(time);
        return row;
    }

    // Rank active riders by a lap-time field (ascending), fill listEl, and size
    // its rows + titleEl to the measured pixel row height so the panel is an exact
    // whole number of rows tall (top lands on a row boundary, no sliver peeking).
    // valueFn(rider) returns the ms to rank by (<=0/missing = skip). Returns count.
    function renderLapBoard(listEl, titleEl, standings, valueFn, count) {
        var riders = [];
        for (var i = 0; i < standings.length; i++) {
            var r = standings[i];
            var v = valueFn(r);
            if (v && v > 0 &&
                r.state !== STATE_DNS && r.state !== STATE_RETIRED && r.state !== STATE_DSQ) {
                riders.push({ rider: r, value: v });
            }
        }
        riders.sort(function (a, b) { return a.value - b.value; });
        if (count > 0 && riders.length > count) riders = riders.slice(0, count);

        while (listEl.children.length > riders.length) listEl.removeChild(listEl.lastChild);
        while (listEl.children.length < riders.length) listEl.appendChild(createBoardRow());

        var rh = measureRowHeight();
        titleEl.style.height = rh + "px";
        for (var j = 0; j < riders.length; j++) {
            var row = listEl.children[j];
            row.classList.remove("board-empty");   // reused row may have held the placeholder
            row.style.height = rh + "px";
            setText(row.children[0], (j + 1) + ".");
            setText(row.children[1], riders[j].rider.fullName || riders[j].rider.name || "");
            setText(row.children[2], formatLapTime(riders[j].value));
        }
        return riders.length;
    }

    // Empty-board placeholder for a forced lap board with no data yet: one row
    // in the normal board-row style reading "No data". Returns the covered row
    // count (title + the placeholder row), matching the build() convention.
    function renderLapBoardEmpty(listEl, titleEl) {
        while (listEl.children.length > 1) listEl.removeChild(listEl.lastChild);
        if (listEl.children.length < 1) listEl.appendChild(createBoardRow());
        var rh = measureRowHeight();
        titleEl.style.height = rh + "px";
        var row = listEl.children[0];
        row.style.height = rh + "px";
        row.classList.add("board-empty");   // flush-left, muted (see style.css)
        setText(row.children[0], "");
        setText(row.children[1], "No data");
        setText(row.children[2], "");
        return 2;  // title + placeholder row
    }

    // Rows the standings tower currently shows (after the DNS filter and the Max
    // Riders cap), mirroring renderStandings. A lap board covers count + 1 rows
    // (title + riders), so its rider count is capped to this − 1 to keep the panel
    // from overshooting the tower's top edge and driving the chip-mask threshold
    // (visibleCount − coveredRows) negative (which would hide every row's chips).
    function visibleTowerRows(standings) {
        if (!standings) return 0;
        var n = standings.length;
        if (CONFIG.hideDns) {
            n = 0;
            for (var i = 0; i < standings.length; i++) {
                if (standings[i].state !== STATE_DNS) n++;
            }
        }
        if (CONFIG.maxRiders > 0 && CONFIG.maxRiders < n) n = CONFIG.maxRiders;
        return n;
    }

    // --- Fastest Last Lap Times (most recent lap) ---
    // Race-only. Lists riders by their most recent completed lap; the live list
    // is kept fresh while visible as new laps come in.
    function renderFastLapList(standings) {
        var cap = Math.max(1, visibleTowerRows(standings) - 1);
        return renderLapBoard(fastLapList, fastLapTitle, standings,
            function (r) { return r.lastLapMs; }, Math.min(CONFIG.fastLapCount, cap));
    }
    createSlotPanel({
        panel: fastLapPanel, name: "fastlap",
        enabled: function () { return CONFIG.fastLap; },
        eligible: function (s) { return !!(s && s.isRace); },
        interval: function () { return CONFIG.fastLapInterval; },
        build: function () { var n = renderFastLapList(lastData.standings); return n ? n + 1 : 0; },
        refresh: function () { if (!renderFastLapList(lastData.standings)) renderLapBoardEmpty(fastLapList, fastLapTitle); },
        autoHide: function () { return CONFIG.fastLapDuration; },
        showEmptyWhenForced: true,   // forced before any laps: show a "No data" board
        renderEmpty: function () { return renderLapBoardEmpty(fastLapList, fastLapTitle); }
    });

    // --- Down the Order (tail) carousel ---
    // When Max Riders crops the tower, periodically page through the riders hidden
    // below the cutoff. The tail snapshot is captured at show time and held for the
    // cycle (pages don't reshuffle mid-flyby); the carousel self-terminates after
    // the last page, so the panel uses no auto-hide.
    var tailPageTimer = null;      // page-advance timeout
    var tailPageIndex = 0;
    var tailPageCount = 0;
    var tailPageRanges = [];       // "11–15" label per page

    // The riders hidden below the Max Riders cutoff, after the same DNS filter
    // the tower applies. Empty when the tower shows everyone.
    function getTailRiders(standings) {
        var limit = CONFIG.maxRiders;
        if (!limit || !standings) return [];
        var list = standings;
        if (CONFIG.hideDns) {
            list = [];
            for (var i = 0; i < standings.length; i++) {
                if (standings[i].state !== STATE_DNS) list.push(standings[i]);
            }
        }
        return list.length > limit ? list.slice(limit) : [];
    }

    // Build a standings-style row (same columns as the tower's .row-main) for
    // the bottom-slot panels (tail, battle). `cls` is the base class.
    function createGridRow(cls) {
        var row = document.createElement("div");
        row.className = cls;
        var pos = document.createElement("span");
        pos.className = "col-pos";
        row.appendChild(pos);
        var posdelta = document.createElement("span");  // spacer; respects hide-posdelta
        posdelta.className = "col-posdelta";
        row.appendChild(posdelta);
        var num = document.createElement("span");
        num.className = "col-num";
        var numBadge = document.createElement("span");
        numBadge.className = "num-badge";
        num.appendChild(numBadge);
        var brandStrip = document.createElement("span");
        brandStrip.className = "brand-strip";
        num.appendChild(brandStrip);
        row.appendChild(num);
        var name = document.createElement("span");
        name.className = "col-name";
        row.appendChild(name);
        var gap = document.createElement("span");
        gap.className = "col-gap";
        row.appendChild(gap);
        return row;
    }

    // Populate a grid row. `gap` is {text, cls} — the caller decides what the
    // gap column shows (tower gap for the tail, intra-battle interval for a battle).
    function setGridRow(row, cls, rider, gap) {
        var cols = row.children; // [pos, posdelta, num, name, gap]
        var inactive = rider.state === STATE_DNS || rider.state === STATE_RETIRED || rider.state === STATE_DSQ;
        row.className = cls + (inactive ? " state-inactive" : "");
        setText(cols[0], String(rider.pos));
        setText(cols[2].children[0], String(rider.num));
        applyPlateColor(cols[2].children[0], rider);
        cols[2].children[1].style.background = rider.brandColor || "transparent";
        setText(cols[3], (rider.fullName || rider.name || "").substring(0, CONFIG.nameChars));
        setClass(cols[4], gap.cls);
        setText(cols[4], gap.text);
    }

    // Build all pages from the captured tail; returns the page count.
    function buildTailPages(standings, session) {
        var tail = getTailRiders(standings);
        var pageSize = CONFIG.tailPageSize;
        var pages = tail.length ? Math.ceil(tail.length / pageSize) : 0;

        tailTrack.textContent = "";
        tailPageRanges = [];
        var rh = measureRowHeight();
        tailTitle.style.height = rh + "px";
        tailViewport.style.height = (pageSize * rh) + "px";

        for (var p = 0; p < pages; p++) {
            var page = document.createElement("div");
            page.className = "tail-page";
            var first = null, last = null;
            for (var k = 0; k < pageSize; k++) {
                var rider = tail[p * pageSize + k];
                var row = createGridRow("tail-row");
                row.style.height = rh + "px";
                if (rider) {
                    setGridRow(row, "tail-row", rider, computeGap(rider, session));
                    if (first === null) first = rider.pos;
                    last = rider.pos;
                } else {
                    row.className = "tail-row tail-row-empty";
                }
                page.appendChild(row);
            }
            tailPageRanges.push(first === last ? String(first) : (first + "–" + last));
            tailTrack.appendChild(page);
        }
        return pages;
    }

    function updateTailTitle() {
        setText(tailTitle, "Positions " + (tailPageRanges[tailPageIndex] || ""));
    }

    function scheduleTailStep() {
        if (tailPageTimer) clearTimeout(tailPageTimer);
        tailPageTimer = setTimeout(tailStep, CONFIG.tailPageHold * 1000);
    }

    function tailStep() {
        if (tailPageIndex + 1 < tailPageCount) {
            tailPageIndex++;
            tailTrack.style.transform = "translateX(-" + (tailPageIndex * 100) + "%)";
            updateTailTitle();
            scheduleTailStep();
        } else {
            tailCtrl.hide();   // carousel finished — drop the gate
        }
    }

    var tailCtrl = createSlotPanel({
        panel: tailPanel, name: "tail",
        enabled: function () { return CONFIG.tail; },
        // Any active session where the tower is cropping the field (riders hidden
        // below the Max Riders cutoff).
        eligible: function (s) {
            return !!(s && s.type && lastData && getTailRiders(lastData.standings).length > 0);
        },
        interval: function () { return CONFIG.tailInterval; },
        build: function () {
            var pages = buildTailPages(lastData.standings, lastData.session);
            if (!pages) return 0;
            tailPageCount = pages;
            tailPageIndex = 0;
            // Reset the carousel to the first page without animating.
            tailTrack.style.transition = "none";
            tailTrack.style.transform = "translateX(0)";
            void tailTrack.offsetWidth;
            tailTrack.style.transition = "";
            updateTailTitle();
            return CONFIG.tailPageSize + 1;   // title + page rows
        },
        autoHide: function () { return 0; },   // self-terminating via paging
        onShow: function () { scheduleTailStep(); },
        onHide: function () { if (tailPageTimer) { clearTimeout(tailPageTimer); tailPageTimer = null; } }
    });

    // --- Battle panel ---
    // Spotlights a cluster of riders running nose-to-tail in the bottom slot
    // (shared with the fastlap/tail panels, mutually exclusive). Cycles through
    // the detected battles on successive appearances. Race-only. The chosen
    // battle is captured at show time and held for the duration.
    var battleCycle = 0;           // rotates which battle is shown
    var BATTLE_MAX_RIDERS = 6;     // cap a single battle's row count

    // Find clusters of consecutive same-lap riders each within the closeness
    // threshold of the next. Returns an array of rider-arrays (front-first).
    function detectBattles(standings, session) {
        if (!session || !session.isRace || !standings) return [];
        var active = [];
        for (var i = 0; i < standings.length; i++) {
            var s = standings[i];
            if (s.state !== STATE_DNS && s.state !== STATE_RETIRED && s.state !== STATE_DSQ) {
                active.push(s);
            }
        }
        var thr = CONFIG.battleGap * 1000;
        var battles = [];
        var a = 0;
        while (a < active.length) {
            var b = a;
            while (b + 1 < active.length) {
                var cur = active[b], next = active[b + 1];
                var lapGap = (next.gapLaps || 0) - (cur.gapLaps || 0);
                var interval = (next.gapMs || 0) - (cur.gapMs || 0);
                if (lapGap === 0 && interval > 0 && interval <= thr) b++;
                else break;
            }
            if (b > a) battles.push(active.slice(a, b + 1));
            a = b + 1;
        }
        return battles;
    }

    // Small element helper for the battle card.
    function battleEl(tag, cls, text) {
        var e = document.createElement(tag);
        e.className = cls;
        if (text !== undefined) e.textContent = text;
        return e;
    }

    // Build a card per battling rider: a left column (main identity line +
    // detail sub-rows) tinted in the rider's brand colour, and a large right-
    // hand headline — the rider's position (front of the battle) or their
    // interval to the front (everyone else). Rebuilt each show. Returns the body
    // height in row-heights (riders * rowsPerCard).
    function buildBattle(group) {
        var rh = measureRowHeight();
        battleTitle.style.height = rh + "px";
        battleList.textContent = "";

        var subs = [];
        if (CONFIG.battleBike)    subs.push("bike");
        if (CONFIG.battleLastLap) subs.push("last");
        if (CONFIG.battleFastLap) subs.push("fast");
        var rowsPerCard = 1 + subs.length;   // detail sub-rows + the identity strip

        // Cap how much of the tower the battle may cover so it never fills the
        // whole thing: at most ~2/3 of the visible rows (minus the title), always
        // at least 2 riders (a battle), and never more than BATTLE_MAX_RIDERS.
        // Cap against the rows the tower actually shows (after the DNS filter and
        // Max Riders), matching visibleTowerRows() used by the lap boards.
        var visibleRows = visibleTowerRows(lastData.standings);
        var maxBody = Math.max(rowsPerCard, Math.floor(visibleRows * 0.66) - 1);
        var ridersByHeight = Math.max(2, Math.floor(maxBody / rowsPerCard));
        var rows = group.slice(0, Math.min(BATTLE_MAX_RIDERS, ridersByHeight));

        var frontGap = rows[0].gapMs || 0;
        for (var i = 0; i < rows.length; i++) {
            var r = rows[i];
            var card = battleEl("div", "battle-card");
            // Brand identity: a left-weighted tint + a solid accent bar. Computed
            // in JS (rgba) rather than CSS color-mix for older OBS/CEF support.
            var rgb = hexToRgb(r.brandColor || "");
            if (rgb) {
                card.style.backgroundImage = "linear-gradient(90deg, rgba(" +
                    rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0.32) 0%, rgba(" +
                    rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0) 70%)";
            }

            var bodyCol = battleEl("div", "battle-body");

            // Detail sub-rows (above the identity strip).
            for (var s = 0; s < subs.length; s++) {
                var sub = battleEl("div", "battle-sub");
                sub.style.height = rh + "px";
                if (subs[s] === "bike") {
                    // No label — the bike name speaks for itself.
                    sub.appendChild(battleEl("span", "battle-sub-label", r.bike || r.brand || "—"));
                } else if (subs[s] === "last") {
                    sub.appendChild(battleEl("span", "battle-sub-label", "Last"));
                    sub.appendChild(battleEl("span", "battle-sub-val battle-sub-time",
                        r.lastLapMs > 0 ? formatLapTime(r.lastLapMs) : LAP_PLACEHOLDER));
                } else {
                    sub.appendChild(battleEl("span", "battle-sub-label", "Best"));
                    sub.appendChild(battleEl("span", "battle-sub-val battle-sub-time",
                        r.bestLapMs > 0 ? formatLapTime(r.bestLapMs) : LAP_PLACEHOLDER));
                }
                bodyCol.appendChild(sub);
            }

            // Identity strip at the bottom: <ordinal> <plate> <name>, with the
            // whole row highlighted in the rider's brand colour. Columns mirror
            // the tower (pos + the optional +/- column, then plate w/ brand strip)
            // so the plate and name line up with the tower; the full name shows
            // and is clipped if it overflows.
            var id = battleEl("div", "battle-main");
            id.style.height = rh + "px";
            if (rgb) id.style.backgroundColor = "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0.4)";
            id.appendChild(battleEl("span", "battle-pos", ordinal(r.pos).toUpperCase()));
            var num = battleEl("span", "col-num");
            var battleBadge = battleEl("span", "num-badge", String(r.num));
            applyPlateColor(battleBadge, r);
            num.appendChild(battleBadge);
            var strip = battleEl("span", "brand-strip");
            strip.style.background = r.brandColor || "transparent";
            num.appendChild(strip);
            id.appendChild(num);
            id.appendChild(battleEl("span", "battle-name", r.fullName || r.name || ""));
            bodyCol.appendChild(id);

            card.appendChild(bodyCol);

            // Right column headline: the leader shows their position, everyone
            // else their interval to the front of the battle.
            card.appendChild(battleEl("div", "battle-value",
                i === 0 ? ordinal(r.pos).toUpperCase() : formatGap((r.gapMs || 0) - frontGap)));

            battleList.appendChild(card);
        }
        return rows.length * rowsPerCard;   // body row-heights covered
    }

    // Empty-board placeholder for a forced battle panel with no active battle:
    // a flush, muted "No data" row under a generic "Battle" title. Returns the
    // covered row count (title + placeholder).
    function renderBattleEmpty() {
        var rh = measureRowHeight();
        battleTitle.style.height = rh + "px";
        setText(battleTitle, "Battle");
        battleList.textContent = "";
        var row = document.createElement("div");
        row.className = "board-empty";
        row.style.height = rh + "px";
        row.textContent = "No data";
        battleList.appendChild(row);
        return 2;  // title + placeholder row
    }

    createSlotPanel({
        panel: battlePanel, name: "battle",
        // Filler panel: priority 0 so the timed boards (priority 1) pre-empt it,
        // and triggerOnEligible so it has no cadence — it slides in the moment a
        // battle exists and the slot is free, instead of waiting for a timer.
        priority: 0,
        triggerOnEligible: true,
        enabled: function () { return CONFIG.battle; },
        eligible: function (s) {
            return !!(s && s.isRace && lastData && detectBattles(lastData.standings, s).length > 0);
        },
        // interval is no longer a cadence (triggerOnEligible); kept only so a
        // broadcaster force() has a value to re-arm with. restAfter repurposes it
        // as the rest AFTER the battle has cycled through every current battle once:
        // within a pass the battles show back-to-back (no rest between, so the slot
        // doesn't go idle while battles are waiting); only once the whole set has
        // been shown does the panel rest battleInterval before the next pass — so a
        // sustained set doesn't park forever. build() bumps battleCycle for the
        // showing that's ending, so a pass is complete when it lands on a multiple
        // of the current battle count. A lone battle rests after each showing. A
        // battle that ends or is evicted by a board gets no cooldown.
        interval: function () { return CONFIG.battleInterval; },
        restAfter: function () {
            var n = (lastData && lastData.session)
                ? detectBattles(lastData.standings, lastData.session).length : 0;
            if (n > 1 && (battleCycle % n) !== 0) return 0;   // mid-pass: straight to the next
            return CONFIG.battleInterval * 1000;
        },
        build: function () {
            var battles = detectBattles(lastData.standings, lastData.session);
            if (!battles.length) return 0;
            var group = battles[battleCycle % battles.length];
            battleCycle++;
            var n = buildBattle(group);
            if (!n) return 0;
            var frontPos = group[0].pos;
            setText(battleTitle, frontPos === 1 ? "Battle for the Lead" : "Battle for " + ordinal(frontPos));
            return n + 1;   // title + battle rows
        },
        autoHide: function () { return CONFIG.battleDuration; },   // max on-screen cap
        showEmptyWhenForced: true,   // forced with no active battle: show "No data"
        renderEmpty: function () { return renderBattleEmpty(); }
    });

    // --- Fastest Laps (session best) ---
    // Race-only. Ranks riders by their best lap of the session (rank 1 = the
    // overall fastest lap). Kept fresh while visible.
    function renderBestLapList(standings) {
        var cap = Math.max(1, visibleTowerRows(standings) - 1);
        return renderLapBoard(bestLapList, bestLapTitle, standings,
            function (r) { return r.bestLapMs; }, Math.min(CONFIG.bestLapCount, cap));
    }
    createSlotPanel({
        panel: bestLapPanel, name: "bestlap",
        enabled: function () { return CONFIG.bestLap; },
        eligible: function (s) { return !!(s && s.isRace); },
        interval: function () { return CONFIG.bestLapInterval; },
        build: function () { var n = renderBestLapList(lastData.standings); return n ? n + 1 : 0; },
        refresh: function () { if (!renderBestLapList(lastData.standings)) renderLapBoardEmpty(bestLapList, bestLapTitle); },
        autoHide: function () { return CONFIG.bestLapDuration; },
        showEmptyWhenForced: true,   // forced before any laps: show a "No data" board
        renderEmpty: function () { return renderLapBoardEmpty(bestLapList, bestLapTitle); }
    });

    // --- Settings Panel ---
    var gearEl = document.getElementById("settings-gear");
    var panelEl = document.getElementById("settings-panel");
    var panelOpen = false;
    var mouseTimer = null;
    var MOUSE_HIDE_DELAY = 3000;

    function applySettings() {
        applyRootSizing();
        eventLogRowHeight = 0; // re-measure after font size change
        if (!CONFIG.focusCard && focusVisible) hideFocusCard();
        // Drop each bottom-slot panel's timer so the next render re-creates it
        // with the current interval / enabled state; hide any switched off now.
        for (var sp = 0; sp < slotPanels.length; sp++) slotPanels[sp].refreshTimer();
        // Force chip refresh by clearing cached keys
        for (var num in riderRows) {
            riderRows[num].children[1].dataset.key = "";
        }
        updatePreviewSizing();
        if (lastData) render();
        else renderEventLog();
        saveSettings();
    }

    // Preview sizing: show empty placeholder space so users can see how
    // much room the tower and event log will occupy with current settings.
    // Lets users gauge the overlay footprint when adjusting maxRiders/maxEvents.
    function updatePreviewSizing() {
        // Standings: set min-height to reflect maxRiders (0 = default 5)
        var previewRows = CONFIG.maxRiders > 0 ? CONFIG.maxRiders : 5;
        standingsBody.style.minHeight = "calc(" + previewRows + " * var(--row-height))";

        // Event log: measure one entry's height, set min-height for configured count
        if (!eventLogRowHeight) {
            var probe = createEventEntry();
            probe.children[0].textContent = "\u00A0";
            probe.children[1].textContent = "\u00A0";
            eventLog.appendChild(probe);
            eventLogRowHeight = probe.offsetHeight || 14;
            eventLog.removeChild(probe);
        }
        var eventPad = parseFloat(getComputedStyle(eventLog).paddingTop) || 0;
        eventLog.style.minHeight = (CONFIG.maxEvents * eventLogRowHeight + eventPad * 2) + "px";
    }
    var eventLogRowHeight = 0;

    // Mouse activity — gear show/hide
    function revealGear() {
        gearEl.classList.add("visible");
        if (mouseTimer) clearTimeout(mouseTimer);
        mouseTimer = setTimeout(function () {
            if (!panelOpen) gearEl.classList.remove("visible");
        }, MOUSE_HIDE_DELAY);
    }
    // Reveal on mouse movement (desktop) or a screen touch (mobile — mousemove
    // doesn't fire there), then auto-hide so the gear never sits permanently over
    // the top-right of the full-width tower. Touch uses a passive listener so it
    // can't block scrolling.
    document.addEventListener("mousemove", revealGear);
    document.addEventListener("touchstart", revealGear, { passive: true });

    // Panel open/close
    gearEl.addEventListener("click", function () {
        panelOpen = !panelOpen;
        panelEl.classList.toggle("open", panelOpen);
        gearEl.classList.add("visible");
    });

    document.getElementById("settings-close").addEventListener("click", function () {
        panelOpen = false;
        panelEl.classList.remove("open");
        if (mouseTimer) clearTimeout(mouseTimer);
        mouseTimer = setTimeout(function () {
            gearEl.classList.remove("visible");
        }, MOUSE_HIDE_DELAY);
    });

    // --- Control builder helpers ---
    function addSection(parent, label) {
        var el = document.createElement("div");
        el.className = "settings-section";
        el.textContent = label;
        parent.appendChild(el);
    }

    function addRow(parent, label, controlEl, tooltip) {
        var row = document.createElement("div");
        row.className = "settings-row";
        if (tooltip) row.title = tooltip;
        var lbl = document.createElement("span");
        lbl.className = "settings-label";
        lbl.textContent = label;
        row.appendChild(lbl);
        row.appendChild(controlEl);
        parent.appendChild(row);
    }

    function createToggle(value, onChange) {
        var el = document.createElement("div");
        el.className = "settings-toggle" + (value ? " on" : "");
        el.addEventListener("click", function () {
            onChange(el.classList.toggle("on"));
        });
        return el;
    }

    function createCounter(min, max, step, value, formatFn, onChange) {
        var wrap = document.createElement("div");
        wrap.className = "settings-counter";
        var minus = document.createElement("button");
        minus.className = "settings-counter-btn";
        minus.textContent = "\u2212";
        var valEl = document.createElement("span");
        valEl.className = "settings-counter-val";
        valEl.textContent = formatFn(value);
        var plus = document.createElement("button");
        plus.className = "settings-counter-btn";
        plus.textContent = "+";
        var current = value;
        minus.addEventListener("click", function () {
            current = Math.max(min, current - step);
            valEl.textContent = formatFn(current);
            onChange(current);
        });
        plus.addEventListener("click", function () {
            current = Math.min(max, current + step);
            valEl.textContent = formatFn(current);
            onChange(current);
        });
        wrap.appendChild(minus);
        wrap.appendChild(valEl);
        wrap.appendChild(plus);
        return wrap;
    }

    // --- Build settings UI ---
    function buildSettingsUI() {
        var body = panelEl.querySelector(".settings-body");
        body.textContent = "";

        // General
        addSection(body, "General");
        addRow(body, "Hide when in Menus", createToggle(CONFIG.hideInMenus, function (v) {
            CONFIG.hideInMenus = v; applySettings();
        }), "Hide the overlay when no session is active.");
        addRow(body, "Font Size", createCounter(14, 56, 2, CONFIG.fontSize,
            function (v) { return v + "px"; },
            function (v) { CONFIG.fontSize = v; applySettings(); }),
            "Root font size in pixels. All elements scale with this value.");
        (function () {
            var btn = document.createElement("button");
            btn.className = "settings-btn";
            btn.textContent = "Reset";
            btn.addEventListener("click", function () {
                CONFIG.towerX = 0;
                CONFIG.towerY = 0;
                applyTowerPosition();
                saveSettings();
            });
            addRow(body, "Position", btn, "Drag the header bar to reposition. Click Reset to return to top-left.");
        })();
        var note = document.createElement("div");
        note.className = "settings-note";
        note.textContent = "Colors and fonts sync from in-game settings.";
        body.appendChild(note);

        // Logos
        addSection(body, "Logos");
        addRow(body, "Slideshow", createToggle(CONFIG.logoSlideshow, function (v) {
            CONFIG.logoSlideshow = v; buildLogoTrack(); saveSettings();
        }), "Show sponsor logos above the standings tower. Drop PNG files into the logos/ folder.");
        addRow(body, "Interval", createCounter(5, 120, 5, CONFIG.logoInterval,
            function (v) { return v + "s"; },
            function (v) {
                CONFIG.logoInterval = v;
                buildLogoTrack();
                saveSettings();
            }),
            "Seconds between logo slides.");
        var logoSettingsContainer = document.createElement("div");
        logoSettingsContainer.id = "logo-settings";
        body.appendChild(logoSettingsContainer);

        // Standings
        // Note: Compact Times is inherited from the in-game HUD (short time format), so it
        // has no control here. The Positions +/- column is fully overlay-controlled (on/off
        // + reference) so it works regardless of the in-game column.
        addSection(body, "Standings");
        addRow(body, "Positions +/-", createToggle(CONFIG.showPosDelta, function (v) {
            CONFIG.showPosDelta = v; applySettings();
        }), "Show the positions gained/lost (+/-) column. Race sessions only.");
        (function () {
            // Order matches the in-game cycle (Sector -> Lap -> Race): the broadest
            // reference (Race, which falls back to the last S/F) sits last.
            var refs = ["split", "sf", "start"];
            var refLabels = { start: "Race", sf: "Lap", split: "Sector" };
            var idx = refs.indexOf(CONFIG.posDeltaRef);
            if (idx < 0) idx = 0;
            addRow(body, "+/- Reference", createCounter(0, refs.length - 1, 1, idx,
                function (v) { return refLabels[refs[v]]; },
                function (v) { CONFIG.posDeltaRef = refs[v]; applySettings(); }),
                "The period the +/- column measures: positions gained/lost since the race start, during the current lap, or in the current sector.");
        })();
        addRow(body, "Hide DNS", createToggle(CONFIG.hideDns, function (v) {
            CONFIG.hideDns = v; applySettings();
        }), "Hide riders who did not start from the standings.");
        addRow(body, "Max Riders", createCounter(0, 40, 1, CONFIG.maxRiders,
            function (v) { return v === 0 ? "All" : String(v); },
            function (v) { CONFIG.maxRiders = v; applySettings(); }),
            "Maximum riders shown in the standings tower. All = no limit.");
        addRow(body, "Name Chars", createCounter(3, 30, 1, CONFIG.nameChars,
            String, function (v) { CONFIG.nameChars = v; applySettings(); }),
            "Characters shown in the name column. Drives the overall tower width.");

        // Bottom-slot panels — broadcast overlays that periodically rise over the
        // bottom of the standings tower. Grouped here with the standings they overlay.

        // Fastest Last Laps
        addSection(body, "Fastest Last Laps");
        addRow(body, "Enabled", createToggle(CONFIG.fastLap, function (v) {
            CONFIG.fastLap = v; applySettings();
        }), "Periodically slide a 'fastest last lap times' panel up over the bottom of the tower. Race sessions only.");
        addRow(body, "Interval", createCounter(10, 300, 5, CONFIG.fastLapInterval,
            function (v) { return v + "s"; },
            function (v) { CONFIG.fastLapInterval = v; applySettings(); }),
            "Seconds between each appearance of the panel.");
        addRow(body, "Duration", createCounter(1, 30, 1, CONFIG.fastLapDuration / 1000,
            function (v) { return v + "s"; },
            function (v) { CONFIG.fastLapDuration = v * 1000; applySettings(); }),
            "How long the panel stays visible each time.");
        addRow(body, "Riders", createCounter(3, 20, 1, CONFIG.fastLapCount,
            String, function (v) { CONFIG.fastLapCount = v; applySettings(); }),
            "How many riders the panel lists, ranked by their last lap.");

        // Fastest Laps (session best)
        addSection(body, "Fastest Laps");
        addRow(body, "Enabled", createToggle(CONFIG.bestLap, function (v) {
            CONFIG.bestLap = v; applySettings();
        }), "Periodically show a leaderboard of each rider's best lap of the session. Race sessions only.");
        addRow(body, "Interval", createCounter(10, 300, 5, CONFIG.bestLapInterval,
            function (v) { return v + "s"; },
            function (v) { CONFIG.bestLapInterval = v; applySettings(); }),
            "Seconds between each appearance of the panel.");
        addRow(body, "Duration", createCounter(1, 30, 1, CONFIG.bestLapDuration / 1000,
            function (v) { return v + "s"; },
            function (v) { CONFIG.bestLapDuration = v * 1000; applySettings(); }),
            "How long the panel stays visible each time.");
        addRow(body, "Riders", createCounter(3, 20, 1, CONFIG.bestLapCount,
            String, function (v) { CONFIG.bestLapCount = v; applySettings(); }),
            "How many riders the panel lists, ranked by their best lap.");

        // Down the Order
        addSection(body, "Down the Order");
        addRow(body, "Enabled", createToggle(CONFIG.tail, function (v) {
            CONFIG.tail = v; applySettings();
        }), "When Max Riders crops the tower, periodically page through the riders hidden below the cutoff. Needs Max Riders set.");
        addRow(body, "Interval", createCounter(10, 300, 5, CONFIG.tailInterval,
            function (v) { return v + "s"; },
            function (v) { CONFIG.tailInterval = v; applySettings(); }),
            "Seconds between each run through the tail of the field.");
        addRow(body, "Page Time", createCounter(1, 15, 1, CONFIG.tailPageHold,
            function (v) { return v + "s"; },
            function (v) { CONFIG.tailPageHold = v; applySettings(); }),
            "Seconds each page of riders is shown before sliding to the next.");
        addRow(body, "Per Page", createCounter(3, 10, 1, CONFIG.tailPageSize,
            String, function (v) { CONFIG.tailPageSize = v; applySettings(); }),
            "How many riders are shown on each page.");

        // Battles
        addSection(body, "Battles");
        addRow(body, "Enabled", createToggle(CONFIG.battle, function (v) {
            CONFIG.battle = v; applySettings();
        }), "Spotlight a group of riders running close together, e.g. 'Battle for 4th'. Race sessions only.");
        addRow(body, "Rest", createCounter(10, 300, 5, CONFIG.battleInterval,
            function (v) { return v + "s"; },
            function (v) { CONFIG.battleInterval = v; applySettings(); }),
            "Rest after the panel has cycled through every active battle once (within a pass they show back-to-back), so a sustained set doesn't stay parked. A battle shows automatically whenever one is happening and the slot is free.");
        addRow(body, "Max Time", createCounter(1, 30, 1, CONFIG.battleDuration / 1000,
            function (v) { return v + "s"; },
            function (v) { CONFIG.battleDuration = v * 1000; applySettings(); }),
            "Longest a battle holds the slot before yielding (it also hides early when the battle breaks up, or when a board takes its turn).");
        addRow(body, "Closeness", createCounter(0.5, 5, 0.5, CONFIG.battleGap,
            function (v) { return v.toFixed(1) + "s"; },
            function (v) { CONFIG.battleGap = v; applySettings(); }),
            "Max gap between two riders for them to count as battling.");
        addRow(body, "Bike", createToggle(CONFIG.battleBike, function (v) {
            CONFIG.battleBike = v; applySettings();
        }), "Add a sub-row showing each battling rider's bike.");
        addRow(body, "Last Lap", createToggle(CONFIG.battleLastLap, function (v) {
            CONFIG.battleLastLap = v; applySettings();
        }), "Add a sub-row showing each battling rider's last lap.");
        addRow(body, "Fastest Lap", createToggle(CONFIG.battleFastLap, function (v) {
            CONFIG.battleFastLap = v; applySettings();
        }), "Add a sub-row showing each battling rider's fastest (session best) lap.");

        // Chips
        addSection(body, "Chips");
        var chipLabels = {
            finished: "Finished", pit: "Pit", penalty: "Penalty",
            fastest: "Fastest", camera: "Camera"
        };
        var chipTips = {
            finished: "Checkered flag icon for riders who finished.",
            pit: "Wrench icon for riders in the pit.",
            penalty: "Penalty time badge on the rider row.",
            fastest: "Stopwatch icon for the fastest lap holder.",
            camera: "Dot icon for the spectated rider."
        };
        for (var ck in CONFIG.chips) {
            (function (key) {
                addRow(body, chipLabels[key] || key, createToggle(CONFIG.chips[key], function (v) {
                    CONFIG.chips[key] = v; applySettings();
                }), chipTips[key]);
            })(ck);
        }

        // Events
        addSection(body, "Events");
        addRow(body, "Max Entries", createCounter(0, 20, 1, CONFIG.maxEvents,
            String, function (v) { CONFIG.maxEvents = v; applySettings(); }),
            "Maximum visible event log entries.");
        (function () {
            var modes = ["off", "session", "clock"];
            var idx = modes.indexOf(CONFIG.timestampMode);
            if (idx < 0) idx = 0;
            addRow(body, "Timestamps", createCounter(0, modes.length - 1, 1, idx,
                function (v) { return modes[v]; },
                function (v) { CONFIG.timestampMode = modes[v]; applySettings(); }),
                "off = hidden, session = elapsed time, clock = wall clock.");
        })();
        var eventLabels = {
            session: "Session", fastestLap: "Fastest Lap", penalty: "Penalty",
            riderOut: "Rider Out", overtime: "Overtime", finalLap: "Final Lap",
            finished: "Finished", leaderChange: "Lead Change", pit: "Pit"
        };
        var eventTips = {
            session: "Session started, ended, and state changes.",
            fastestLap: "New fastest lap set.",
            penalty: "Penalty received, cleared, or changed.",
            riderOut: "Rider retired, DNS, or disqualified.",
            overtime: "Session time expired or overtime started.",
            finalLap: "Leader starts the final lap.",
            finished: "Rider crosses the finish line.",
            leaderChange: "Race lead changes hands.",
            pit: "Rider enters or exits the pit."
        };
        for (var ek in CONFIG.events) {
            (function (key) {
                addRow(body, eventLabels[key] || key, createToggle(CONFIG.events[key], function (v) {
                    CONFIG.events[key] = v; applySettings();
                }), eventTips[key]);
            })(ek);
        }

        // Focus Card
        addSection(body, "Focus Card");
        addRow(body, "Enabled", createToggle(CONFIG.focusCard, function (v) {
            CONFIG.focusCard = v; applySettings();
        }), "Show a detail card for the spectated rider.");
        addRow(body, "Duration", createCounter(0, 30, 1, CONFIG.focusDuration / 1000,
            function (v) { return v === 0 ? "Stay" : v + "s"; },
            function (v) { CONFIG.focusDuration = v * 1000; applySettings(); }),
            "Auto-hide delay. Stay = remain visible until rider changes.");

        // Reset
        var resetBtn = document.createElement("button");
        resetBtn.className = "settings-reset";
        resetBtn.textContent = "Reset to Defaults";
        resetBtn.addEventListener("click", function () {
            localStorage.removeItem(STORAGE_KEY);
            location.reload();
        });
        body.appendChild(resetBtn);
    }

    buildSettingsUI();
    updatePreviewSizing();

    // --- Demo mode ---
    // Append "?demo" (or "#demo") to the URL to replay a synthetic race instead
    // of connecting to the plugin. Feeds the same snapshots into render() that
    // the SSE stream would, so every feature (standings, gaps, position tints,
    // events, focus card, fastest-last-lap panel) can be previewed without the
    // game running. Nothing here touches the live code path.
    function startDemo() {
        demoActive = true;                 // suppress persistence of scaled timings
        appendStatusLine("Demo mode — replaying a sample race", "ok");
        versionAnnounced = true;           // suppress the "Connected" banner
        overlay.classList.remove("disconnected");

        // Compress the bottom-slot timing for the demo by scaling each panel's
        // duration AND interval by the same factor. Scaling both keeps every
        // panel's on-screen-vs-at-rest ratio identical to the live config, so the
        // demo shows the real rest cadence at ~0.4x the live intervals. This is
        // proportional, not "instant": the battle filler (triggerOnEligible)
        // appears as soon as a battle forms, but the boards keep their relative
        // rarity - fastest-last-lap ~58s and session-best ~113s of real time
        // (manage() uses Date.now(), not the demo's SPEED-scaled clock). That's
        // faithful, not stuck. The trade is a panel dwells ~6s here instead of the
        // real 15s. (0.4 lands close to the original pre-15s cadence, since the
        // live values are ~2.5x the old.)
        // In-memory only — not persisted to the user's real settings.
        var DEMO_PANEL_SCALE = 0.4;
        CONFIG.fastLapInterval *= DEMO_PANEL_SCALE; CONFIG.fastLapDuration *= DEMO_PANEL_SCALE;
        CONFIG.bestLapInterval *= DEMO_PANEL_SCALE; CONFIG.bestLapDuration *= DEMO_PANEL_SCALE;
        CONFIG.battleInterval  *= DEMO_PANEL_SCALE; CONFIG.battleDuration  *= DEMO_PANEL_SCALE;
        CONFIG.tailInterval    *= DEMO_PANEL_SCALE; CONFIG.tailPageHold    *= DEMO_PANEL_SCALE;

        var TICK_MS = 250;                 // render cadence (real ms)
        var SPEED = 8;                     // virtual ms elapsed per real ms
        var SESSION_LEN = 12 * 60000;      // 12-minute timed race (virtual)

        // Give one rider a tracked-rider plate (the #1 points leader) so the demo
        // shows the red/white number badge without configuring tracked riders.
        var DEMO_TRACKED_NUM = 1;
        var DEMO_TRACKED_PLATE = "#e10600";   // red plate -> white number (luma < 128)

        // Roster — names/brands evocative of a 450 main event. `pace` is the
        // rider's baseline lap time in ms; the leader has the lowest.
        var roster = [
            { num: 94,  name: "K. Roczen",    bike: "Suzuki RM-Z450",   brand: "Suzuki",     color: "#f4d300", pace: 45300 },
            { num: 96,  name: "H. Lawrence",  bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 45500 },
            { num: 32,  name: "J. Cooper",    bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 46400 },
            { num: 27,  name: "M. Stewart",   bike: "Husqvarna FC450",  brand: "Husqvarna",  color: "#c8c8c8", pace: 46600 },
            { num: 26,  name: "J. Prado",     bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 46900 },
            { num: 1,   name: "C. Webb",      bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 47200 },
            { num: 17,  name: "J. Savatgy",   bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 47800 },
            { num: 14,  name: "D. Ferrandis", bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 48100 },
            { num: 28,  name: "C. Craig",     bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 48400 },
            { num: 15,  name: "D. Wilson",    bike: "GasGas MC450F",    brand: "GasGas",     color: "#d4002a", pace: 48700 },
            { num: 719, name: "V. Friese",    bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 49100 },
            { num: 51,  name: "J. Barcia",    bike: "GasGas MC450F",    brand: "GasGas",     color: "#d4002a", pace: 49300 },
            { num: 21,  name: "J. Anderson",  bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 49500 },
            { num: 7,   name: "A. Plessinger", bike: "KTM 450 SX-F",    brand: "KTM",        color: "#ff6600", pace: 49800 },
            { num: 36,  name: "B. Sexton",    bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 50000 },
            { num: 24,  name: "R. Brown",     bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 50300 },
            { num: 45,  name: "C. Sexton",    bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 50500 },
            { num: 12,  name: "S. Cianciarulo", bike: "Kawasaki KX450", brand: "Kawasaki",   color: "#4caf00", pace: 50800 },
            { num: 38,  name: "H. Deegan",    bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 51000 },
            { num: 75,  name: "B. Hampshire", bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 51300 },
            { num: 48,  name: "M. Vohland",   bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 51600 },
            { num: 92,  name: "A. Cairoli",   bike: "Ducati Desmo450",  brand: "Ducati",     color: "#cc0000", pace: 51900 }
        ];

        var sim;

        function pushEvent(type, message, detail, T) {
            sim.events.push({
                type: type, message: message, detail: detail,
                clockTime: clockNowFull(),
                sessionTime: formatMmSs(T)
            });
            while (sim.events.length > 12) sim.events.shift();
        }

        function resetSim() {
            sim = {
                T: 0, events: [], leaderNum: -1,
                bestOverall: 0, bestOverallNum: -1,
                specIndex: 0, specMs: 0, finished: false, finishLap: -1,
                riders: roster.map(function (r) {
                    return {
                        num: r.num, fullName: r.name, bike: r.bike, brand: r.brand,
                        color: r.color, pace: r.pace, phase: Math.random() * Math.PI * 2,
                        dist: 0, laps: 0, lastLapMs: 0, bestLapMs: 0, gridPos: 0,
                        // Position snapshots for the +/- column: at the last S/F
                        // crossing (Lap reference) and last sector change (Sector ref).
                        posAtSf: 0, posAtSplit: 0, prevSector: 0, crossedSf: false
                    };
                })
            };
            // Grid order = baseline pace (fastest starts up front).
            var grid = sim.riders.slice().sort(function (a, b) { return a.pace - b.pace; });
            for (var i = 0; i < grid.length; i++) {
                grid[i].gridPos = i + 1;
                grid[i].posAtSf = grid[i].posAtSplit = i + 1;  // seed +/- references to the grid
            }
            pushEvent(0, "Race started", "", 0);
        }

        // Lap pace wanders ±1.5% per rider so the order shuffles over time.
        function effPace(r, T) {
            return r.pace * (1 + 0.015 * Math.sin(2 * Math.PI * T / 110000 + r.phase));
        }

        function tick() {
            var dT = TICK_MS * SPEED;
            sim.T += dT;
            var T = sim.T;

            for (var i = 0; i < sim.riders.length; i++) {
                var r = sim.riders[i];
                var p = effPace(r, T);
                var prevLaps = r.laps;
                r.dist += dT / p;
                r.laps = Math.floor(r.dist);
                r.crossedSf = (r.laps > prevLaps && r.laps > 0);
                if (r.crossedSf) {
                    var lt = Math.round(p + (Math.random() * 600 - 300));
                    r.lastLapMs = lt;
                    if (r.bestLapMs === 0 || lt < r.bestLapMs) r.bestLapMs = lt;
                    if (sim.bestOverall === 0 || lt < sim.bestOverall) {
                        sim.bestOverall = lt;
                        sim.bestOverallNum = r.num;
                        pushEvent(4, "Fastest lap — " + r.fullName, formatLapTime(lt), T);
                    }
                }
            }

            // Classification: most distance covered leads.
            var order = sim.riders.slice().sort(function (a, b) { return b.dist - a.dist; });
            var leader = order[0];
            if (sim.leaderNum !== -1 && leader.num !== sim.leaderNum) {
                pushEvent(15, "Lead change", leader.fullName, T);
            }
            sim.leaderNum = leader.num;

            // Rotate the spectated rider every ~12s so the focus card cycles.
            sim.specMs += TICK_MS;
            if (sim.specMs >= 12000) {
                sim.specMs = 0;
                sim.specIndex = (sim.specIndex + 1) % Math.min(6, order.length);
            }
            var specNum = order[Math.min(sim.specIndex, order.length - 1)].num;

            // Update +/- reference snapshots now that classification (order) is known:
            // the Lap reference snaps each rider's position at the S/F line, the Sector
            // reference at each sector change (3 sectors/lap). Race uses the grid.
            var DEMO_SECTORS = 3;
            for (var oi = 0; oi < order.length; oi++) {
                var od = order[oi];
                var pos = oi + 1;
                var sector = Math.floor((od.dist - od.laps) * DEMO_SECTORS);
                if (od.crossedSf) od.posAtSf = pos;
                if (sector !== od.prevSector) { od.posAtSplit = pos; od.prevSector = sector; }
            }

            var standings = [];
            for (var k = 0; k < order.length; k++) {
                var rr = order[k];
                var gd = leader.dist - rr.dist;          // laps behind the leader
                var gLaps = Math.floor(gd + 1e-9);
                var gMs = Math.round((gd - gLaps) * effPace(leader, T));
                var chips = [];
                if (sim.bestOverallNum === rr.num) chips.push("fastest");
                if (rr.num === specNum) chips.push("camera");
                standings.push({
                    pos: k + 1, num: rr.num, name: rr.fullName, fullName: rr.fullName,
                    bike: rr.bike, brand: rr.brand, brandColor: rr.color,
                    plateColor: (rr.num === DEMO_TRACKED_NUM ? DEMO_TRACKED_PLATE : undefined),
                    gap: "", gapMs: (k === 0 ? 0 : gMs), gapLaps: (k === 0 ? 0 : gLaps),
                    state: 0, numLaps: rr.laps, inPit: false, penalty: 0,
                    bestLapMs: rr.bestLapMs, lastLapMs: rr.lastLapMs, finished: false,
                    posDeltaStart: rr.gridPos - (k + 1),
                    posDeltaSf: rr.posAtSf - (k + 1),
                    posDeltaSplit: rr.posAtSplit - (k + 1),
                    chips: chips
                });
            }

            // Time + 2 bonus laps. Until the clock expires, count it down; after
            // that, mirror the plugin's overtime label off the leader's laps so the
            // demo previews the "N TO GO" -> "FINAL LAP" -> "CHECKERED" sequence.
            // The clock holds at 00:00 while the leader finishes the lap in progress
            // at expiry (toGo > DEMO_BONUS); the countdown only starts once they
            // cross S/F into the bonus laps — same as the plugin.
            var DEMO_BONUS = 2;
            var remaining = Math.max(0, SESSION_LEN - T);
            var clock = formatMmSs(remaining);
            if (remaining <= 0) {
                if (sim.finishLap < 0) sim.finishLap = leader.laps + DEMO_BONUS;
                var toGo = sim.finishLap - leader.laps + 1;   // 1 = final lap
                if (leader.laps > sim.finishLap) clock = "CHECKERED";
                else if (toGo <= DEMO_BONUS) clock = (toGo <= 1) ? "FINAL LAP" : (toGo + " TO GO");
                // else toGo > DEMO_BONUS: in-progress lap — leave clock at "00:00"
            }
            render({
                session: {
                    time: clock, timeMs: remaining,
                    type: "Race", state: "In Progress", format: "12:00 + 2L",
                    numLaps: DEMO_BONUS, sessionLength: SESSION_LEN, isRace: true,
                    isSpectating: true, trackName: "Demo National", trackLength: 1600,
                    leaderLap: leader.laps, compactTimes: true, pluginVersion: "demo"
                },
                standings: standings,
                events: sim.events.slice()
            });

            // Once the leader takes the checkered, hold it briefly then restart.
            if (remaining <= 0 && sim.finishLap >= 0 && leader.laps > sim.finishLap && !sim.finished) {
                sim.finished = true;
                setTimeout(resetSim, 6000);
            }
        }

        resetSim();
        setInterval(tick, TICK_MS);
    }

    // --- Initialize ---
    if (/[?&#]demo\b/i.test(location.search + location.hash)) {
        startDemo();
    } else {
        overlay.classList.add("disconnected");
        connect();
    }
})();
