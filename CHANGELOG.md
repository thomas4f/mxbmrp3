# Changelog

All notable user-facing changes to MXBMRP3 are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

**Versioning.** The version lives in `mxbmrp3/resource.h` as `MAJOR.MINOR.PATCH`;
a fourth component is stamped automatically at build time from the git commit
count, so the DLL reports `1.28.0.1234` while the release is tagged `v1.28.0`.
This is *not* strict semantic versioning: the plugin has no API consumers, so
MINOR carries feature releases and PATCH carries everything else. Tags before
`v1.27.3` carry the full four components (`v1.26.0.0`), and some PATCH numbers
never shipped publicly, so gaps in this list are expected.

Entries up to and including `v1.27.7` were compiled retroactively from the
[GitHub Releases](https://github.com/thomas4f/mxbmrp3/releases) of the public
repository, which were the project's release notes before this file existed.
Their wording is preserved; only the section headings are normalised to Keep a
Changelog's categories.

## [Unreleased]

## [1.29.5] - 2026-09-02

The HUD can draw itself now, instead of asking the game to, which hands back
frames the old way was costing.

### Added
- Direct GL Rendering (Settings > General): the plugin draws the in-game HUD
  itself, inside the game's own OpenGL context, rather than handing every piece
  of it to the game to draw, which can give you a healthy FPS gain - how much
  depends on your hardware, how many HUDs you run, and what else is on screen.
  Off by default, and it falls back to the old way by itself if it cannot run
- Turning Direct GL Rendering on now asks you to confirm it, with a countdown:
  if the HUD does not come out readable on your machine, wait it out or press the
  red button and it puts itself back. The countdown only runs while the HUD is
  actually on screen, so it will not time out on you in a menu
- Sharper text and icons in the companion window and under Direct GL: both are
  now drawn from properly downscaled copies of their source art instead of being
  sampled at full size, which was making small text look thin and broken up
- Five handwritten fonts, pickable in any font category and a natural fit for
  Marker: Permanent Marker, Rock Salt, Caveat Brush, Reenie Beanie and Gloria
  Hallelujah. They are bundled for the web overlay too, so an in-game font
  choice still follows through to OBS

### Changed
- The companion window is drawn by your GPU: smooth to drag things around with a
  theme on, and edges are no longer jagged
- The Direct GL confirmation prompt is marked with a question mark rather than a
  speedometer, so it reads as the question it is
- Updated vendored cpp-httplib to 0.54.1

### Fixed
- "Reset General" now really does reset the General tab: Grid Snap, Screen Clamp
  and Direct GL Rendering were left as they were. Your analytics choice is still
  left alone deliberately - a reset should not opt you back in
- "Reset Widgets" now also resets the Crashes widget and the pointer's menu-only
  setting, and "Reset Spotter" the spotter button's opacity and scale
- The web overlay now follows every font the game offers. The three IBM Plex
  faces were never bundled for it, so picking one in-game - or just running a
  Carbon theme, which uses them for every category - left the overlay on its own
  default font instead of matching the HUD
- The Standings session row now shows the clock in Testing and open practice,
  counting up from zero. A session with no time limit and no laps to run has
  nothing to count down, so the row showed the session name and nothing else -
  while the Time widget, the Timing panel and the web overlay were all showing
  that same clock ticking up

## [1.29.3] - 2026-08-29

Instrument faces become packs, themes recolour in one file, every pack type
now says the same thing at the top of its `.ini`, and an install missing its
textures no longer draws every sprite off by one.

### Added
- Gauges packs: the tacho and speedo faces travel with the numbers saying what
  each one reads, so a dial you draw gets a needle that agrees with it. Classic
  ships; pick a set in the Texture column of Settings > Widgets
- A theme recolour is one file: `base = <theme>` layers your `theme.ini` over an
  existing theme, so a folder holding nothing but an ini is a complete theme
- The colour picker carries the pack colours - Crimson, Orange, Yellow, Lime,
  Cyan, Navy, Royal, Silver and Graphite - so text can match your pit board or
  gamepad exactly instead of by eye
- Panel themes and spotter voices can title themselves in the picker with an
  optional `name` key, as gamepad and pit board packs already could
- The installer has a Privacy page: what the anonymous usage ping contains, and
  a tickbox to turn it off before the plugin ever runs

### Changed
- Every pack type opens its `.ini` with the same `[pack]` section, so what you
  learn writing one pack is what you write in the next. Packs made since 1.29.1
  need that one line changed
- A pack's `.ini` has a fixed name per type - `theme.ini`, `gamepad.ini`,
  `pitboard.ini`, `gauge.ini`, `spotter.ini` - so copying a shipped pack to
  start your own is just "rename the folder". Packs made before this keep
  working untouched
- Custom `tacho_widget`/`speedo_widget` art in your `textures\` folder is copied
  into a gauges pack for you on first run, and selected
- Three colours renamed to make room for the pack ones: Orange is Amber, Cyan is
  Aqua, Yellow is Bright Yellow and Dark Gray is Graphite. The colours
  themselves are unchanged, so nothing you have set moves

### Fixed
- Pit board and gamepad widgets drawing as empty panels after upgrading from a
  version where their artwork was a loose texture. Existing settings heal
  themselves on the next save
- An install with no `textures\` folder drew every theme and pack sprite off by
  one, each element wearing its neighbour's art. The plugin now also checks the
  whole sprite table against itself at startup and logs any skew it finds

## [1.29.1] - 2026-08-26

A spotter in your ear, panel themes for every HUD, and artwork that travels:
the gamepad, the pit board and the spotter's voice all become shareable packs.

### Added
- Spotter (beta): spoken race callouts while you ride - position, gaps,
  hazards and more, in any Windows voice or a recorded voice pack (separate
  download). Off by default; turn it on under Settings > Spotter. Every line
  is an editable file, subtitles are optional, and it follows whoever you are
  watching in spectate and replay
- Panel themes: a frame, header band and body card around every HUD and the
  settings menu. Two ship, Carbon dark and light - pick one under
  Settings > Appearance, or run with none. Anything you set yourself still
  wins, and any single HUD can use its own theme or opt out
- Gamepad and pit board packs: artwork and the measurements that place it
  travel together, so a pad or board you make can be shared. Two gamepads
  (Xbox, DualShock 4) and the Classic pit board ship, each with nine
  brand-colour skins
- Timing HUD: a second section of readouts - position, lap, time left, session
  format, fuel laps left, server and track
- Crashes widget: a large crash tally counting across sessions, servers and
  restarts until you reset it
- Standings can show each rider's last lap time as a column
- `uiFontSize` and `uiLineHeight` under `[Advanced]`: scale the whole
  interface, or give every row more air
- About page, opened from the settings footer; the Ko-fi link moves there
- The map keeps pointing at the track when you ride off it, instead of losing
  your marker
- A browser page that repairs MX Bikes trainer files the game corrupts at save
  time. Nothing to install (`tools/trnfix`)

### Changed
- The settings menu points out what is new: a "New" tag on a tab, and the row
  itself picked out until you hover it
- Text drop shadows are off by default - every shadowed string is a second
  draw. Turn them back on under Settings > Appearance
- Your gamepad and pit board are remembered by NAME, not texture number, so
  adding a pack never hands you a different one; existing settings convert on
  first load
- Panels land on the layout grid and captions share one height, so HUDs beside
  each other line up
- The Texture setting no longer offers "Off" on the gamepad, pit board, radar,
  speedo and tacho - there the artwork *is* the widget. Hide the widget instead
- The settings tab list is grouped under headings, and Reset is two buttons -
  Profile and Everything
- Reload Config re-reads themes and packs; new artwork reaches the game on the
  next launch
- Faster: cheaper panel layout, fewer quads submitted, and no companion redraw
  of an unchanged picture
- The log records what the spotter said, so a bad callout can be reported with
  its context
- Updated the embedded web-overlay HTTP server library
- Analytics now records which panel theme and spotter pack are in use (shipped
  names only)
- The reference half of the README moved into `docs/`, and both licence files
  now ship with the plugin
- Settings files are upgraded on first read by 1.29.1; going back to 1.28.0
  afterwards is not supported

### Fixed
- The winner crossing the line raised blue flags for the riders still racing
- The Fuel widget trusting your grid-inflated first lap when it was the only
  sample
- A crash when cycling spotter voices quickly
- A third-party voice engine crashing no longer takes the game with it; the
  voice is retired for the session
- The gap bar, notices, timing, version and radar drifting sideways when
  scaled; existing positions convert on first load
- The gap bar's time reading disappearing into its own coloured fill on opaque
  backgrounds
- The Benchmark HUD dropping panels from its report
- The web overlay could lose your saved settings on load in some browsers
- The uninstaller could leave its own executable behind when a scanner still
  held it open

## [1.28.0] - 2026-07-30

Broadcast director control, richer session charts, and broad stability and
performance fixes.

### Added
- Auto-director Max shot Off (now the default): cuts only for stories, then back
  to your rider
- Session charts can plot race traces at sector resolution: three points per lap
- Click-to-spectate from Event Log rows and from chart rider labels
- The Benchmark HUD and its report now cover the whole session, heaviest first

### Changed
- "Category" is now called "Class", matching the game; labels only, nothing you
  saved changes
- Onboard variety settings grey out while Max shot is Off
- The installer now carries full version information, so Windows and security
  scanners can identify what it is
- The log no longer records your Steam friends' names or servers, so a log
  pasted into a bug report carries nobody else's details
- The plugin now budgets itself against 480fps instead of 240fps
- Cheaper blue-flag detection and standings rebuilds
- Each session chart now labels its own right edge with its own range
- Less web overlay work on a full grid
- Updated the embedded web-overlay HTTP server library
- Standings number plates: the brand marker is now a right-pointing arrow rather
  than a bar, and the race number is set in your Title font - in-game and on the
  web overlay
- The helmet overlay is now in-game only, so it can no longer cover the HUDs the
  companion window exists to show

### Fixed
- The settings menu's title bar sticking out past everything under it. Its bar,
  the tab list's panel and the section panels now all stop on the same line, at
  any padding
- The settings menu ignoring a theme's panel padding, so it sat tight inside its
  frame while every HUD beside it kept the padding
- Title bars being a different height in the settings menu than on a HUD, so two
  headers side by side did not line up
- Settings tabs whose content ran under the Save and Close buttons - most
  visibly Help & Community on the General tab. The menu measures its tallest tab
  and is that tall on every tab, so nothing is ever cut off and the buttons stay
  put as you switch. It also follows your theme now: a theme with more air in it
  makes the menu taller instead of squeezing what fits
- Row highlights on Standings, Records and the Event Log spilling outside the
  panel they sit in once a theme set any padding. A highlight covers its row's
  text now, not the air around it
- A frame-time spike every ~10 seconds from the Steam friends scan; it now runs
  in the background
- GP Bikes and Kart Racing Pro shipping an unoptimized build; both should be
  markedly faster
- Crashes when the game closed or unloaded the plugin without shutting it down
- The experimental worker thread hanging the game with no way to recover
- Closing the game stalling for several seconds with a CPU core spinning
- The auto-director cutting to the wrong camera on tracks with a very long
  camera name
- A false all-time PB notice when riding a second bike in a class you already
  had a faster lap in
- Player-only timing rows showing your own data while spectating someone else
- A wrong-way rider not always being flagged as a hazard
- A blue flag not raised when the lapping rider sat exactly on the backmarker
- Telemetry graphs empty on a HUD enabled only on the companion window
- Settings tooltips being cut off instead of wrapping
- Session charts rebuilding the whole session once a second for nothing
- The Benchmark HUD showing "(none)" and mislabelled timings after leaving a
  session
- The Benchmark HUD using memory even when never opened
- Web overlay: on a phone the tower could stop short of filling the screen width
- Crash reports naming the wrong module and splitting one fault in two
- The Benchmark profiler collecting nothing, and its on/off misbehaving, when
  enabled only on the companion window
- The Performance and Telemetry graphs opening on leftover samples from an
  earlier stint when first enabled on the companion window
- The helmet button in the settings tab bar not switching the overlay off
- The Director and Benchmark profiler "Visible" rows toggling the wrong window,
  and their labels reading the wrong one
- The race number sitting high in the standings number plate instead of centred
- Every rider the auto-director followed to the flag being logged as the race
  winner; only the rider who actually won is now, and the rest read "Finishing"
- The uninstaller telling you to delete settings and data by hand even after you
  ticked the box to remove them

## [1.27.7] - 2026-07-18

Map performance and control, finer HUD adjustments, and broad stability fixes.

### Added
- Map Detail setting (20–200%) replaces Auto/High/Low; old settings migrate
- Map Adaptive detail toggle: every track looks and costs about the same
- Map Track outline width is now adjustable (Off ↔ 25–300%)

### Changed
- The map renders up to ~25% fewer quads at the same quality
- Leaner map defaults; markers no longer overflow a slim outline
- Reorganized Map settings tab with clearer tooltips
- Gap Bar range now adjusts in fine 250 ms steps and accelerates when held
- Hardened the auto-updater (HTTPS-only, GitHub-only, redirect-aware, verified and complete downloads)
- Web overlay: settings toggles are keyboard-operable, and a tower saved on a larger monitor no longer loads off-screen
- Lower CPU use for the rumble graph, gamepad widget, and lap segment timer
- Custom segment timer now accumulates like the official split time
- INI-only settings now document their defaults and valid ranges
- Various stability and performance improvements

### Fixed
- Web overlay caching: refresh your OBS browser source once after updating
- The web overlay update throttle setting doing nothing
- A corrupt custom asset killing the companion window
- The companion window hanging or crashing when closed
- Rapid director camera switches being occasionally dropped
- Stale live-gap, hazard, and setup-notice state when a race number is reused
- Stale bike categories carrying over after a stats reload
- Hardened track-geometry and Kart Racing Pro entry handling against malformed input

## [1.27.5] - 2026-07-14

Threading and performance: an optional plugin worker thread and a few small fixes.

### Added
- Experimental plugin worker thread (see documentation)

### Changed
- Controller input and rumble now run on a dedicated thread
- Crash reports capture more diagnostic detail (faulting-stack info and fault address)
- Various stability and performance improvements

### Fixed
- The Records HUD footer being misaligned
- A faint text-rendering artifact

## [1.27.4] - 2026-07-12

Hotfix for v1.27.3.

### Changed
- Updated vendored miniz to 3.1.2 and doctest to 2.5.3

### Fixed
- Auto-updater downloaded the wrong file and couldn't install

## [1.27.3] - 2026-07-12

Auto-director for spectating and replays, companion window, Charts HUD, and
expanded web overlay.

### Added
- Auto-director for spectating and replays
- Companion window (second-monitor HUD)
- Charts HUD: a new in-game HUD (replacing Lap Consistency)
- Web overlay: session-charts panel
- Web overlay: best-sectors panel
- Web overlay: rider focus card
- Web overlay: live battle gaps
- Analytics: additional events including crashes

### Changed
- Timing HUD redesigned
- Web overlay broadcast panels stay in sync with the director
- Web overlay settings overhaul
- Standings: new director-lock icon; the "Top positions" limit is now configurable in-game
- Map: new toggle to hide the start/finish, sector, and segment-line markers
- Settings: the plugin now remembers which settings tab you had open
- Settings and personal-best saves are deferred until you leave the track
- Installer: new options to do a fresh install (wiping old data) or remove your data on uninstall
- Installer: now runs without admin rights and only elevates when it actually needs to
- Various stability and performance improvements

### Fixed
- The lap timer not freezing on the first lap after a garage/pit start
- On standing (grid) starts, the lap timer now begins at the gate drop
- Timing HUD no longer shows INVALID for a lap interrupted by a pit stop
- Suppressed spurious wrong-way and hazard warnings at the grid start until the first split
- A spurious "takes the lead" message on session transitions
- The rider focus card mis-aligning under the name and occasionally staying hidden after a battle

## [1.26.0.0] - 2026-06-28

### Added
- Anonymous usage analytics (no personal data) to help guide development
- "Lapper ahead" notice informs you (the lapping rider) when you're closing on a player who's a lap or more down
- Ability to hide the cursor and settings gear while riding, showing them only when you open the menu via hotkey

### Changed
- Map: rider icons now draw on top of all track markers, and the Zoom range is now a smooth accelerated step
- Radar: Range and Alert distance now use the same accelerated step as the map
- Settings sliders: every multi-step numeric control now hold-accelerates
- Standings: in pure-lap races, the title shows the session clock before the race starts
- Notices settings: merged the "Warnings" and "Hazards" sections into one to save space
- Help & Community links are now clickable and open in your browser
- Web overlay: more spacing between the position and number plate in the standings

### Fixed
- A blue-flag/lapping detection bug

## [1.25.3.0] - 2026-06-27

Pre-release. Its contents shipped in 1.26.0.0, minus the analytics addition.

### Added
- "Lapper ahead" notice informs you (the lapping rider) when you're closing on a player who's a lap or more down
- Ability to hide the cursor and settings gear while riding, showing them only when you open the menu via hotkey

### Changed
- Map: rider icons now draw on top of all track markers, and the Zoom range is now a smooth accelerated step
- Radar: Range and Alert distance now use the same accelerated step as the map
- Settings sliders: every multi-step numeric control now hold-accelerates
- Standings: in pure-lap races, the title shows the session clock before the race starts
- Notices settings: merged the "Warnings" and "Hazards" sections into one to save space
- Help & Community links are now clickable and open in your browser
- Web overlay: more spacing between the position and number plate in the standings

### Fixed
- A blue-flag/lapping detection bug

## [1.25.0.0] - 2026-06-21

### Added
- New Segment Timer: a training tool to time custom sections of track
- New UI icons: HUDs, settings tabs, and the settings button now show identity icons
- New Compass widget: a rotating heading dial showing the rider's heading
- Timing & Standings HUDs: compare against last laps (a Last Lap chip/toggleable Last Lap column)
- Web overlay: broadcasters can limit battle cards to the top N positions

### Changed
- Map: split, holeshot and segment markers now sit exactly on the centerline

### Fixed
- Wrong finish order, gaps and labels in a race held after a qualifying session
- A spurious "#X takes the lead" message at the start of a new session

## [1.24.0.0] - 2026-06-14

### Added
- New Friends HUD: see which of your Steam friends are also in-game, what server and track they're on
- Rumble: the Bumps and Lockup effects can now be tuned separately for the front and rear wheel
- Rumble: new Rev and Pit Limiter effects that buzz when active
- Standings HUD can now display a positions gained/lost column
- G-Force widget ring now indicates G-load severity by color
- Settings button (the `[=]` toggle in the corner) can now be configured in the Widgets tab
- Web overlay: new panels (fastest lap, battles, down the order, ...)
- Web overlay: broadcasters can force a specific panel to appear with a hotkey

### Changed
- Lower CPU/GPU overhead across controller polling, hotkeys, the map, the telemetry graph and web overlay
- Rumble is now rate-limited so it no longer floods the connection, which may help with performance
- Rumble graph now shows separate front and rear traces when an effect is split
- G-Force ring holds its peak color briefly and freezes red during a crash
- G-Force ring full-scale raised to 20 g (was 5 g)
- Speedo and Tacho needles, and the Speedo odometer, now fade together with the dial when lowering opacity
- The mouse cursor and settings button now require a small deliberate mouse move, so accidental bumps are ignored
- Standings HUD: session-info row under the title instead of replacing it
- Tracked riders custom colors now appear on the number plate in standings and web overlay
- Time + lap races now show N to go, final lap and checkered when the clock runs out, instead of freezing at 00:00
- FMX HUD rotation arcs now freeze red during a crash, matching the Bars and G-Force widgets
- Simplified Session HUD and made the server appear in larger text
- Web overlay: now fills the screen width on phones, so it's readable on a mobile browser
- Web overlay: append `?demo` to the overlay URL to preview a synthetic race

### Fixed
- Gamepad elements, Bars, Lean, Tyre Temp and Rumble gauges no longer fade their bar/arc backgrounds with the opacity slider
- ECU widget parameter chips no longer disappear when lowering the background opacity
- Session HUD is no longer hidden by the "hide all widgets" toggle
- Several potential crashes (records leaderboard refresh, game version/struct mismatches, and quitting mid-update-check)
- Controller hotkeys now work while spectating and in menus
- Time + lap qualifying no longer shows the checkered flag early when the clock expires
- Web overlay: standings columns (position, number plate, gap) now size to the configured font

## [1.23.10.0] - 2026-05-30

### Added
- Support for Kart Racing Pro
- G-Force widget
- ECU widget for GP Bikes
- Standings title now shows live session info in place of a static title
- Map HUD now renders split markers, start/finish, and a holeshot marker
- Standings position animation expanded to Off / Basic / Colored
- FMX HUD now recognizes Coaster Wheelies (wheelie with clutch pulled in)
- The Audiowide font

### Changed
- Major FPS improvement on long, curvy tracks when the Map HUD is enabled
- Online detection and server name now sourced directly from the game API
- Penalty time now displays correctly across the Event Log, Standings, and Web Overlay
- The player/spectated rider's highlight bar in the Standings now uses the accent color
- Overhauled FMX trick classification and polished the UI
- Gear widget shows "D" instead of "N" for gearless vehicles
- Peak markers in various widgets now freeze and turn red during a crash
- Standings spectate selection is now limited to riders on track
- Map HUD rider colors refined, with the player highlighted in the accent color
- Unified HUD layouts and added optional column headers
- Compact time format now keeps millisecond precision for gaps
- Compact time format is now ON by default
- "Reset all settings" now also clears INI-only/advanced overrides
- Per-tab "Reset" no longer hides an element that's hidden by default
- Update notifications are now enabled by default
- Web overlay now supports lightweight style overrides via `custom.css`
- Many plugin-caused crashes are now caught at the API boundary instead of taking the game down
- For crashes that do get through, a minidump is now written to `Documents\PiBoSo\[Game]\mxbmrp3\crashes\`

### Fixed
- Standings position animation now slides rows in the correct direction
- GP Bikes sessions are now labelled correctly
- Per-HUD color/font overrides no longer persist across profile switches or survive a reset
- Standings styling no longer disappears when primary and muted colors are identical
- Thread-safety issues in Discord rich presence and online records fetching

### Removed
- Player count row in Session HUD and Discord rich presence
- Server password row in Session HUD

## [1.22.0.0] - 2026-04-26

### Added
- Helmet overlay HUD with visor tint, lean-linked tilt, and suspension vibration
- Stats HUD now follows the spectated rider
- Web server port configurable in-game (Settings > General)
- Logo slideshow banner in the web overlay

### Changed
- Web overlay: new Name Chars setting controls tower width
- Web overlay settings reorganized (Logos, Standings, merged Events section)
- PB Scope now defaults to Category instead of Bike for new installs
- Local player's icon on the map no longer changes for hazard/flag states
- Web server shows "Error" with port info when the port is unavailable

### Fixed
- Suppress stationary hazard warning for riders who left the pits but haven't moved
- Web server silently binding to an already-used port (SO_REUSEADDR)

## [1.21.4.0] - 2026-04-07

### Changed
- OBS overlay now loads offline (but requires Web Server to retrieve data)
- Session timer properly pushed from the plugin
- Overlay settings apply instantly
- Menu state labeled in the overlay header

## [1.21.2.0] - 2026-04-05

### Added
- Embedded HTTP server serving a live race overlay
- PB scope setting (Bike vs Category)

### Changed
- Updated Fastest Lap event icon and color
- New "Leader change" event log
- Settings UI reorganization

### Fixed
- Event log showing wrong finishing positions
- Stale HUD data after track/bike switch

### Removed
- Status column removed from standings

## [1.20.0.0] - 2026-03-30

### Added
- Event Log HUD showing a feed of notable race events

### Changed
- Replaced hardcoded radar proximity gradient with semantic colors
- Added external RGB to ABGR color converter tool
- UI/UX tweaks

### Fixed
- Last-lap indicators (LL status, white flag, LAST LAP notice) incorrectly showing in non-race sessions
- FINISHED and LAST LAP notices not re-triggering when switching spectated rider
- Hotkeys firing when the game window is not focused
- Spectate finish notice

## [1.19.0.0] - 2026-03-28

### Added
- Hazard detection system (stationary on track, wrong way)
- Alternating gap reference mode
- Status icons (pit, last lap, finished) in standings
- Compact Times display option
- Records HUD support when spectating

### Changed
- Increased the maximum number of entries in Standings HUD to 50
- Consistent precision for live and official gap timing

### Fixed
- Lapped riders not receiving FIN status after the leader finishes
- False blue flags from stale track positions

### Removed
- Live position sorting (the game only provides track data for 10 riders, making it unreliable)

## [1.18.0.0] - 2026-03-22

### Added
- Animated position changes in standings
- Race number plates with bike brand color strips
- Live position updates
- Filtering of DNS riders

### Changed
- Rider name mode: Off / Short / Long
- Simplified standings gap system
- User asset override directories are now auto-created
- Show "Not Connected" overlay on gamepad widget when no controller
- Upgraded Notices Widget to Notices HUD with a dedicated settings tab
- UI/UX tweaks

### Fixed
- Timing HUD drifting off-screen when placed near a screen edge
- Lap-based color accents showing in non-race sessions

## [1.17.0.0] - 2026-03-15

### Added
- Default setup warning notice on the HUD
- Benchmark profiler widget for plugin performance analysis

### Changed
- Several UI/UX consistency improvements

### Fixed
- Lap rejection on tracks with broken split markers (timing now works on KMX-Spring a Ding)

## [1.16.6.0] - 2026-03-14

### Added
- Clock widget showing local/UTC time
- Hold-to-repeat with acceleration for settings controls
- Several compare modes in the Pitboard HUD

### Changed
- Draggable Notices Widget
- Various UI/UX layout and consistency tweaks

### Fixed
- HUD background textures stretching
- Live gap column in Standings never displaying
- Drop shadow not resetting

## [1.16.1.0] - 2026-03-08

### Added
- Stats HUD
- Separated speed and gear into independent widgets with a large-font gear display
- Session PB, overall PB, and all-time PB notices

### Changed
- Made LAST LAP and FINISHED notices timed instead of persistent
- Changed rev limiter texture
- Slimmed down element visibility defaults
- Consolidated stats, personal bests, and odometer into a single file

### Fixed
- Auto-switch profiles overriding manual profile changes
- Fuel average/estimate being skewed by invalid laps (pit-in, cut track)
- Whip/scrub classification in FMX HUD
- Missing resets in Reset All Settings
- Wrong way notice flashing on teleport
- Missing elements in the hotkeys menu

## [1.15.0.0] - 2026-02-11

### Added
- FMX trick detection and scoring HUD
- Lap Consistency HUD
- Per-HUD color and font overrides via INI

### Changed
- Removed dependency on Visual C++ Redistributable installation
- Refactored INI format to use base sections with sparse profile overrides
- Moved HUD-specific settings from `[Advanced]`/`[General]` to HUD sections
- Disabled lap-based color modulation for tracked riders in non-race sessions

### Fixed
- Game freeze on close
- Rumble HUD graphs, and 0% values shown when spectating/replay
- Rumble tab checkbox now toggles master rumble from any tab
- Performance/Telemetry display mode control not working

## [1.14.0.0] - 2026-01-24

### Added
- Discord Rich Presence support (MX Bikes only)
- Refactored Session HUD into a full HUD with more info (server, weather, ...)
- Tyre temperature widget (GP Bikes only)
- Per-bike odometer tracking with persistent storage
- Per-bike rumble profiles for customized vibration settings
- Engine and water temperature bars in the Bars Widget
- Option to hide bar labels in the Bars Widget
- Auto-save toggle for manual settings management
- Digits font category for numeric displays

### Changed
- Drop shadow enabled by default

### Fixed
- Lean Widget steer value always showing 0
- Effect profile tooltip length and styling

## [1.13.3.0] - 2026-01-18

### Added
- GP Bikes support (same plugin, separate `.dlo` file)
- Color mode setting in the Gap Bar HUD
- Flat map mode in the Gap Bar HUD
- Blue sector highlighting for lappers on radar
- Screen clamping toggle in General settings
- `standingsUseAccentHighlight` advanced setting
- Per-button face sprites in the Gamepad Widget

### Fixed
- Timing HUD not showing records when Records HUD is hidden
- Gap bar/lap log gap sometimes not showing in races
- Timing HUD padding
- Controller selector separate click regions
- Suspension data lost after spectating

## [1.12.0.0] - 2026-01-11

### Added
- Row toggle support for the Session Widget
- Configurable top positions count setting for the Standings HUD
- Display order option in the Lap Log HUD (Oldest/Newest first)
- Live gap row in the Lap Log HUD showing gap-to-PB (colorized)
- Drop shadow option for HUD text
- Placeholder rows to show configured HUD size
- Date shown for personal bests in the Records HUD

### Changed
- Colorize gap bar text based on value (green = faster, red = slower)
- Simplified column/row toggles to group-based settings
- Moved records auto-fetch to global settings
- Performance improvements for the Map HUD and disabled HUDs

### Fixed
- Bars Widget not updating throttle/brake when spectating
- Leader showing wrong gap sign in player-relative mode
- Telemetry widgets not clearing when the spectate target becomes invalid
- Map zoom mode losing the player when scaling or crashing
- HUD flicker and position jumping during profile switching
- Profile cycle arrows only working on the General tab
- Standings displaying fewer rows than configured

## [1.11.0.0] - 2026-01-05

### Added
- Auto-update system with in-game installation
- MXB-Ranked leaderboard support in the Records HUD
- User asset overrides for custom fonts, textures, and icons
- In-game tooltips for all settings
- "Record" gap comparison in the Timing HUD

### Changed
- Lingering max markers on telemetry bars
- Auto-fetch option for the Records HUD
- Fill mode option for Gamepad Widget triggers

### Fixed
- Fuel tracking now resets properly when entering track

## [1.10.0.0] - 2025-12-29

### Added
- Personal best lap times now persist across sessions
- Proximity arrows in the Radar HUD
- Gamepad Widget replaces the Input HUD
- Click-to-spectate on the Map HUD

### Changed
- Records HUD shows your personal best
- Timing HUD shows actual improvement
- Reload Config hotkey
- Inverted gaps shown in warning color when pending official updates
- Live gap shows N/A in non-race sessions
- Improved settings storage
- More INI settings for power users

## [1.9.2.0] - 2025-12-23

### Added
- Tracked riders with custom colors and icons across all HUDs
- Customizable keyboard and controller hotkeys
- Dynamic asset loading for fonts, textures, and icons
- Multiple gap type options in the Timing HUD (PB, Ideal, Session Best)
- Wheelie rumble effect for controller feedback
- Force visualization bars in the Forces HUD
- Live gap coloring in the Lap Log HUD
- Live timing display in the Lap Log HUD
- Player-relative gap mode for Standings
- Marker scale setting for rider icons
- Edge magnetism when grid snapping (HUDs snap flush to screen edges)
- INI-configurable row toggles for the Fuel Widget and Speed Widget

### Changed
- Show the leader's best lap in the official gap column
- Show gap bar with 3 decimal places
- Show improvement vs previous ideal lap when beating theoretical best
- Show "Finish" label and total race time after completing a race
- Changed track width from fixed meters to an adaptive percentage scale
- Clear separation of global vs profile settings
- Overhauled rumble settings
- Improved copy/reset UX in settings
- Display controller name instead of index
- Use monospace font for timing text to prevent number jumping
- Renamed SessionBestHud to IdealLapHud

### Fixed
- Race finish detection error
- Cursor focus detection robustness
- Performance regressions and code duplication
- Live timing now stops after a rider finishes the race
- Live timing now pauses when the game pauses

## [1.8.0.0] - 2025-12-16

### Added
- Controller rumble and Forces HUD
- Centralized lap timer for real-time sector timing

### Changed
- Reduced default HUD clutter

## [1.7.0.0] - 2025-12-13

### Added
- Gap Bar HUD for lap timing comparison
- Real-time elapsed lap timer in the Timing HUD
- Relative position color mode for map and radar rider icons
- Quad-based pointer rendering
- Granular reset options
- Update checker

### Changed
- Documentation and code maintainability updates

## [1.6.0.0] - 2025-12-09

### Added
- Map zoom mode with player-following view (50–500 m range)
- Qualify profile for qualifying sessions
- Apply to All button for copying settings across profiles
- Restore Defaults with confirmation checkbox
- Widgets master toggle to enable/disable all widgets at once
- Configurable Accent color with hover highlights throughout the UI

### Changed
- UI consistency tweaks

## [1.5.5.3] - 2025-12-07

### Added
- Records HUD: displays lap records from online leaderboards
- Radar HUD: top-down view of nearby riders with proximity alerts
- Fuel Widget: tracks fuel level and consumption
- Customizable color palette (reworked Settings menu)
- Speed units toggle (km/h / mph)
- Fuel units toggle (L / gal)
- Grid snapping toggle for HUD positioning

### Changed
- Various UI/default tweaks

## [1.5.4.2] - 2025-12-04

### Fixed
- The plugin now identifies the local player by event order rather than name
  matching, so it works regardless of how the server modifies your name.
  Thanks to mushy for testing.

## [1.5.4.1] - 2025-12-03

### Fixed
- Partly fixed player identification on servers that alter Rider Name
  ([#1](https://github.com/thomas4f/mxbmrp3/issues/1))

## [1.5.3.0] - 2025-11-29

Initial public release.

[Unreleased]: https://github.com/thomas4f/mxbmrp3/compare/v1.29.5...HEAD
[1.29.5]: https://github.com/thomas4f/mxbmrp3/compare/v1.29.3...v1.29.5
[1.29.3]: https://github.com/thomas4f/mxbmrp3/compare/v1.29.1...v1.29.3
[1.29.1]: https://github.com/thomas4f/mxbmrp3/compare/v1.28.0...v1.29.1
[1.28.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.27.7...v1.28.0
[1.27.7]: https://github.com/thomas4f/mxbmrp3/compare/v1.27.5...v1.27.7
[1.27.5]: https://github.com/thomas4f/mxbmrp3/compare/v1.27.4...v1.27.5
[1.27.4]: https://github.com/thomas4f/mxbmrp3/compare/v1.27.3...v1.27.4
[1.27.3]: https://github.com/thomas4f/mxbmrp3/compare/v1.26.0.0...v1.27.3
[1.26.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.25.3.0...v1.26.0.0
[1.25.3.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.25.0.0...v1.25.3.0
[1.25.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.24.0.0...v1.25.0.0
[1.24.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.23.10.0...v1.24.0.0
[1.23.10.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.22.0.0...v1.23.10.0
[1.22.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.21.4.0...v1.22.0.0
[1.21.4.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.21.2.0...v1.21.4.0
[1.21.2.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.20.0.0...v1.21.2.0
[1.20.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.19.0.0...v1.20.0.0
[1.19.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.18.0.0...v1.19.0.0
[1.18.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.17.0.0...v1.18.0.0
[1.17.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.16.6.0...v1.17.0.0
[1.16.6.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.16.1.0...v1.16.6.0
[1.16.1.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.15.0.0...v1.16.1.0
[1.15.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.14.0.0...v1.15.0.0
[1.14.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.13.3.0...v1.14.0.0
[1.13.3.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.12.0.0...v1.13.3.0
[1.12.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.11.0.0...v1.12.0.0
[1.11.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.10.0.0...v1.11.0.0
[1.10.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.9.2.0...v1.10.0.0
[1.9.2.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.8.0.0...v1.9.2.0
[1.8.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.7.0.0...v1.8.0.0
[1.7.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.6.0.0...v1.7.0.0
[1.6.0.0]: https://github.com/thomas4f/mxbmrp3/compare/v1.5.5.3...v1.6.0.0
[1.5.5.3]: https://github.com/thomas4f/mxbmrp3/compare/v1.5.4.2...v1.5.5.3
[1.5.4.2]: https://github.com/thomas4f/mxbmrp3/compare/v1.5.4.1...v1.5.4.2
[1.5.4.1]: https://github.com/thomas4f/mxbmrp3/compare/v1.5.3.0...v1.5.4.1
[1.5.3.0]: https://github.com/thomas4f/mxbmrp3/releases/tag/v1.5.3.0
