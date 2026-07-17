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
    "js/overlay-config.js",
    "js/overlay-shell.js",
    "js/overlay-connection.js",
    "js/overlay-util.js",
    "js/overlay-render.js",
    "js/overlay-focus.js",
    "js/overlay-slots.js",
    "js/overlay-panels.js",
    "js/overlay-charts.js",
    "js/overlay-settings.js",
    "js/overlay-demo.js",
    "style.css",
    "logos/mxb-mods-logo.png",
    "logos/mxbikes-logo.png",
    "icons/flag-checkered.svg",
    "icons/gear.svg",
    "icons/stopwatch.svg",
    "icons/wrench.svg",
    "fonts/Audiowide-Regular.ttf",
    "fonts/EnterSansman-Italic.ttf",
    "fonts/FuzzyBubbles-Regular.ttf",
    "fonts/RobotoMono-Bold.ttf",
    "fonts/RobotoMono-Regular.ttf",
    "fonts/Tiny5-Regular.ttf"
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
            // theoretical "old overlay scripts with new cached index.html" mismatch
            // is not observable in practice — and on the next reload
            // everything is consistent.
            return self.skipWaiting();
        })
    );
});

self.addEventListener("activate", function (event) {
    console.log("[MXBMRP3 SW] activate", CACHE_NAME);
    event.waitUntil(
        caches.keys().then(function (keys) {
            // Our own overlay caches from an OLDER plugin version: a version bump changes
            // CACHE_NAME (it embeds __PLUGIN_VERSION__), so these are stale and get purged.
            var stale = keys.filter(function (key) {
                return key !== CACHE_NAME && key.indexOf("mxbmrp3-overlay-") === 0;
            });
            return Promise.all(keys.map(function (key) {
                return key === CACHE_NAME ? null : caches.delete(key);
            })).then(function () {
                if (!stale.length) return;   // first install / nothing to purge — stay quiet
                // Report the version-driven cache reset in the SW console, AND forward it to
                // every open overlay so it also lands in the PAGE console (where a caster is
                // more likely to be looking). See the message handler in overlay-config.js.
                console.log("[MXBMRP3 SW] plugin version changed — cleared old cache:",
                    stale.join(", "), "-> now", CACHE_NAME);
                return self.clients.matchAll({ includeUncontrolled: true }).then(function (clients) {
                    clients.forEach(function (client) {
                        client.postMessage({ type: "mxbmrp3-cache-updated", cache: CACHE_NAME, cleared: stale });
                    });
                });
            });
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

    // Bypass cache for user style overrides so edits show up on reload without
    // waiting for a plugin version bump to invalidate the cache.
    if (url.pathname.endsWith("/custom.css")) {
        return;
    }

    // Cache-first for the static shell, with background refresh on success.
    // Navigations ignore the query string ("/?debug" or an OBS source saved with
    // params must still hit the precached "./" when offline); asset requests
    // keep exact matching.
    event.respondWith(
        caches.open(CACHE_NAME).then(function (cache) {
            return cache.match(req, { ignoreSearch: req.mode === "navigate" }).then(function (cached) {
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
