# Web overlay

A live browser overlay of the session, served by the plugin itself: a standings
tower, an event log, a rider focus card, a sponsor-logo slideshow, and periodic
broadcast panels (last/best laps, best sectors, a "down the order" rundown,
session charts and on-track battles). Colors and fonts follow your in-game
settings.

It is built for an OBS Browser Source, but it works opened straight in a
browser - useful for checking it, or for a second screen without OBS.

## Setting it up in OBS

1. Turn on **Web server** (Settings > General). The address is in the
   **Live overlay** row, a clickable link while it is running.
2. In OBS, add a **Browser Source** with the URL `http://localhost:8080`
   (the default - use the port from step 1 if you changed it).
3. Set width and height to match your stream resolution, e.g., 1920x1080.

The server only answers this PC by default. To watch from another machine,
set `webServerBindAddress=0.0.0.0` under `[Advanced]` in
`mxbmrp3_settings.ini` (there is no menu control for it) and open
`http://<your-ip>:8080` there - this exposes the overlay to everyone on your
network.

## Configuring the overlay

Move the mouse over the overlay and a gear icon appears in the top-right
corner; click it for the settings panel. In OBS you need mouse access first:
right-click the Browser Source and choose **Interact**.

The panel covers:

- **General** - Hide when in Menus, Font Size, and Position (reset).
- **Logos** - the slideshow above the tower and its interval; drop PNGs into
  `web\logos\` (see [Modding](modding.md#where-custom-assets-go)).
- **Standings** - Positions +/-, Class (each rider's category, off by default),
  Hide DNS, Max Riders, Name Chars.
- **Broadcast Panels** - Panel Rows, Panel Time and Panel Rest, plus which
  panels run; the **Battle** and **Session Charts** sections pick their rows
  and charts.
- **Chips**, **Events** (including Max Entries and Timestamps) and the
  **Focus Card**.

Compact times are not here: the overlay follows the in-game HUD's setting.
The header bar drags the tower around the screen.

Everything you set there is saved in the browser's `localStorage`, so it
belongs to that browser (or that OBS source) rather than to the plugin - a
second machine watching the same overlay keeps its own arrangement.

## Making it look like yours

The overlay's HTML, CSS and JS are ordinary files you can edit; a `custom.css`
is loaded last so a color or font change needs no forking. See
[Modding](modding.md#web-overlay-files).
