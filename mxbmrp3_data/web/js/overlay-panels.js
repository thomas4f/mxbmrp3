// ============================================================================
// MXBMRP3 Web Overlay — Bottom-slot panels: lap boards, down-the-order, battle, sectors
// Part 08/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

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

// One shared "No data" placeholder ROW for any bottom-slot panel forced before its
// data exists: a muted, flush-left row in the normal board font. EVERY forced-empty
// panel — lap boards, down-the-order, best-sectors, session-charts — renders THIS row,
// so a caster gets identical confirmation the hotkey fired whatever the panel was.
function slotEmptyRow(rh) {
    var row = createBoardRow();
    row.classList.add("board-empty");   // flush-left, muted (see style.css)
    row.style.height = rh + "px";
    setText(row.children[0], "");
    setText(row.children[1], "No data");
    setText(row.children[2], "");
    return row;
}

// Empty-board placeholder for a forced lap board with no data yet: its title +
// the shared "No data" row. Returns the covered row count (title + placeholder),
// matching the build() convention.
function renderLapBoardEmpty(listEl, titleEl) {
    var rh = measureRowHeight();
    titleEl.style.height = rh + "px";
    listEl.textContent = "";
    listEl.appendChild(slotEmptyRow(rh));
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

// Event-key helper for the data-driven boards: a "num:ms" signature of the field's
// current leader (fastest = min ms). It changes exactly when a new best is posted, so
// a board keyed on it shows the moment its headline metric improves. null = no data.
function fastestOf(standings, field) {
    if (!standings) return null;
    var bestMs = 0, bestNum = -1;
    for (var i = 0; i < standings.length; i++) {
        var ms = standings[i][field];
        if (ms > 0 && (bestMs === 0 || ms < bestMs)) { bestMs = ms; bestNum = standings[i].num; }
    }
    return bestMs === 0 ? null : (bestNum + ":" + bestMs);
}

// --- Fastest Last Lap Times (most recent lap) ---
// Race-only. Lists riders by their most recent completed lap; the live list
// is kept fresh while visible as new laps come in.
function renderFastLapList(standings) {
    var cap = Math.max(1, visibleTowerRows(standings) - 1);
    return renderLapBoard(fastLapList, fastLapTitle, standings,
        function (r) { return r.lastLapMs; }, Math.min(CONFIG.slotRows, cap));
}
createSlotPanel({
    panel: fastLapPanel, name: "fastlap",
    enabled: function () { return CONFIG.fastLap; },
    eligible: function (s) { return !!(s && s.isRace); },
    eventKey: function () { return fastestOf(lastData && lastData.standings, "lastLapMs"); },  // new fastest recent lap
    build: function () { var n = renderFastLapList(lastData.standings); return n ? n + 1 : 0; },
    refresh: function () { if (!renderFastLapList(lastData.standings)) renderLapBoardEmpty(fastLapList, fastLapTitle); },
    autoHide: function () { return CONFIG.slotDuration * 1000; },
    showEmptyWhenForced: true,   // forced before any laps: show a "No data" board
    renderEmpty: function () { return renderLapBoardEmpty(fastLapList, fastLapTitle); }
});

// --- Down the Order (tail) scroller ---
// When Max Riders crops the tower, show the riders hidden below the cutoff as one
// vertical list and scroll through it: slide up -> pause -> scroll down -> pause ->
// scroll up -> pause -> slide down. The tail snapshot is captured at show time and
// held for the pass (it doesn't reshuffle mid-scroll); the sequence self-terminates,
// so the panel uses no auto-hide.
var tailTimer = null;          // phase timer for the scroll sequence
var tailScrollMax = 0;         // px the list can scroll (0 = everything fits, no scroll)

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

// Build the single vertical list of ALL tail riders. The viewport shows up to slotRows
// rows; the track is as tall as the whole list and scrolls inside it. Returns the
// number of rows the VIEWPORT shows (<= slotRows) so the caller can size the panel, and
// sets tailScrollMax to how far the list can scroll (0 = everything already fits).
function buildTailList(standings, session) {
    var tail = getTailRiders(standings);
    if (!tail.length) { tailScrollMax = 0; return 0; }
    var rh = measureRowHeight();
    // Cap to the visible tower height (like the lap boards) so a large slotRows
    // can't grow the panel up over the header/clock.
    var cap = Math.max(1, visibleTowerRows(standings) - 1);
    var visibleRows = Math.min(CONFIG.slotRows, cap, tail.length);
    tailTitle.style.height = rh + "px";
    tailViewport.style.height = (visibleRows * rh) + "px";

    tailTrack.textContent = "";
    for (var k = 0; k < tail.length; k++) {
        var row = createGridRow("tail-row");
        row.style.height = rh + "px";
        setGridRow(row, "tail-row", tail[k], computeGap(tail[k], session));
        tailTrack.appendChild(row);
    }
    // Whole hidden range in the title, e.g. "Positions 11–30".
    var first = tail[0].pos, last = tail[tail.length - 1].pos;
    setText(tailTitle, "Positions " + (first === last ? first : (first + "–" + last)));

    tailScrollMax = (tail.length - visibleRows) * rh;   // 0 if everything fits
    return visibleRows;
}

function clearTailTimer() { if (tailTimer) { clearTimeout(tailTimer); tailTimer = null; } }

function tailScrollTo(px, durMs) {
    tailTrack.style.transitionTimingFunction = "linear";   // constant scroll speed
    tailTrack.style.transitionDuration = durMs + "ms";
    tailTrack.style.transform = "translateY(-" + px + "px)";
}

// The scroll pass (the slot's slide-up/down bracket it): pause at the top, scroll down
// through the list, pause at the bottom, scroll back up, pause, then hide. Five equal
// phases split the shared Panel Time. If everything already fits (no scroll), just hold
// the list for Panel Time, then hide.
function runTailSequence() {
    var total = CONFIG.slotDuration * 1000;
    if (tailScrollMax <= 0) {
        tailTimer = setTimeout(function () { tailCtrl.hide(true); }, total);   // natural end -> arm rest
        return;
    }
    var phase = total / 5;
    var steps = [
        null,                                                // pause at top
        function () { tailScrollTo(tailScrollMax, phase); }, // scroll down
        null,                                                // pause at bottom
        function () { tailScrollTo(0, phase); },             // scroll back up
        null                                                 // pause at top
    ];
    var i = 0;
    (function next() {
        if (i >= steps.length) { tailCtrl.hide(true); return; }   // natural end -> arm rest
        if (steps[i]) steps[i]();
        i++;
        tailTimer = setTimeout(next, phase);
    })();
}

// Forced-empty placeholder for down-the-order (no hidden riders): the panel title +
// the shared "No data" row, self-hiding after Panel Time. Its normal show self-
// terminates via the scroll sequence (onShow), but the framework skips onShow for a
// forced-empty, so bound it with its own timer like the other autoHide:0 panels.
function renderTailEmpty() {
    clearTailTimer();
    var rh = measureRowHeight();
    // Reset the title: the normal path (buildTailList) overwrites it with the dynamic
    // "Positions N–M" range, which would otherwise linger over the "No data" row on a
    // later forced-empty show. The other panels' titles are static (lap boards) or
    // rebuilt each time (sectors/charts), so only this one needs the reset.
    setText(tailTitle, "Down the Order");
    tailTitle.style.height = rh + "px";
    tailViewport.style.height = rh + "px";
    tailTrack.style.transition = "none";
    tailTrack.style.transform = "translateY(0)";
    void tailTrack.offsetWidth;
    tailTrack.style.transition = "";
    tailTrack.textContent = "";
    tailTrack.appendChild(slotEmptyRow(rh));
    tailTimer = setTimeout(function () { tailCtrl.hide(true); }, Math.max(3000, CONFIG.slotDuration * 1000));
    return 2;   // title + placeholder
}

var tailCtrl = createSlotPanel({
    panel: tailPanel, name: "tail",
    enabled: function () { return CONFIG.tail; },
    // Any active session where the tower is cropping the field (riders hidden
    // below the Max Riders cutoff).
    eligible: function (s) {
        return !!(s && s.type && lastData && getTailRiders(lastData.standings).length > 0);
    },
    // No cadence and no data event - "down the order" is coverage, not a story, so it
    // never auto-shows. The caster brings it up on demand via the Down-the-order hotkey.
    build: function () {
        var rows = buildTailList(lastData.standings, lastData.session);
        if (!rows) return 0;
        // Reset the scroll to the top without animating (and clear any lingering
        // per-phase duration from a previous showing).
        tailTrack.style.transition = "none";
        tailTrack.style.transform = "translateY(0)";
        void tailTrack.offsetWidth;
        tailTrack.style.transition = "";
        tailTrack.style.transitionDuration = "";
        tailTrack.style.transitionTimingFunction = "";
        return rows + 1;   // title + visible rows
    },
    autoHide: function () { return 0; },   // self-terminating via the scroll sequence
    showEmptyWhenForced: true,   // forced with no hidden riders: show a "No data" board
    renderEmpty: renderTailEmpty,
    onShow: function () { runTailSequence(); },
    onHide: function () { clearTailTimer(); }
});

// --- Battle panel ---
// Spotlights a cluster of riders running nose-to-tail in the bottom slot
// (shared with the fastlap/tail panels, mutually exclusive). Race-only.
// ALWAYS synced to the in-game director: shows ONLY the battle the camera is
// framing, and re-syncs live (refresh) as the director moves between battles.
var BATTLE_MAX_RIDERS = 6;     // cap a single battle's row count

// Battle groups come from the plugin (PluginData::getBattleGroups), so the in-game
// director and this panel share ONE definition and ONE config: the battle gap and
// the max-position cutoff both live in the in-game Director settings. The plugin
// already applies the same-lap rule, the gap threshold and the max-position cutoff;
// here we just hydrate the emitted groups of race numbers into rider objects from
// the current standings, returning an array of rider-arrays (front-first).
// (?demo supplies its own synthetic battles array.)
function getBattles(standings, session) {
    if (!session || !session.isRace || !standings) return [];
    var groups = (lastData && lastData.battles) ? lastData.battles : [];
    if (!groups.length) return [];
    var byNum = {};
    for (var i = 0; i < standings.length; i++) byNum[standings[i].num] = standings[i];
    var out = [];
    for (var g = 0; g < groups.length; g++) {
        var grp = [];
        for (var k = 0; k < groups[g].length; k++) {
            var r = byNum[groups[g][k]];
            if (r) grp.push(r);
        }
        if (grp.length >= 2) out.push(grp);
    }
    return out;
}

// The single battle the in-game director is currently framing: the group that CONTAINS
// the director's subject. Matching anywhere in the group (not just the front) is robust
// to a battle onboard dip, where the subject is redirected to a chasing rider while the
// front is the partner. Null when the director isn't on a battle (solo / off / paused /
// manual). The battle panel is always synced to the camera, so this is the ONLY battle it
// shows — the overlay never surfaces a battle the broadcast camera isn't on, and the panel
// hides when the director leaves battles.
function directorBattle() {
    var dir = lastData && lastData.director;
    if (!dir || !dir.active || dir.subject < 0) return null;
    var battles = getBattles(lastData && lastData.standings, lastData && lastData.session);
    for (var i = 0; i < battles.length; i++) {
        for (var k = 0; k < battles[i].length; k++) {
            if (battles[i][k].num === dir.subject) return battles[i];
        }
    }
    return null;
}

// Small element helper for the battle card.
function battleEl(tag, cls, text) {
    var e = document.createElement(tag);
    e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
}

// Build the battle's headline + a card per battling rider into `track` (a .battle-track
// element). The track is the sliding unit on a battle→battle cut (it translateY-es down/up),
// so BOTH the title and the cards live in it — not directly in #battle-list — and the whole
// panel moves together. Each card is a left column (main identity line + detail sub-rows,
// tinted in the rider's brand colour) plus a large right-hand headline: the rider's position
// (front of the battle) or their interval to the front (everyone else). Rebuilt each show.
// Returns the covered height in row-heights (title row + riders * rowsPerCard), 0 if empty.
function buildBattleCards(track, group) {
    var rh = measureRowHeight();
    track.textContent = "";

    var subs = [];
    if (CONFIG.battleBike)    subs.push("bike");
    if (CONFIG.battleLastLap) subs.push("last");
    if (CONFIG.battleFastLap) subs.push("fast");
    if (CONFIG.battleIdeal)   subs.push("ideal");
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
    if (rows.length === 0) return 0;   // nothing to show — leave the track empty

    // Headline is the FIRST CHILD of the track (the sliding unit), not a static
    // sibling, so a battle→battle cut slides the whole panel — title + rows — down/up
    // together instead of leaving the headline pinned while only the rows move.
    var title = battleEl("div", "battle-title");
    title.style.height = rh + "px";
    var frontPos = group[0].pos;
    setText(title, frontPos === 1 ? "Battle for the Lead" : "Battle for " + ordinal(frontPos));
    track.appendChild(title);

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
                // No label — the bike name speaks for itself (bold, like the focus card).
                sub.appendChild(battleEl("span", "battle-sub-label battle-sub-bike", r.bike || r.brand || "—"));
            } else if (subs[s] === "last") {
                sub.appendChild(battleEl("span", "battle-sub-label", "Last"));
                sub.appendChild(battleEl("span", "battle-sub-val battle-sub-time",
                    r.lastLapMs > 0 ? formatLapTime(r.lastLapMs) : LAP_PLACEHOLDER));
            } else if (subs[s] === "ideal") {
                sub.appendChild(battleEl("span", "battle-sub-label", "Ideal"));
                sub.appendChild(battleEl("span", "battle-sub-val battle-sub-time",
                    r.idealLapMs > 0 ? formatLapTime(r.idealLapMs) : LAP_PLACEHOLDER));
            } else {
                sub.appendChild(battleEl("span", "battle-sub-label", "Best"));
                sub.appendChild(battleEl("span", "battle-sub-val battle-sub-time",
                    r.bestLapMs > 0 ? formatLapTime(r.bestLapMs) : LAP_PLACEHOLDER));
            }
            bodyCol.appendChild(sub);
        }

        // Identity strip at the bottom: <pos> <±spacer> <plate> <name>, with the
        // whole row highlighted in the rider's brand colour. Columns mirror the tower
        // exactly (position number right-aligned in --col-pos-w, then the optional
        // +/- column, then plate w/ brand strip) so position, plate and name line up
        // with the tower; the full name shows and is clipped if it overflows. Plain
        // number (not an ordinal), matching the tower - an ordinal overflows
        // --col-pos-w and shoves the plate/name out of alignment.
        var id = battleEl("div", "battle-main");
        id.style.height = rh + "px";
        if (rgb) id.style.backgroundColor = "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0.4)";
        id.appendChild(battleEl("span", "battle-pos", String(r.pos)));
        id.appendChild(battleEl("span", "battle-posdelta", ""));   // empty +/- spacer, like the tower
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
        // else their interval to the front of the battle — real-time when the
        // overlay live-gaps toggle is on and both riders have a valid live gap,
        // otherwise the official split (a `.live` class marks a live value).
        if (i === 0) {
            card.appendChild(battleEl("div", "battle-value", ordinal(r.pos).toUpperCase()));
        } else {
            var iv = battleInterval(rows[0], r);
            card.appendChild(battleEl("div", "battle-value" + (iv.live ? " live" : ""),
                formatGap(iv.ms)));
        }

        track.appendChild(card);
    }
    return 1 + rows.length * rowsPerCard;   // title row + card bodies covered
}

// The set of rider numbers currently shown in the battle card. A refresh whose new
// group is DISJOINT from this is a genuine cut to a *different* battle (animate it);
// an overlapping group is the same battle updating (front riders trading, a rider
// joining/leaving the pack) — re-render in place, no slide. Reset on hide.
var battleShownNums = null;
var battleSlideSeq = 0;   // bumped per height-ease; guards a stale release from clearing a newer one
function groupNumSet(group) {
    var s = {};
    for (var i = 0; i < group.length; i++) s[group[i].num] = true;
    return s;
}
function isSameBattle(group) {
    if (!battleShownNums) return false;
    for (var i = 0; i < group.length; i++) if (battleShownNums[group[i].num]) return true;
    return false;   // no shared rider -> a different battle
}

// The single in-flow track that holds the current battle's cards. Created on demand.
// There is only ever one track: a cut to a different battle slides the whole panel out
// and rebuilds this same track on the way back in (see the battle refresh / restart()).
function battleTrack() {
    var t = battleList.querySelector(".battle-track");
    if (!t) { t = document.createElement("div"); t.className = "battle-track"; battleList.appendChild(t); }
    return t;
}

// Render the battle card content (title + rows) for a group IN PLACE and record what's
// shown. animateHeight grows/shrinks the box for a committed membership change (a rider
// joining/leaving the SAME battle); otherwise the height snaps — correct for a plain data
// refresh where the row count is unchanged. Returns covered rows (buildBattleCards already
// includes the title row).
function renderBattleContent(group, animateHeight) {
    if (animateHeight) return animateBattleTo(group);
    var n = buildBattleCards(battleTrack(), group);
    if (!n) return 0;
    battleShownNums = groupNumSet(group);
    return n;   // title + battle rows
}

// Animate the battle box to the height it will be with `group`, gap-free in BOTH
// directions (the box is bottom-anchored; #battle-list clips with overflow:hidden, and
// cards are top-anchored so the departing/arriving rider is always the bottom row):
//   * GROW — build the new (taller) content first; the new bottom row sits clipped below
//     the still-short box, then the box eases OPEN and reveals it.
//   * SHRINK — keep the current (taller) content and ease the box CLOSED over it, so the
//     overflow clips the departing bottom row away as it collapses; only once the box has
//     fully shrunk do we swap in the new (shorter) content and drop the row.
// So a leaving row is never yanked mid-collapse (the old "black box" gap) and an arriving
// row is already present as the box opens. Guarded by battleSlideSeq so a superseding
// change's release/swap can't fire against a newer one.
function animateBattleTo(group) {
    var track = battleTrack();
    var oldH = battleList.offsetHeight;
    var oldHTML = track.innerHTML;                   // snapshot to keep through a shrink
    var n = buildBattleCards(track, group);          // build the new content to measure it
    if (!n) { track.innerHTML = oldHTML; return 0; } // nothing to show -> keep the old card
    var newH = battleList.offsetHeight;
    battleShownNums = groupNumSet(group);
    if (newH === oldH) return n;                     // same height -> content swapped, no anim
    var mySeq = ++battleSlideSeq;
    var shrink = newH < oldH;
    if (shrink) track.innerHTML = oldHTML;           // restore old content; collapse clips it
    battleList.style.height = oldH + "px";
    void battleList.offsetHeight;                    // commit the start height, then ease
    battleList.style.height = newH + "px";
    setTimeout(function () {
        if (mySeq !== battleSlideSeq) return;        // superseded by a newer change
        if (shrink) buildBattleCards(track, group);  // fully collapsed now -> drop the row
        battleList.style.height = "";                // release to natural (== newH)
    }, cssTimeMs("--anim-base", 600) + 60);
    return n;
}


// --- Membership hysteresis (a rider entering/leaving the SAME battle) ---
// A rider hovering right at the battle-gap threshold would otherwise flicker the card
// (and its height) in and out every frame. So a membership change is only COMMITTED (and
// its resize animated) after a rest since the last commit; during the rest we hold the
// current membership but keep its data fresh. Cuts to a different battle are NOT rested —
// those are real, deliberate changes.
var battleMemberCommitAt = 0;                        // Date.now() of the last committed change
var BATTLE_MEMBER_REST_MS = 1500;
function battleNowMs() { return Date.now(); }
// True if `group` is exactly the held set (same riders, same count) — a pure data refresh.
function sameBattleMembers(group) {
    if (!battleShownNums) return false;
    var held = 0; for (var k in battleShownNums) held++;
    if (group.length !== held) return false;
    for (var i = 0; i < group.length; i++) if (!battleShownNums[group[i].num]) return false;
    return true;
}
// Build the display list from the currently-HELD rider numbers using the freshest data
// (the director's current group first, then the tower), dropping any held rider that has
// left the field. Keeps them ordered by current position.
function heldBattleRiders(curGroup) {
    var byNum = {};
    for (var i = 0; i < curGroup.length; i++) byNum[curGroup[i].num] = curGroup[i];
    var st = (lastData && lastData.standings) || [];
    for (var j = 0; j < st.length; j++) if (!byNum[st[j].num]) byNum[st[j].num] = st[j];
    var out = [];
    for (var num in battleShownNums) if (byNum[num]) out.push(byNum[num]);
    out.sort(function (a, b) { return (a.pos || 0) - (b.pos || 0); });
    return out.length ? out : curGroup;   // fall back if every held rider vanished
}

var battleCtrl = createSlotPanel({
    panel: battlePanel, name: "battle",
    // Filler panel: priority 0 so the timed boards (priority 1) pre-empt it,
    // and triggerOnEligible so it has no cadence — it slides in the moment the
    // director frames a battle and the slot is free, instead of waiting for a timer.
    priority: 0,
    triggerOnEligible: true,
    enabled: function () { return CONFIG.battle; },
    // Always synced: eligible only while the director is on a battle, so the panel
    // mirrors the camera and never surfaces a battle the broadcast isn't on.
    eligible: function (s) {
        return !!(s && s.isRace && lastData && directorBattle());
    },
    build: function () {
        var group = directorBattle();
        if (!group) return 0;
        battleShownNums = null;          // fresh appearance — no prior battle to diff against
        battleMemberCommitAt = 0;        // allow the first membership change immediately
        return renderBattleContent(group, false);
    },
    // No cap — eligibility hides the panel when the director leaves battles.
    autoHide: function () { return 0; },
    onHide: function () { battleShownNums = null; battleMemberCommitAt = 0; },
    // Track the director as its subject moves. Three cases, kept distinct on purpose:
    //   * A cut to a genuinely DIFFERENT battle (disjoint riders) — slide the panel fully
    //     OUT and a fresh one back IN (restart()), exactly like any other slot panel
    //     changing: one header, the tower revealed as it clears, and the old panel gone
    //     before the new arrives. Never an in-place two-card reel.
    //   * SAME battle, SAME riders (front swap, gap/position updates) — re-render in place,
    //     data only, no height change.
    //   * SAME battle, membership changed (a rider crossed the battle-gap threshold) —
    //     commit + animate the resize, but only after a rest (BATTLE_MEMBER_REST_MS) so a
    //     rider hovering at the edge can't grow/shrink the card every frame; during the
    //     rest we hold the current membership with fresh data.
    // The only hide is the natural end (director leaves battles), owned by manage()'s
    // grace timer — which arms Panel Rest, so the battle honors the inter-panel gap.
    refresh: function () {
        var cur = directorBattle();
        if (!cur) return;   // director left battles — grace timer owns the hide
        if (!isSameBattle(cur)) {
            battleCtrl.restart();   // different battle → clean slide-out then slide-in
            return;
        }
        var rows;
        if (sameBattleMembers(cur)) {
            rows = renderBattleContent(cur, false);
        } else if (battleNowMs() - battleMemberCommitAt < BATTLE_MEMBER_REST_MS) {
            rows = renderBattleContent(heldBattleRiders(cur), false);   // hold membership, fresh data
        } else {
            rows = renderBattleContent(cur, true);                      // commit + animate resize
            battleMemberCommitAt = battleNowMs();
        }
        if (rows) battleCtrl.resize(rows);   // update covered rows for the chip mask
    }
});

// --- Fastest Laps (session best) ---
// Race-only. Ranks riders by their best lap of the session (rank 1 = the
// overall fastest lap). Kept fresh while visible.
function renderBestLapList(standings) {
    var cap = Math.max(1, visibleTowerRows(standings) - 1);
    return renderLapBoard(bestLapList, bestLapTitle, standings,
        function (r) { return r.bestLapMs; }, Math.min(CONFIG.slotRows, cap));
}
createSlotPanel({
    panel: bestLapPanel, name: "bestlap",
    enabled: function () { return CONFIG.bestLap; },
    eligible: function (s) { return !!(s && s.isRace); },
    eventKey: function () { return fastestOf(lastData && lastData.standings, "bestLapMs"); },  // new session-best lap
    build: function () { var n = renderBestLapList(lastData.standings); return n ? n + 1 : 0; },
    refresh: function () { if (!renderBestLapList(lastData.standings)) renderLapBoardEmpty(bestLapList, bestLapTitle); },
    autoHide: function () { return CONFIG.slotDuration * 1000; },
    showEmptyWhenForced: true,   // forced before any laps: show a "No data" board
    renderEmpty: function () { return renderLapBoardEmpty(bestLapList, bestLapTitle); }
});

// --- Best Sectors (non-races only) ---
// A horizontal page carousel: one page per sector, each a ranked mini-board of the
// fastest riders in that sector. The plugin emits, per sector, a ranked rider list
// in lastData.sectors = [{s, riders:[{num, ms}, ...]}]; we hydrate riders from
// standings[] and page one sector at a time (slide out left, next in from the right).
var sectorsPageTimer = null, sectorsPageIndex = 0, sectorsPageCount = 0;
var sectorsPageSize = 0;   // effective rows per page (capped to visible tower height)

// Sectors that have at least one ranked rider present in the current field (cheap,
// DOM-free — used by eligible(), which runs every manage()).
function countSectorPages() {
    if (!lastData || !lastData.sectors || !lastData.standings) return 0;
    var present = {};
    for (var i = 0; i < lastData.standings.length; i++) present[lastData.standings[i].num] = true;
    var n = 0;
    for (var k = 0; k < lastData.sectors.length; k++) {
        var rs = lastData.sectors[k].riders || [];
        for (var j = 0; j < rs.length; j++) {
            if (rs[j].ms > 0 && present[rs[j].num]) { n++; break; }   // this sector has a showable rider
        }
    }
    return n;
}

// Signature of each sector's fastest rider (num:ms) — changes when any best sector
// improves, so the event-driven show fires exactly then. null = no data.
function sectorsEventKey() {
    var secs = lastData && lastData.sectors;
    if (!secs || !secs.length) return null;
    var k = "";
    for (var i = 0; i < secs.length; i++) {
        var rs = secs[i].riders || [];
        if (rs.length) k += secs[i].s + ":" + rs[0].num + ":" + rs[0].ms + "|";
    }
    return k || null;
}

// A carousel page's title row (built into the page as its first child so the header
// slides in with its content, instead of a fixed title that swaps text in place).
// `cls` is the panel's title class (sectors-title / charts-title) so it inherits the
// shared header styling; height is pinned to the measured row so the panel stays an
// exact whole number of rows tall.
function carouselTitleRow(cls, text, rh) {
    var t = document.createElement("div");
    t.className = cls;
    t.style.height = rh + "px";
    t.textContent = text;
    return t;
}

// Build one carousel page per sector, each a title row + a fixed (slotRows) height
// ranked board. Returns the page count.
function buildSectorsPages(standings) {
    var secs = (lastData && lastData.sectors) ? lastData.sectors : [];
    var byNum = {};
    for (var i = 0; i < standings.length; i++) byNum[standings[i].num] = standings[i];
    var pages = [];
    for (var k = 0; k < secs.length; k++) {
        var list = [], rs = secs[k].riders || [];
        for (var j = 0; j < rs.length; j++) {
            var r = byNum[rs[j].num];
            if (r && rs[j].ms > 0) list.push({ rider: r, ms: rs[j].ms });
        }
        if (list.length) pages.push({ s: secs[k].s, riders: list });
    }

    // Cap rows-per-page to the visible tower height (like the lap boards) so a
    // large slotRows can't grow the panel up over the header/clock.
    var pageSize = Math.min(CONFIG.slotRows, Math.max(1, visibleTowerRows(standings) - 1));
    sectorsPageSize = pageSize;
    sectorsTrack.textContent = "";
    var rh = measureRowHeight();
    sectorsViewport.style.height = ((pageSize + 1) * rh) + "px";   // title row + board rows

    for (var p = 0; p < pages.length; p++) {
        var page = document.createElement("div");
        page.className = "sectors-page";
        page.appendChild(carouselTitleRow("sectors-title", "Best Sector " + pages[p].s, rh));
        for (var n = 0; n < pageSize; n++) {
            var entry = pages[p].riders[n];
            var row = createBoardRow();
            row.style.height = rh + "px";
            if (entry) {
                row.classList.remove("board-row-empty");
                setText(row.children[0], String(n + 1));
                setText(row.children[1], (entry.rider.fullName || entry.rider.name || "").substring(0, CONFIG.nameChars));
                setText(row.children[2], formatLapTime(entry.ms));
            } else {
                row.classList.add("board-row-empty");   // padding row (keeps page height)
            }
            page.appendChild(row);
        }
        sectorsTrack.appendChild(page);
    }
    return pages.length;
}

function scheduleSectorsStep() {
    if (sectorsPageTimer) clearTimeout(sectorsPageTimer);
    sectorsPageTimer = setTimeout(sectorsStep, slotPageMs(sectorsPageCount));
}

function sectorsStep() {
    if (sectorsPageIndex + 1 < sectorsPageCount) {
        sectorsPageIndex++;
        sectorsTrack.style.transform = "translateX(-" + (sectorsPageIndex * 100) + "%)";
        scheduleSectorsStep();
    } else {
        sectorsCtrl.hide(true);   // paged through every sector — natural end, arm rest
    }
}

// Forced-empty placeholder for best-sectors (no sector data yet, or forced in a race):
// one carousel page = the title + the shared "No data" row, self-hiding after Panel
// Time (the framework skips onShow/paging for a forced-empty).
function renderSectorsEmpty() {
    if (sectorsPageTimer) { clearTimeout(sectorsPageTimer); sectorsPageTimer = null; }
    var rh = measureRowHeight();
    sectorsPageSize = 1;
    sectorsViewport.style.height = (2 * rh) + "px";   // title + one row
    sectorsTrack.style.transition = "none";
    sectorsTrack.style.transform = "translateX(0)";
    void sectorsTrack.offsetWidth;
    sectorsTrack.style.transition = "";
    sectorsTrack.textContent = "";
    var page = document.createElement("div");
    page.className = "sectors-page";
    page.appendChild(carouselTitleRow("sectors-title", "Best Sectors", rh));
    page.appendChild(slotEmptyRow(rh));
    sectorsTrack.appendChild(page);
    sectorsPageTimer = setTimeout(function () { sectorsCtrl.hide(true); }, Math.max(3000, CONFIG.slotDuration * 1000));
    return 2;   // title + placeholder
}

var sectorsCtrl = createSlotPanel({
    panel: sectorsPanel, name: "sectors",
    enabled: function () { return CONFIG.sectors; },
    // Non-race sessions only (practice / qualifying / warmup) - matches the in-game
    // fastest-sectors story; in a race, position battles own the bottom slot. Needs
    // at least one sector with a shown rider.
    eligible: function (s) { return !!(s && s.type && !s.isRace && countSectorPages() > 0); },
    eventKey: sectorsEventKey,   // fires on a new best sector
    build: function () {
        var pages = buildSectorsPages(lastData.standings);
        if (!pages) return 0;
        sectorsPageCount = pages;
        sectorsPageIndex = 0;
        // Reset the carousel to the first sector without animating.
        sectorsTrack.style.transition = "none";
        sectorsTrack.style.transform = "translateX(0)";
        void sectorsTrack.offsetWidth;
        sectorsTrack.style.transition = "";
        return sectorsPageSize + 1;   // title + page rows (page size capped to tower height)
    },
    autoHide: function () { return 0; },   // self-terminating via paging
    showEmptyWhenForced: true,   // forced with no sector data: show a "No data" board
    renderEmpty: renderSectorsEmpty,
    onShow: function () { scheduleSectorsStep(); },
    onHide: function () { if (sectorsPageTimer) { clearTimeout(sectorsPageTimer); sectorsPageTimer = null; } }
});

// (The Session Charts panel lives in overlay-charts.js, the next file in the
// load order — its math, SVG rendering, and carousel are one cohesive unit.)
