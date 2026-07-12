// ============================================================================
// MXBMRP3 Web Overlay — Bottom-slot broadcast panel framework
// Part 07/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

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
// Latest-wins hand-off: while an outgoing panel slides out before a forced panel
// opens, a NEWER force supersedes the pending one (a manual force cancels whatever
// is active/queued and shows what the caster just triggered). Only pendingForceTarget
// opens when the single shared timer fires.
var pendingForceTarget = null;
var forceTimer = null;

// Global inter-panel rest: after ANY bottom-slot panel ends its turn NATURALLY
// (auto-hide, or a triggered panel losing eligibility), no other panel may start a
// new turn until this timestamp. Gives the tower breathing room so panels don't run
// back-to-back. Set by hide(true); NOT armed by a pre-empt eviction or a battle-to-
// battle hop (those aren't the end of a turn). A manual force bypasses it entirely.
// Configurable via CONFIG.slotRest (0 = off). Deferring (not dropping) is intentional:
// an event that lands mid-rest still shows once the rest passes.
var slotRestUntil = 0;

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
// has nothing to show (e.g. forcing a lap board with no laps yet), leaving the
// current panel untouched. The battle panel is not forceable (always synced).
function forceSlot(name) {
    var target = null;
    for (var i = 0; i < slotPanels.length; i++) {
        if (slotPanels[i].name === name) { target = slotPanels[i]; break; }
    }
    if (!target) { dlog("forceSlot: no panel named", name); return; }
    // Manual force is intentionally gated on NEITHER enabled() nor
    // eligible(): disabling only stops auto-rotation, and a broadcaster can
    // force a panel even outside its usual conditions (e.g. the lap boards
    // during qualifying). Every forceable panel opts into showEmptyWhenForced,
    // so a force before its data exists shows the shared "No data" placeholder
    // (slotEmptyRow) rather than silently no-opping — the caster always sees the
    // hotkey worked, consistently across every panel.
    if (target.masking()) { dlog("forceSlot:", name, "already on screen"); return; }
    dlog("forceSlot: forcing", name, "in");

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
        // re-grab it before the forced panel opens (cleared once it has). Latest
        // force wins: overwrite the pending target and restart the SINGLE shared
        // timer so a second hotkey pressed during the hand-off cancels the first
        // and only the newest panel opens (no stacking two panels).
        pendingForceTarget = target;
        slotForcing = true;
        if (forceTimer) clearTimeout(forceTimer);
        forceTimer = setTimeout(function () {
            forceTimer = null;
            slotForcing = false;   // clear first so a throw in force() can't strand it
            var t = pendingForceTarget;
            pendingForceTarget = null;
            if (t) t.force();
        }, cssTimeMs("--anim-slide", 1000) + 70);
    } else {
        // Slot is clear. A prior force may still be mid-hand-off (its outgoing
        // panel finished sliding out but its timer hasn't fired yet); this newer
        // force supersedes it, so cancel the pending one before opening.
        if (forceTimer) { clearTimeout(forceTimer); forceTimer = null; }
        pendingForceTarget = null;
        slotForcing = false;
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
        dlog("overlayCmd present in snapshot; baseline seq =", seq);
        return;
    }
    if (seq === lastForcedSeq) return;
    lastForcedSeq = seq;
    dlog("overlay force command received: panel =", data.overlayCmd.panel, "seq =", seq);
    if (data.overlayCmd.panel) forceSlot(data.overlayCmd.panel);
}

// Per-page dwell for a self-terminating carousel (best sectors / down the order):
// the shared Panel Time split across its pages, so the whole carousel runs in about
// slotDuration. Floored so a many-page carousel never flickers unreadably (which can
// push a very large one a little past the nominal total).
function slotPageMs(pages) {
    return Math.max(1200, Math.round(CONFIG.slotDuration * 1000 / Math.max(1, pages)));
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
    var ineligibleSince = 0; // when a visible triggerOnEligible panel lost eligibility (0 = eligible)
    var lastEventKey;        // last value of spec.eventKey() (undefined until first seen)
    var inClass = spec.name + "-in", outClass = spec.name + "-out";
    // Don't hide a triggerOnEligible panel (the battle filler) the instant
    // eligibility blips off for one snapshot - wait this long still-ineligible
    // first, so a momentary data gap can't make it dance in and out.
    var ELIGIBLE_HIDE_GRACE_MS = 700;

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
        ineligibleSince = 0;
        dlog("slot show:", spec.name, forced ? "(forced)" : "", "rows", n);
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
            // park in the slot permanently. A natural auto-hide also arms the
            // global inter-panel rest before the next panel's turn.
            if (spec.restAfter) cooldownUntil = Date.now() + spec.restAfter();
            hide(true);
        }, ms);
        return true;
    }

    // Once the slot's current panel has been slid out (a lower panel evicted, or THIS
    // panel restarting), open this one — but only after the slide-out fully finishes and
    // only if nothing higher grabbed the slot meanwhile. `pending` holds off manage()'s
    // show() so the out and the in never overlap (the "half-slide" the battle reel had).
    function openAfterSlideOut() {
        pending = true;
        setTimeout(function () {
            pending = false;
            if (!visible && !slotForcing && activeSlotPriority() < priority) openPanel(false);
        }, cssTimeMs("--anim-slide", 1000) + 70);
    }

    // Returns true if the panel took the slot (or committed to taking it via an
    // evict slide-out), false if it was blocked — a manual force owns the slot, or
    // an equal/higher-priority panel is masking. An event-driven panel that must
    // not lose its one-shot trigger (deferEventWhenBlocked — charts' leader-finish)
    // uses the return value to defer rather than consume the event while blocked.
    function show() {
        if (slotForcing) return false;   // a manual force owns the slot for now
        if (visible || pending || !lastData || !lastData.standings) return false;
        if (activeSlotPriority() >= priority) return false;   // equal/higher panel holds the slot
        if (evictBelow(priority)) {
            openAfterSlideOut();   // a lower filler holds the slot — slide it out, then open
            return true;
        }
        return openPanel(false);
    }

    // Broadcaster force: bypass the priority gate and the cadence timer
    // (the forceSlot dispatcher has already slid out any active panel).
    // Momentary - re-arms the cadence so the normal rotation resumes after
    // the forced showing auto-hides.
    function force() {
        if (visible || !lastData || !lastData.standings) return;
        if (openPanel(true) && spec.interval) nextShowAt = Date.now() + spec.interval() * 1000;
    }

    // armRest: arm the global inter-panel rest (Panel Rest) on a NATURAL end of
    // turn (auto-hide / eligibility loss). Left false for pre-empt evictions and
    // battle-to-battle hops, which aren't the end of a turn. Only arms when a
    // visible panel actually goes away.
    function hide(armRest) {
        if (visible) {
            dlog("slot hide:", spec.name);
            if (armRest && CONFIG.slotRest > 0) slotRestUntil = Date.now() + CONFIG.slotRest * 1000;
        }
        visible = false;
        forcedShow = false;
        ineligibleSince = 0;
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

    // Restart: slide the panel fully OUT, then back IN with fresh build() content — a
    // clean "one leaves, a new one arrives" change like every other slot panel, for when
    // the panel's subject changes fundamentally (the director cuts to a genuinely
    // different battle). Reuses the evict hand-off (openAfterSlideOut) so the slide-out
    // completes before the slide-in starts and the tower is revealed as it goes. Not the
    // end of a turn → no inter-panel rest. No-op if not shown or already mid-hand-off.
    function restart() {
        if (!visible || pending) return;
        hide(false);                 // slide out (arm no rest — same panel, new content)
        openAfterSlideOut();         // ...then slide back in once it has cleared
    }

    function manage(session) {
        if (spec.enabled() && spec.eligible(session)) {
            var now = Date.now();
            ineligibleSince = 0;   // eligible again -> cancel any pending grace hide
            // Global inter-panel rest: block STARTING a new turn until it passes
            // (a visible panel keeps refreshing). Deferred, not dropped - the
            // event/eligibility is re-checked each manage() and fires once free.
            var resting = (now < slotRestUntil);
            if (spec.triggerOnEligible) {
                // No cadence (the battle filler): appear as soon as a battle is
                // eligible and the slot is free of equal/higher-priority panels,
                // after any post-cap rest. Boards still pre-empt it via show().
                if (!visible && !pending && !resting && now >= cooldownUntil) show();
            } else if (spec.eventKey) {
                // Event-driven boards: show once when the board's underlying metric
                // changes (a fresh fastest lap / session best / best sector), no
                // cadence timer. Seed on first sight so an existing record on connect
                // doesn't trigger a show. A caster can also force it via hotkey.
                var key = spec.eventKey();
                if (lastEventKey === undefined) {
                    lastEventKey = key;
                } else if (key != null && key !== lastEventKey && !resting) {
                    // Note the !resting guard is BEFORE consuming the key, so an event
                    // that lands mid-rest still shows once the rest passes (deferred).
                    if (!visible && !pending && now >= cooldownUntil) {
                        // Consume the trigger only once the panel actually takes the
                        // slot. A one-shot panel (deferEventWhenBlocked — charts'
                        // leader-finish) that is blocked by a manual force / higher-
                        // priority panel leaves the key unconsumed, so it retries and
                        // shows the moment the slot frees (a defer, like !resting)
                        // instead of dropping the event forever. Other event boards
                        // keep the eager consume: a stale fastest-lap/sector isn't
                        // worth re-showing once its slot moment has passed.
                        if (show() || !spec.deferEventWhenBlocked) lastEventKey = key;
                    } else {
                        lastEventKey = key;
                    }
                }
            } else if (spec.interval) {
                // Timestamp-based cadence (not a setInterval): arm the next
                // showing one interval out and only advance it once a showing
                // actually happens. An eligibility that flickers between
                // snapshots must NOT keep resetting the clock, or the panel
                // would be starved of its turn in exactly the busy races it
                // exists for. manage() runs every render(), so it polls.
                if (nextShowAt === 0) nextShowAt = now + spec.interval() * 1000;
                if (!visible && !pending && !resting && now >= nextShowAt) {
                    show();
                    if (visible || pending) nextShowAt = now + spec.interval() * 1000;
                }
            }
            if (visible && spec.refresh) spec.refresh();
        } else if (visible) {
            // Not enabled/eligible. A manually-forced panel stays up (and keeps
            // refreshing) until its own auto-hide - force is intentionally
            // condition-agnostic. Losing eligibility is a NATURAL end of turn, so
            // hide(true) arms the global inter-panel rest (but not the per-panel
            // cooldownUntil, so the same panel could re-take its turn once the rest
            // passes). Cadence clock is left as-is across flickers.
            if (forcedShow) {
                if (spec.refresh) spec.refresh();
            } else if (spec.triggerOnEligible) {
                // Debounce: only hide once it has been ineligible for the grace
                // window, so a one-snapshot eligibility blip can't make it dance.
                var tnow = Date.now();
                if (ineligibleSince === 0) ineligibleSince = tnow;
                else if (tnow - ineligibleSince >= ELIGIBLE_HIDE_GRACE_MS) hide(true);
                else if (spec.refresh) spec.refresh();   // keep content fresh during the grace
            } else {
                hide(true);
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
        restart: restart,
        refreshTimer: refreshTimer,
        masking: function () { return masking; },
        pending: function () { return pending; },
        forced: function () { return forcedShow; },
        covered: function () { return masking ? covered : 0; },
        // Update the covered-row count in place (no slide, no render() — would
        // recurse through manageSlots). Used by the battle panel to re-render a
        // changed group without a hide/reshow flicker; the current render pass's
        // renderStandings runs after manageSlots, so it picks up the new count.
        resize: function (n) { if (masking) covered = n; }
    };
    slotPanels.push(ctrl);
    return ctrl;
}


