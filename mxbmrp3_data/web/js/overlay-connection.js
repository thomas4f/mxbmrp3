// ============================================================================
// MXBMRP3 Web Overlay — Server-Sent Events connection & reconnect
// Part 03/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

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
var lastData = null;

// Debug logging: append ?debug (or #debug) to the overlay URL to log battle-set
// changes and bottom-slot panel show/hide to the browser console. Off by default.
// (Same URL-flag convention as ?demo below.)
var DEBUG = /[?&#]debug\b/i.test(location.search + location.hash);
function dlog() {
    if (DEBUG) console.log.apply(console, ["[mxbmrp3]"].concat([].slice.call(arguments)));
}
var _prevBattlesSig = null;
// Log the plugin's battle set whenever it changes, so a flickering DATA source
// (plugin side) is distinguishable from panel thrash (client side).
function debugLogBattles(data) {
    if (!DEBUG) return;
    var b = (data && data.battles) ? data.battles : [];
    var sig = JSON.stringify(b);
    if (sig === _prevBattlesSig) return;
    _prevBattlesSig = sig;
    dlog("battles set ->", b.length, "group(s):", sig);
}

// status: "info" (default), "ok", "error"
function appendStatusLine(message, status) {
    statusLines.push({
        time: clockNow(),
        ms: Date.now(),   // monotonic epoch-ms key for chronological merge with events
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

