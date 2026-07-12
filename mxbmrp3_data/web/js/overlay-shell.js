// ============================================================================
// MXBMRP3 Web Overlay — Page shell: DOM refs, responsive sizing, logos, tower position
// Part 02/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

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
var sectorsPanel = document.getElementById("sectors-panel");
var sectorsViewport = sectorsPanel.querySelector(".sectors-viewport");
var sectorsTrack = sectorsPanel.querySelector(".sectors-track");
var chartsPanel = document.getElementById("charts-panel");
var chartsViewport = chartsPanel.querySelector(".charts-viewport");
var chartsTrack = chartsPanel.querySelector(".charts-track");
var tailPanel = document.getElementById("tail-panel");
var tailTitle = tailPanel.querySelector(".tail-title");
var tailViewport = tailPanel.querySelector(".tail-viewport");
var tailTrack = tailPanel.querySelector(".tail-track");
var battlePanel = document.getElementById("battle-panel");
var battleList = document.getElementById("battle-list");
var focusCard = document.getElementById("focus-card");

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

