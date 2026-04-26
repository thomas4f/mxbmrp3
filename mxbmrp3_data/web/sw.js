// ============================================================================
// MXBMRP3 Web Overlay - Service Worker
// Caches the static overlay shell so OBS can load the UI even when the plugin
// HTTP server is not running yet (e.g. PC restart, MX Bikes not started yet).
// Live data endpoints (/api/*) always go to the network.
// ============================================================================

// __PLUGIN_VERSION__ is substituted by the plugin's HTTP server when serving
// this file, so a plugin update automatically invalidates the old cache.
var CACHE_NAME = "mxbmrp3-overlay-__PLUGIN_VERSION__";

// Precache the full overlay shell so a cold load with the plugin server
// down still renders correctly (icons, logo, fonts all available offline).
// Individual .add() calls below tolerate missing files, so renaming or
// removing an asset won't break SW install — the stale entry is just
// silently skipped and the new one is lazy-cached on first fetch.
var PRECACHE_URLS = [
    "./",
    "index.html",
    "app.js",
    "style.css",
    "logos/mxb-mods-logo.png",
    "logos/mxbikes-logo.png",
    "flag-checkered.svg",
    "gear.svg",
    "stopwatch.svg",
    "video.svg",
    "wrench.svg",
    "EnterSansman-Italic.ttf",
    "FuzzyBubbles-Regular.ttf",
    "RobotoMono-Bold.ttf",
    "RobotoMono-Regular.ttf",
    "Tiny5-Regular.ttf"
];

console.log("[MXBMRP3 SW]", CACHE_NAME, "loaded");

self.addEventListener("install", function (event) {
    console.log("[MXBMRP3 SW] install");
    event.waitUntil(
        caches.open(CACHE_NAME).then(function (cache) {
            // Use {cache: "reload"} so install always fetches fresh copies.
            return Promise.all(PRECACHE_URLS.map(function (url) {
                return cache.add(new Request(url, { cache: "reload" })).catch(function () {
                    // Ignore individual failures so one missing asset doesn't
                    // abort the whole install.
                });
            }));
        }).then(function () {
            // skipWaiting + clients.claim activates a new SW mid-session.
            // For an OBS browser source the tab persists for hours and the
            // user only updates the plugin while the game is closed, so the
            // theoretical "old app.js with new cached index.html" mismatch
            // is not observable in practice — and on the next reload
            // everything is consistent.
            return self.skipWaiting();
        })
    );
});

self.addEventListener("activate", function (event) {
    console.log("[MXBMRP3 SW] activate");
    event.waitUntil(
        caches.keys().then(function (keys) {
            return Promise.all(keys.map(function (key) {
                if (key !== CACHE_NAME) {
                    return caches.delete(key);
                }
            }));
        }).then(function () {
            return self.clients.claim();
        })
    );
});

self.addEventListener("fetch", function (event) {
    var req = event.request;
    if (req.method !== "GET") return;

    var url = new URL(req.url);

    // Never cache live data endpoints — let them fail naturally so the client
    // can retry/reconnect when the server comes back up.
    if (url.pathname.indexOf("/api/") === 0) {
        return;
    }

    // Cache-first for the static shell, with background refresh on success.
    event.respondWith(
        caches.open(CACHE_NAME).then(function (cache) {
            return cache.match(req).then(function (cached) {
                var network = fetch(req).then(function (resp) {
                    if (resp && resp.ok && resp.type !== "opaque") {
                        cache.put(req, resp.clone());
                    }
                    return resp;
                }).catch(function () {
                    return cached;
                });
                return cached || network;
            });
        })
    );
});
