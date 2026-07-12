// ============================================================================
// MXBMRP3 Web Overlay — Main render loop: header, standings tower & event log
// Part 05/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

// --- Rendering ---
// Call with a fresh snapshot from SSE, or with no args to re-render
// the cached snapshot (used by applySettings()).
function render(data) {
    if (data) lastData = data;
    else data = lastData;
    if (!data) return;
    // Demo/test hook: surface the latest snapshot so a Playwright test can wait for
    // real data (e.g. laps) before exercising a panel. Demo-only — never on the live path.
    if (demoActive) window.mxbmrp3LastData = data;
    debugLogBattles(data);
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
        // ONLY the rider we're watching (the on-screen / spectated rider, flagged by
        // the plugin with the "camera" marker) gets the accent bar + tinted background
        // — the single "who's on camera" indicator in the tower. Exactly one row, never
        // the whole battle group: when the director frames a fight the battle PANEL
        // shows the group; the tower strip stays on the tracked rider alone. When the
        // auto-director is driving, that's its subject; on a manual camera it's whoever
        // you're spectating. (This replaces the old camera chip.)
        var isCamera = !!(rider.chips && rider.chips.indexOf("camera") !== -1);
        if (isCamera) rowCls += " camera-row";
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
        var pd = refPosDelta(rider);
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

        // Chips - filter client-side based on CONFIG.chips. "camera" is never a
        // chip icon (the spectated rider is a row highlight); it stays in the data
        // only to identify that rider (camera-row + focus card), so skip it here.
        var chipKey = "";
        if (rider.chips) {
            for (var j = 0; j < rider.chips.length; j++) {
                var chip = rider.chips[j];
                if (chip === "camera" || !CONFIG.chips[chip]) continue;
                chipKey += chip + (chip === "penalty" ? rider.penalty : "") + ";";
            }
        }
        if (chipsEl.dataset.key !== chipKey) {
            chipsEl.dataset.key = chipKey;
            chipsEl.textContent = "";
            if (rider.chips) {
                for (var k = 0; k < rider.chips.length; k++) {
                    var c = rider.chips[k];
                    if (c === "camera" || !CONFIG.chips[c]) continue;
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
    "pit",          // 17 PitExit
    "director"      // 18 DirectorCut
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
            sortMs: sl.ms || 0,
            time: sl.time,
            message: sl.message,
            status: sl.status
        });
    }
    for (var e = 0; e < filtered.length; e++) {
        combined.push({
            isStatus: false,
            sortMs: filtered[e].clockMs || 0,
            evt: filtered[e]
        });
    }
    // Stable chronological sort so status lines and events interleave by
    // wall-clock time. Sort on the numeric epoch-ms key (clockMs / Date.now()),
    // not the HH:MM:SS string, which would sort lexically and invert across midnight.
    combined.sort(function (a, b) {
        return a.sortMs - b.sortMs;
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

