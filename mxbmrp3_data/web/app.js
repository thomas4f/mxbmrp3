// ============================================================================
// MXBMRP3 Web Overlay - Client Application
// Connects to the plugin's HTTP server via Server-Sent Events (SSE).
// Customize freely - the plugin serves these files from disk.
// ============================================================================

(function () {
    "use strict";

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
        compactTimes: false,     // Compact format: drop leading 0:, tenths for gaps

        // --- Tower ---
        hideDns: false,          // Hide DNS riders from standings
        maxRiders: 0,            // Max visible standings rows (0 = show all)
        towerX: 0,               // Tower position X in pixels
        towerY: 0,               // Tower position Y in pixels

        // --- Visibility ---
        hideInMenus: false,      // Hide overlay when no session is active

        // --- Focus card ---
        focusCard: true,         // Show/hide the rider focus card
        focusDuration: 8000      // Auto-hide delay in ms (0 = stay visible)
    };

    // =========================================================================
    // END OF SETTINGS — code below connects to the plugin and renders data.
    // =========================================================================

    // --- Settings persistence (localStorage) ---
    var STORAGE_KEY = "mxbmrp3_settings";

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

    function loadSettings() {
        try {
            var stored = JSON.parse(localStorage.getItem(STORAGE_KEY));
            if (stored) {
                var merged = deepMerge(CONFIG, stored);
                for (var key in merged) {
                    if (merged.hasOwnProperty(key)) CONFIG[key] = merged[key];
                }
            }
        } catch (e) { /* corrupted localStorage, use defaults */ }
        // Clamp numeric values to valid ranges
        CONFIG.fontSize = clamp(CONFIG.fontSize, 14, 56);
        CONFIG.maxRiders = clamp(CONFIG.maxRiders, 0, 40);
        CONFIG.towerX = Math.max(0, CONFIG.towerX);
        CONFIG.towerY = Math.max(0, CONFIG.towerY);
        CONFIG.maxEvents = clamp(CONFIG.maxEvents, 0, 20);
        CONFIG.focusDuration = clamp(CONFIG.focusDuration, 0, 30000);
        if (["off", "session", "clock"].indexOf(CONFIG.timestampMode) < 0) {
            CONFIG.timestampMode = "clock";
        }
    }

    function saveSettings() {
        var toSave = {
            fontSize: CONFIG.fontSize,
            compactTimes: CONFIG.compactTimes,
            hideDns: CONFIG.hideDns,
            maxRiders: CONFIG.maxRiders,
            towerX: CONFIG.towerX,
            towerY: CONFIG.towerY,
            maxEvents: CONFIG.maxEvents,
            timestampMode: CONFIG.timestampMode,
            events: {},
            chips: {},
            hideInMenus: CONFIG.hideInMenus,
            focusCard: CONFIG.focusCard,
            focusDuration: CONFIG.focusDuration
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

    // Apply configured font size
    document.documentElement.style.fontSize = CONFIG.fontSize + "px";

    // --- DOM References ---
    var overlay = document.getElementById("overlay");
    var sessionTime = document.getElementById("session-time");
    var sessionType = document.getElementById("session-type");
    var sessionInfo = document.getElementById("session-info");
    var standingsBody = document.getElementById("standings-body");
    var headerGap = document.getElementById("header-gap");
    var eventLog = document.getElementById("event-log");
    var focusCard = document.getElementById("focus-card");
    var focusAhead = document.getElementById("focus-ahead");
    var focusMain = document.getElementById("focus-main");
    var focusBehind = document.getElementById("focus-behind");

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

    // Client-side timer interpolation (ticks between server updates)
    var lastTimeMsFromServer = 0;  // session.timeMs from last update
    var lastUpdateLocalMs = 0;     // Date.now() when we received it
    var timerCountsDown = true;    // true for timed sessions, false for lap-only
    var timerInterval = null;

    // --- Status messages via event log ---
    function clockNow() {
        var d = new Date();
        var h = d.getHours(), m = d.getMinutes();
        return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m;
    }

    function trimEventLog() {
        while (eventLog.children.length > CONFIG.maxEvents) {
            eventLog.removeChild(eventLog.firstChild);
        }
    }

    // status: "info" (default), "ok", "error"
    function appendStatusLine(message, status) {
        var div = createEventEntry();
        div.children[0].textContent = clockNow();
        div.children[1].textContent = message;
        if (status === "ok") div.children[1].style.color = "var(--green)";
        else if (status === "error") div.children[1].style.color = "var(--red)";
        eventLog.appendChild(div);
        trimEventLog();
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
            appendStatusLine("Connecting to " + CONFIG.host + ":" + CONFIG.port);
        }

        // Probe server availability before opening SSE
        fetch(STATE_URL, { mode: "cors" }).then(function () {
            serverReachable = true;
            openEventSource();
        }).catch(function () {
            serverReachable = false;
            overlay.classList.add("disconnected");
            logError("Failed. Web server enabled?");
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
            appendStatusLine("Connected", "ok");
            if (!timerInterval) {
                timerInterval = setInterval(tickTimer, 500);
            }
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
                logError("Connection lost");
                if (timerInterval) {
                    clearInterval(timerInterval);
                    timerInterval = null;
                }
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

    function tickTimer() {
        if (!isConnected || lastUpdateLocalMs === 0) return;
        var elapsed = Date.now() - lastUpdateLocalMs;
        // Timed sessions count down, lap-only sessions count up
        var currentMs = timerCountsDown
            ? lastTimeMsFromServer - elapsed
            : lastTimeMsFromServer + elapsed;
        setText(sessionTime, formatMmSs(currentMs));
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
    function render(data) {
        // Sync timer baseline from server
        if (data.session && typeof data.session.timeMs === "number") {
            lastTimeMsFromServer = data.session.timeMs;
            lastUpdateLocalMs = Date.now();
            // sessionLength > 0 means timed session (counts down)
            timerCountsDown = data.session.sessionLength > 0;
            // No active session — stop interpolation so timer shows --:--
            if (!data.session.type) lastUpdateLocalMs = 0;
        }

        if (data.session) {
            applyPalette(data.session.palette);
            applyFonts(data.session.fonts);
        }
        // Hide overlay when idle (no active session) if configured
        var idle = !data.session || !data.session.type;
        overlay.classList.toggle("idle", idle && CONFIG.hideInMenus);

        renderHeader(data.session);
        renderStandings(data.standings, data.session);
        renderEventLog(data.events);
        renderFocusCard(data.standings, data.session);
    }

    function renderHeader(session) {
        if (!session) return;

        setText(sessionTime, session.time || "--:--");
        setText(sessionType, session.type || "");

        // Info line (below session type, right-aligned):
        //   Race (time+lap or timed): "Lap X"
        //   Race (lap-only):          "Lap X/Y"
        //   Non-race:                 state (e.g. "In Progress")
        var info = "";
        if (session.isRace) {
            if (session.numLaps > 0 && session.sessionLength <= 0 && session.leaderLap >= 0) {
                // Pure lap race: show Lap X/Y
                info = "Lap " + Math.min(session.leaderLap + 1, session.numLaps) + "/" + session.numLaps;
            } else if (session.leaderLap >= 0) {
                // Timed or time+lap race: show Lap X only
                info = "Lap " + (session.leaderLap + 1);
            }
        } else {
            info = session.state || "";
        }
        setText(sessionInfo, info);
        setText(headerGap, session.isRace ? "GAP" : "BEST");
    }

    // Row structure:
    //   div.standings-row
    //     div.row-main            [0] - has background
    //       span.col-pos            [0][0]
    //       span.col-num            [0][1]
    //         span.num-badge
    //         span.brand-strip
    //       span.col-name           [0][2]
    //       span.col-gap            [0][3]
    //     div.row-chips            [1] - no background, hangs outside
    function createStandingsRow() {
        var row = document.createElement("div");
        row.className = "standings-row";

        var main = document.createElement("div");
        main.className = "row-main";

        var pos = document.createElement("span");
        pos.className = "col-pos";
        main.appendChild(pos);

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
    // Resolve --row-height to pixels (handles both rem and px values).
    // Re-measured each render since root font-size may change via palette sync.
    var rowHeightProbe = document.createElement("div");
    rowHeightProbe.style.height = "var(--row-height)";
    rowHeightProbe.style.position = "absolute";
    rowHeightProbe.style.visibility = "hidden";

    function measureRowHeight() {
        document.body.appendChild(rowHeightProbe);
        var h = rowHeightProbe.offsetHeight;
        document.body.removeChild(rowHeightProbe);
        return h || 28;
    }

    function renderStandings(standings, session) {
        if (!standings) return;

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

            // Number badge + brand strip
            var numBadge = cols[1].children[0];
            var brandStrip = cols[1].children[1];
            setText(numBadge, String(num));

            var brandColor = rider.brandColor || "";
            if (brandStrip.dataset.color !== brandColor) {
                brandStrip.dataset.color = brandColor;
                brandStrip.style.background = brandColor || "transparent";
            }

            // Name
            setText(cols[2], rider.name || "");

            // Gap — format client-side from raw ms values
            var gap;
            var isRace = session && session.isRace;
            if (rider.state === STATE_DNS || rider.state === STATE_RETIRED || rider.state === STATE_DSQ) {
                gap = rider.gap || "";  // DNS/RET/DSQ — use server label
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
            var gapCls = "col-gap";
            if (gap === "Leader") gapCls += " gap-leader";
            else if (rider.gapLaps > 0) gapCls += " gap-laps";
            setClass(cols[3], gapCls);
            setText(cols[3], gap);

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

    // Map server event type integers to CONFIG.events keys
    // (enum order from event_log_types.h)
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
        if (!events) return;

        // Empty events array = session ended, clear the log
        if (events.length === 0) {
            eventLog.textContent = "";
            return;
        }

        // Filter events client-side, then take the last N
        var filtered = [];
        for (var i = 0; i < events.length; i++) {
            if (isEventEnabled(events[i].type)) filtered.push(events[i]);
        }
        var recentEvents = CONFIG.maxEvents > 0 ? filtered.slice(-CONFIG.maxEvents) : [];
        var entries = eventLog.children;

        while (entries.length > recentEvents.length) {
            eventLog.removeChild(eventLog.lastChild);
        }
        while (entries.length < recentEvents.length) {
            eventLog.appendChild(createEventEntry());
        }

        var tsOff = CONFIG.timestampMode === "off";
        var tsSession = CONFIG.timestampMode === "session";

        for (var i = 0; i < recentEvents.length; i++) {
            var evt = recentEvents[i];
            var div = entries[i];

            // Timestamp based on config
            var ts = "";
            if (tsSession) ts = (evt.sessionTime || "").substring(0, 5);
            else if (!tsOff) ts = (evt.clockTime || "").substring(0, 5);
            setText(div.children[0], ts);
            div.children[0].style.display = tsOff ? "none" : "";
            div.children[1].style.color = "";  // Clear any inline color from status lines
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
        }, 400);
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
        els.strip.style.background = neighbor.brandColor || "transparent";
        setText(els.name, neighbor.fullName || neighbor.name || "");

        // Compute interval relative to spectated rider
        var interval = formatInterval(neighbor.gapMs, spectated.gapMs, neighbor.gapLaps, spectated.gapLaps);
        setText(els.interval, interval);
    }

    // --- Settings Panel ---
    var gearEl = document.getElementById("settings-gear");
    var panelEl = document.getElementById("settings-panel");
    var panelOpen = false;
    var mouseTimer = null;
    var MOUSE_HIDE_DELAY = 3000;

    function applySettings() {
        document.documentElement.style.fontSize = CONFIG.fontSize + "px";
        eventLogRowHeight = 0; // re-measure after font size change
        if (!CONFIG.focusCard && focusVisible) hideFocusCard();
        // Force chip refresh by clearing cached keys
        for (var num in riderRows) {
            riderRows[num].children[1].dataset.key = "";
        }
        updatePreviewSizing();
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
    document.addEventListener("mousemove", function () {
        gearEl.classList.add("visible");
        if (mouseTimer) clearTimeout(mouseTimer);
        mouseTimer = setTimeout(function () {
            if (!panelOpen) gearEl.classList.remove("visible");
        }, MOUSE_HIDE_DELAY);
    });

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

    function createSlider(min, max, step, value, onChange) {
        var el = document.createElement("input");
        el.type = "range";
        el.className = "settings-slider";
        el.min = min; el.max = max; el.step = step; el.value = value;
        el.addEventListener("input", function () { onChange(parseFloat(el.value)); });
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

        // Info note
        var note = document.createElement("div");
        note.className = "settings-note";
        note.textContent = "Colors and fonts sync from in-game settings.";
        body.appendChild(note);

        // General
        addSection(body, "General");
        addRow(body, "Hide when in Menus", createToggle(CONFIG.hideInMenus, function (v) {
            CONFIG.hideInMenus = v; applySettings();
        }), "Hide the overlay when no session is active.");

        // Times
        addSection(body, "Times");
        addRow(body, "Compact", createToggle(CONFIG.compactTimes, function (v) {
            CONFIG.compactTimes = v; applySettings();
        }), "Gaps show tenths instead of milliseconds. Lap times drop leading 0: but keep full precision.");

        // Scale
        addSection(body, "Scale");
        addRow(body, "Font Size", createCounter(14, 56, 2, CONFIG.fontSize,
            function (v) { return v + "px"; },
            function (v) { CONFIG.fontSize = v; applySettings(); }),
            "Root font size in pixels. All elements scale with this value.");

        // Tower
        addSection(body, "Tower");
        addRow(body, "Hide DNS", createToggle(CONFIG.hideDns, function (v) {
            CONFIG.hideDns = v; applySettings();
        }), "Hide riders who did not start from the standings.");
        addRow(body, "Max Riders", createCounter(0, 40, 1, CONFIG.maxRiders,
            function (v) { return v === 0 ? "All" : String(v); },
            function (v) { CONFIG.maxRiders = v; applySettings(); }),
            "Maximum riders shown in the standings tower. All = no limit.");
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

        // Event Log
        addSection(body, "Event Log");
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

        // Events
        addSection(body, "Events");
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

    // --- Initialize ---
    overlay.classList.add("disconnected");
    connect();
})();
