// ============================================================================
// MXBMRP3 Web Overlay — Settings panel UI
// Part 10/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

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
    (function () {
        // Single control combining on/off + reference: Off > Sector > Lap > Race.
        // Off hides the +/- column; the others enable it and pick what the delta is
        // measured against. Order matches the in-game cycle (Sector -> Lap -> Race),
        // the broadest reference (Race, which falls back to the last S/F) last. Backed
        // by the existing showPosDelta + posDeltaRef so render + plugin are unchanged.
        var modes = ["off", "split", "sf", "start"];
        var modeLabels = { off: "Off", split: "Sector", sf: "Lap", start: "Race" };
        var idx = CONFIG.showPosDelta ? modes.indexOf(CONFIG.posDeltaRef) : 0;
        if (idx < 1) idx = CONFIG.showPosDelta ? 1 : 0;   // enabled-but-unknown ref -> Sector
        addRow(body, "Positions +/-", createCounter(0, modes.length - 1, 1, idx,
            function (v) { return modeLabels[modes[v]]; },
            function (v) {
                if (v === 0) { CONFIG.showPosDelta = false; }
                else { CONFIG.showPosDelta = true; CONFIG.posDeltaRef = modes[v]; }
                applySettings();
            }),
            "The positions gained/lost (+/-) column: Off hides it, or measure it over the current Sector, the current Lap, or the whole Race. Race sessions only.");
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

    // --- Broadcast Panels: the bottom-slot overlays that rise over the tower. Two
    // shared knobs (Panel Rows / Panel Time) size + time them all; each panel is then
    // a simple on/off. The Battle spotlight keeps its extra options in its own section
    // below. ---
    addSection(body, "Broadcast Panels");
    addRow(body, "Panel Rows", createCounter(3, 20, 1, CONFIG.slotRows,
        String, function (v) { CONFIG.slotRows = v; applySettings(); }),
        "How many rider rows every slide-up panel lists (last laps, best laps, best sectors, down the order) — and how many rider lines the session charts draw.");
    addRow(body, "Panel Time", createCounter(3, 30, 1, CONFIG.slotDuration,
        function (v) { return v + "s"; }, function (v) { CONFIG.slotDuration = v; applySettings(); }),
        "How long each slide-up panel holds the slot: the best-sectors carousel splits it across its sector pages, and down-the-order across its scroll (pause / down / pause / up / pause). The battle panel ignores it — it stays as long as the camera is on the battle.");
    addRow(body, "Panel Rest", createCounter(0, 30, 1, CONFIG.slotRest,
        function (v) { return v === 0 ? "Off" : v + "s"; }, function (v) { CONFIG.slotRest = v; applySettings(); }),
        "Global breathing gap after any slide-up panel ends before the next one may appear, so they don't run back-to-back. 0 = off. A manual force ignores it; the director hopping between battles doesn't trigger it.");
    addRow(body, "Last Laps", createToggle(CONFIG.fastLap, function (v) {
        CONFIG.fastLap = v; applySettings();
    }), "'Fastest last laps' board — ranks riders by their most recent lap, sliding up when a rider posts a new fastest recent lap. Race sessions only. (Also on a hotkey.)");
    addRow(body, "Best Laps", createToggle(CONFIG.bestLap, function (v) {
        CONFIG.bestLap = v; applySettings();
    }), "'Fastest laps' board — ranks riders by their session-best lap, shown when a new session-best lap is set. Race sessions only. (Also on a hotkey.)");
    addRow(body, "Best Sectors", createToggle(CONFIG.sectors, function (v) {
        CONFIG.sectors = v; applySettings();
    }), "A carousel that pages one sector at a time, each a ranked board of the fastest riders in that sector. Shows when a new best sector is set. Practice/qualifying only. (Also on a hotkey.)");
    addRow(body, "Down the Order", createToggle(CONFIG.tail, function (v) {
        CONFIG.tail = v; applySettings();
    }), "Scroll through the riders hidden below the Max Riders cutoff (down then back up), then hide. Brought up on demand via the Down-the-order hotkey. Needs Max Riders set.");
    addRow(body, "Session Charts", createToggle(CONFIG.charts, function (v) {
        CONFIG.charts = v; applySettings();
    }), "A carousel of race-progression line charts (lap chart, race trace, gap, pace), one chart per page. Auto-shows once when the race leader finishes; also on a hotkey. Race sessions only. Pick which charts below.");
    addRow(body, "Battle", createToggle(CONFIG.battle, function (v) {
        CONFIG.battle = v; applySettings();
    }), "Spotlight a group of riders running close together, e.g. 'Battle for 4th'. Race sessions only. Extra options below.");

    // --- Battle: extra options for the battle spotlight (enable it under Broadcast
    // Panels above). ---
    addSection(body, "Battle");
    addRow(body, "Live Gaps", createToggle(CONFIG.battleLiveGaps, function (v) {
        CONFIG.battleLiveGaps = v; applySettings();
    }), "Show the real-time interval to the front of the battle where available (both riders within the game's live-timing range), instead of the official split-time gap. Falls back to the official gap otherwise.");
    addRow(body, "Bike", createToggle(CONFIG.battleBike, function (v) {
        CONFIG.battleBike = v; applySettings();
    }), "Add a sub-row showing each battling rider's bike.");
    addRow(body, "Last Lap", createToggle(CONFIG.battleLastLap, function (v) {
        CONFIG.battleLastLap = v; applySettings();
    }), "Add a sub-row showing each battling rider's last lap.");
    addRow(body, "Fastest Lap", createToggle(CONFIG.battleFastLap, function (v) {
        CONFIG.battleFastLap = v; applySettings();
    }), "Add a sub-row showing each battling rider's fastest (session best) lap.");
    addRow(body, "Ideal Lap", createToggle(CONFIG.battleIdeal, function (v) {
        CONFIG.battleIdeal = v; applySettings();
    }), "Add a sub-row showing each battling rider's ideal lap (sum of their best sectors).");

    // --- Session Charts: which charts the carousel pages through, and how many
    // rider lines each draws (enable the panel under Broadcast Panels above). ---
    addSection(body, "Session Charts");
    addRow(body, "Lap Chart", createToggle(CONFIG.chartLap, function (v) {
        CONFIG.chartLap = v; applySettings();
    }), "Track position per lap — the classic 'lap chart' showing every overtake as lines crossing.");
    addRow(body, "Race Trace", createToggle(CONFIG.chartTrace, function (v) {
        CONFIG.chartTrace = v; applySettings();
    }), "Cumulative race time vs a fixed reference pace (the leader's average). Riders ahead of the pace rise, those falling back drop. Race sessions only.");
    addRow(body, "Gap Chart", createToggle(CONFIG.chartGap, function (v) {
        CONFIG.chartGap = v; applySettings();
    }), "Seconds behind the leader per lap (in a race) or behind the session-best lap (practice/qualifying).");
    addRow(body, "Pace Chart", createToggle(CONFIG.chartPace, function (v) {
        CONFIG.chartPace = v; applySettings();
    }), "Raw lap time per lap, with the opening lap and outliers filtered out so mistakes and drop-off stand out.");

    // Chips
    addSection(body, "Chips");
    var chipLabels = {
        finished: "Finished", pit: "Pit", penalty: "Penalty",
        fastest: "Fastest"
    };
    var chipTips = {
        finished: "Checkered flag icon for riders who finished.",
        pit: "Wrench icon for riders in the pit.",
        penalty: "Penalty time badge on the rider row.",
        fastest: "Stopwatch icon for the fastest lap holder."
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
        function (v) { return v === 0 ? "Off" : String(v); },
        function (v) { CONFIG.maxEvents = v; applySettings(); }),
        "Maximum visible event log entries. Off = hide the event log.");
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
        finished: "Finished", leaderChange: "Lead Change", pit: "Pit",
        director: "Director"
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
        pit: "Rider enters or exits the pit.",
        director: "Auto-director shot decisions and state changes (spectate/replay)."
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
    // Detail rows, like the battle card. Ideal is off by default.
    addRow(body, "Bike", createToggle(CONFIG.focusBike, function (v) {
        CONFIG.focusBike = v; applySettings();
    }), "Show a row with the rider's bike.");
    addRow(body, "Last Lap", createToggle(CONFIG.focusLastLap, function (v) {
        CONFIG.focusLastLap = v; applySettings();
    }), "Show a row with the rider's last lap.");
    addRow(body, "Fastest Lap", createToggle(CONFIG.focusBestLap, function (v) {
        CONFIG.focusBestLap = v; applySettings();
    }), "Show a row with the rider's fastest (session best) lap.");
    addRow(body, "Ideal Lap", createToggle(CONFIG.focusIdeal, function (v) {
        CONFIG.focusIdeal = v; applySettings();
    }), "Show a row with the rider's ideal lap (sum of their best sectors).");

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
// Append "?demo" (or "#demo") to the URL to replay a synthetic session instead
// of connecting to the plugin. Feeds the same snapshots into render() that
// the SSE stream would, so every feature (standings, gaps, position tints,
// events, focus card, fastest-last-lap panel) can be previewed without the
// game running. Runs a warmup (lap-times view, no +/- column) and then the
