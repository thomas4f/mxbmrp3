// ============================================================================
// MXBMRP3 Web Overlay — Rider focus card
// Part 06/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

// --- Rider Focus Card ---
// Cache querySelector results (DOM structure is static)
var fc = {
    banner:  focusCard.querySelector(".focus-banner"),
    body:    focusCard.querySelector(".focus-body"),
    bike:    focusCard.querySelector(".focus-bike"),
    lastLap: focusCard.querySelector(".focus-last-lap"),
    bestLap: focusCard.querySelector(".focus-best-lap"),
    ideal:   focusCard.querySelector(".focus-ideal"),
    // Row containers, toggled on/off per CONFIG (like the battle card).
    bikeRow: focusCard.querySelector(".focus-row-bike"),
    lastRow: focusCard.querySelector(".focus-row-last"),
    bestRow: focusCard.querySelector(".focus-row-best"),
    idealRow: focusCard.querySelector(".focus-row-ideal"),
    ident:   focusCard.querySelector(".focus-ident"),
    value:   focusCard.querySelector(".focus-value"),
    num:     focusCard.querySelector(".focus-num"),
    strip:   focusCard.querySelector(".focus-strip"),
    name:    focusCard.querySelector(".focus-name")
};

// Positions gained (+) / lost (-) for a rider vs the caster's chosen +/-
// reference (race start / last S/F / last split) — the same value the ± column
// renders. Returns null when that reference doesn't exist yet for this rider.
function refPosDelta(rider) {
    var d = CONFIG.posDeltaRef === "sf" ? rider.posDeltaSf
          : CONFIG.posDeltaRef === "split" ? rider.posDeltaSplit
          : rider.posDeltaStart;
    return (typeof d === "number") ? d : null;
}

// The director's reason for being on this rider, as a broadcast caption + palette
// colour role - mirrors the (removed) in-game badge "why", driven by the snapshot's
// director block. Returns null when the director isn't actively directing THIS rider
// (manual / paused / off, or a different subject), so the card stays a plain who+timing
// card and never shows a stale story.
function directorCaption(rider) {
    var dir = lastData && lastData.director;
    var directing = !!(dir && dir.active && dir.subject === Number(rider.num));
    if (directing) {
        var withNum = (typeof dir.with === "number") ? dir.with : -1;
        switch (dir.shot) {
            // "battle" is intentionally absent: the focus card is suppressed
            // for battle shots (renderFocusCard bails early) so it never
            // duplicates the battle cards. Position is never repeated in the
            // caption (it's in the identity row). Returns a plain string — the
            // caption is a single neutral colour (styled in CSS), not tinted.
            case "overtake": {   // positions gained
                // Prefer the caster's +/- reference so the caption matches the ±
                // column (e.g. "gained this lap" / "…since the start"); fall back to
                // the plugin's single-move count when that reference is missing or
                // the rider is net-down for it (a pass amid an otherwise bad lap).
                var g = (typeof dir.gained === "number") ? dir.gained : -1;
                var rd = refPosDelta(rider);
                var n = (rd !== null && rd > 0) ? rd : g;
                return n > 0 ? "GAINED " + n + (n === 1 ? " POSITION" : " POSITIONS") : "OVERTAKE";
            }
            case "incident": return "INCIDENT";
            case "fastest":  return "FASTEST LAP";
            case "pace": {   // up on a session-best individual sector (S1, S2 or S3)
                var ps = (typeof dir.paceSplit === "number") ? dir.paceSplit : -1;
                return ["FASTEST S1", "FASTEST S2", "FASTEST S3"][ps] || "FASTEST SECTOR";
            }
            case "drop": {   // positions lost
                // Same as overtake, mirrored: prefer the +/- reference (a negative
                // delta = places lost), fall back to the plugin's window count when
                // the reference is missing or the rider is net-up for it.
                var l = (typeof dir.lost === "number") ? dir.lost : -1;
                var rd = refPosDelta(rider);
                var n = (rd !== null && rd < 0) ? -rd : l;
                return n > 0 ? "DROPPED " + n + (n === 1 ? " POSITION" : " POSITIONS") : "DROPPING";
            }
            case "lapper":   return withNum >= 0 ? "LAPPING #" + withNum : "LAPPING";
            case "finallap": return "FINAL LAP";
            case "finish":   // run-in to the flag: the director locks on before the
                             // rider crosses, so read "FINISHING" until they've
                             // actually finished (then "FINISHED"), never a bare
                             // "FINISH" that looks like it already happened.
                return rider.finished ? "FINISHED" : "FINISHING";
            default: break;  // "solo": routine follow → fall through to status
        }
    }
    // No specific story: show the leader status if this is P1, otherwise a
    // neutral label reflecting whether the auto-director is driving the shot
    // (FOLLOWING) or it's a manual/paused/off spectate (SPECTATING). In a
    // non-race the rank is by best lap, so P1 there is the fastest-lap holder -
    // but only once they've actually set a lap; before any time is on the board
    // the tower is still ordered by grid/entry, so P1 isn't a "fastest lap".
    var isRace = !!(lastData && lastData.session && lastData.session.isRace);
    var neutral = directing ? "FOLLOWING" : "SPECTATING";
    if (rider.pos === 1) {
        if (isRace) return "LEADER";
        return (rider.bestLapMs > 0) ? "FASTEST LAP" : neutral;
    }
    return neutral;
}

function ordinal(n) {
    return n + ordinalSuffix(n);
}

function ordinalSuffix(n) {
    var s = ["th", "st", "nd", "rd"];
    var v = n % 100;
    return s[(v - 20) % 10] || s[v] || s[0];
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
    // Defensive: if the focus-card DOM is missing or stale (e.g. the service
    // worker served a cached old index.html after a JS-only upgrade, so the
    // new markup isn't present), bail. One missing element must never throw
    // and abort the whole overlay render.
    if (!fc.body) {
        if (focusVisible) hideFocusCard();
        return;
    }
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

    // Show the card for whoever is on camera (the spectated rider) — whether the
    // auto-director put us there OR you switched manually — so it always tracks the
    // on-screen rider, like the tower strip does. directorCaption() reads a story
    // caption while the director is actively directing THIS rider, otherwise a
    // generic LEADER / FASTEST LAP / SPECTATING label.
    var dir = lastData && lastData.director;
    var directingThis = !!(dir && dir.active && dir.subject === Number(rider.num));

    // Suppress the card only for the director's BATTLE shot: the battle cards already
    // show the contested pair, so a single-rider card would duplicate them. A manual
    // spectate of a rider who happens to be battling still shows the card. Reset the
    // last-seen sentinel so that when the battle ends — even on the SAME rider
    // (battle -> solo is a common transition) — the next frame reads as a rider
    // change and re-shows the card.
    if (directingThis && dir.shot === "battle") {
        focusLastSeenNum = -1;
        if (focusVisible) hideFocusCard();
        return;
    }

    var isRiderChange = (rider.num !== focusLastSeenNum);
    focusLastSeenNum = rider.num;

    // Only show/re-show when the spectated rider actually changes
    if (isRiderChange) {
        dlog("spectated rider changed: #" + rider.num, rider.name || "");
        showFocusCard();
        resetFocusTimer();
    }

    // Update content silently (gaps/laps change even if rider doesn't)
    if (!focusVisible) return;

    // Detail rows: bike, last lap, best lap, ideal lap (same fields/order as
    // buildBattle). Each row is toggleable, like the battle card; Ideal is off by
    // default. Toggle the row container, then fill the value on the visible ones.
    fc.bikeRow.style.display = CONFIG.focusBike ? "" : "none";
    fc.lastRow.style.display = CONFIG.focusLastLap ? "" : "none";
    fc.bestRow.style.display = CONFIG.focusBestLap ? "" : "none";
    fc.idealRow.style.display = CONFIG.focusIdeal ? "" : "none";
    setText(fc.bike, rider.bike || rider.brand || "—");
    setText(fc.lastLap, rider.lastLapMs > 0 ? formatLapTime(rider.lastLapMs) : LAP_PLACEHOLDER);
    setText(fc.bestLap, rider.bestLapMs > 0 ? formatLapTime(rider.bestLapMs) : LAP_PLACEHOLDER);
    setText(fc.ideal, rider.idealLapMs > 0 ? formatLapTime(rider.idealLapMs) : LAP_PLACEHOLDER);

    // Position: the large ordinal headline on the middle-right, exactly like a
    // battle card's front-rider headline (ordinal, uppercased). The identity row
    // below carries just the plate + name. The whole-row brand highlight and the
    // left-weighted card gradient are set inline like buildBattle().
    setText(fc.value, ordinal(rider.pos).toUpperCase());
    setText(fc.num, String(rider.num));
    applyPlateColor(fc.num, rider);
    fc.strip.style.background = rider.brandColor || "transparent";
    setText(fc.name, rider.fullName || rider.name || "");
    var rgb = hexToRgb(rider.brandColor || "");
    fc.ident.style.backgroundColor = rgb
        ? "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0.4)" : "";
    fc.body.style.backgroundImage = rgb
        ? "linear-gradient(90deg, rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] +
          ",0.32) 0%, rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + ",0) 70%)"
        : "";

    // Director story caption: a plain lower-third. Always shows a label —
    // an event (INCIDENT, FASTEST LAP, …), LEADER, or a neutral
    // FOLLOWING/SPECTATING — in a single neutral colour (styled in CSS, no
    // per-story tint). The else-branch is defensive (caption is never null).
    var cap = directorCaption(rider);
    if (cap) {
        setText(fc.banner, cap);
        fc.banner.hidden = false;
    } else {
        fc.banner.hidden = true;
    }

    // Align the detail rows (bike / last / best) exactly under the name. The
    // CSS indent is only a nominal token formula; the identity row's real
    // columns render wider than their tokens (the ordinal "1ST"/"10TH"
    // overflows --col-pos-w, the plate widens with digit count), so measure
    // the name's actual left edge and indent the sub-rows to match. Using the
    // left-edge DIFFERENCE is transform-invariant — the card's translate shifts
    // name and sub-row equally, so it cancels even mid slide-in. Measure against
    // the identity row (always present) rather than a sub-row, since any given
    // sub-row may be toggled off — the sub-rows and the identity row share the
    // same left edge (both full-width children of .focus-body).
    var indent = fc.name.getBoundingClientRect().left - fc.ident.getBoundingClientRect().left;
    if (indent > 0) fc.body.style.setProperty("--focus-sub-indent", indent.toFixed(1) + "px");
}

