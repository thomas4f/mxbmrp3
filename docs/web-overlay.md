# Web overlay

A live browser overlay of the session, served by the plugin itself: a standings
tower, an event log, a rider focus card, and periodic broadcast panels
(fastest-lap boards, a "down the order" rundown, and on-track battles). Colors
and fonts follow your in-game settings.

It is built for an OBS Browser Source, but it works opened straight in a
browser - useful for checking it, or for a second screen without OBS.

## Setting it up in OBS

1. Turn on **Web Server** (Settings > General). The port is shown next to the
   setting while it is running.
2. In OBS, add a **Browser Source** with the URL `http://localhost:8080`
   (the default - use the port from step 1 if you changed it).
3. Set width and height to match your stream resolution, e.g., 1920x1080.

Anyone on your network can watch it too, at `http://<your-ip>:8080`.

## Configuring the overlay

Move the mouse over the overlay and a gear icon appears in the top-right
corner; click it for the settings panel. In OBS you need mouse access first:
right-click the Browser Source and choose **Interact**.

The panel sets compact times, tower size, event and chip filters, the focus
card and font size. The header bar drags the tower around the screen.

Everything you set there is saved in the browser's `localStorage`, so it
belongs to that browser (or that OBS source) rather than to the plugin - a
second machine watching the same overlay keeps its own arrangement.

## Making it look like yours

The overlay's HTML, CSS and JS are ordinary files you can edit; a `custom.css`
is loaded last so a color or font change needs no forking. See
[Modding](modding.md#web-overlay-files).
