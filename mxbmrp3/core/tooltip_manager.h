// ============================================================================
// core/tooltip_manager.h
// Manages tooltips for settings UI elements.
// Strings are compiled into the plugin (no external file).
//
// LENGTH LIMIT: tooltips render as at most 2 word-wrapped lines (~60 chars each)
// in the settings panel; anything beyond ~120 characters is hard-truncated with
// "..." (see renderWrappedText in settings_hud.cpp, MAX_LINES=2). Keep every
// description <= ~120 chars - prefer ~100 to leave wrap margin. Measuring stick:
// "rumble.bumps" (~94 chars) fills the box without truncating - treat that as the
// comfortable ceiling.
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>

class TooltipManager {
public:
    static TooltipManager& getInstance() {
        static TooltipManager instance;
        return instance;
    }

    // Get tab tooltip by tab ID (e.g., "standings", "map")
    const char* getTabTooltip(const char* tabId) const {
        const auto& m = tabs();
        auto it = m.find(tabId);
        return (it != m.end()) ? it->second : "";
    }

    // Get control tooltip by control ID (e.g., "common.visible", "standings.rows")
    const char* getControlTooltip(const char* controlId) const {
        const auto& m = controls();
        auto it = m.find(controlId);
        return (it != m.end()) ? it->second : "";
    }

private:
    TooltipManager() = default;
    ~TooltipManager() = default;
    TooltipManager(const TooltipManager&) = delete;
    TooltipManager& operator=(const TooltipManager&) = delete;

    using Map = std::unordered_map<std::string, const char*>;

    static const Map& tabs() {
        static const Map m = {
            {"general", "Global settings including unit preferences, profiles, and configuration options."},
            {"appearance", "Customize the visual style of all HUD elements including fonts and colors."},
            {"hotkeys", "Configure keyboard shortcuts and controller buttons for quick access to HUD toggles."},
            {"riders", "Track specific riders with custom colors and icons. Highlighted on standings, map, and radar."},
            {"rumble", "Configure controller vibration for racing events like bumps, wheelspin, and impacts."},
            {"updates", "Automatic update checking and installation. Downloads and installs new versions from GitHub."},
            {"standings", "Live race standings showing position, gaps, and rider information for all competitors."},
            {"map", "Overhead view of the track with real-time rider positions. Supports zoom and display modes."},
            {"radar", "Shows nearby riders relative to your position. Useful for close racing awareness."},
            {"lap_log", "History of your recent lap times with sector breakdowns. Shows lap-by-lap performance."},
            {"friends", "Steam friends in the same game: where they are, badged when on your server and track."},
            {"lap_consistency", "Visualizes lap time consistency with bar charts and statistics. Track your improvement over time."},
            {"ideal_lap", "Displays your theoretical best lap time calculated from your fastest individual sectors."},
            {"telemetry", "Real-time input data including throttle, brake, clutch, RPM, suspension travel, and gear."},
            {"performance", "System performance metrics including frame rate and plugin execution time."},
            {"records", "Lap records from online providers (CBR, MXB-Ranked) for your current track."},
            {"pitboard", "Info board at pit area or timing points. Shows position, lap times, and gap comparisons."},
            {"session", "Session information including track, server details, player count, and password display."},
            {"timing", "Lap and sector timing display with comparisons to session best and personal records."},
            {"gap_bar", "Visual progress bar showing your gap to the rider ahead and behind."},
            {"widgets", "Simple HUD elements showing speed, position, lap count, session time, and other info."},
            {"notices", "On-screen notices for race events, personal bests, and setup warnings."},
            {"event_log", "Scrolling feed of race events: session changes, fastest laps, penalties, and finishes."},
            {"fmx", "Freestyle trick detection with scoring, chain combos, and rotation visualization."},
            {"stats", "Per-track statistics including lap counts, crashes, gear shifts, top speed, and riding time."},
            {"helmet", "First-person helmet overlay with lean tilt and vibration."},
        };
        return m;
    }

    static const Map& controls() {
        static const Map m = {
            {"common.visible", "Show or hide this element during gameplay."},
            {"common.title", "Toggle the title bar at the top of this HUD."},
            {"common.texture", "Select a background texture variant. Off uses a solid color background."},
            {"common.opacity", "Background transparency. 0% is fully transparent, 100% is fully opaque."},
            {"common.scale", "Size multiplier. Affects all text and graphics in this HUD."},

            {"standings.live_gaps", "Show real-time estimated gaps during races. When off, gaps only update at split points."},
            {"standings.animate_positions", "Animate position changes. Off: none; Basic: slide rows; Colored: slide + green/red tint."},
            {"standings.filter_dns", "Hide riders who did not start (DNS) from standings, position counts, and other displays."},
            {"standings.session_info", "Show a session-info row below the title (session, time remaining, leader's lap, or overtime label)."},
            {"standings.headers", "Show a header row labeling each enabled column above the rider rows."},
            {"standings.rows", "Maximum rider rows to show. More rows use more screen space."},
            {"standings.col_tracked", "Show status icons: hazard warnings, blue flags, checkered flags, and tracked rider markers."},
            {"standings.col_pos", "Show position number column."},
            {"standings.col_posgain", "Show positions gained or lost since the race start (caret up/down with a count). Race sessions only."},
            {"standings.col_racenum", "Show race number column."},
            {"standings.col_name", "Rider name display. Off hides the column, Short shows 3-letter abbreviation, Long shows full name."},
            {"standings.col_bike", "Show bike model column."},
            {"standings.col_status", "Show session status indicator (PIT, DNS, DSQ, lap count, FIN)."},
            {"standings.col_penalty", "Show penalty indicator when riders have active penalties."},
            {"standings.col_bestlap", "Show best lap time column."},
            {"standings.col_lastlap", "Show last lap time column (each rider's most recent lap, cuts included)."},
            {"standings.gap_mode", "Off hides column. Player: your gap; Adjacent: riders ahead/behind; All: everyone."},
            {"standings.gap_reference", "Gap reference: Leader shows gaps to P1, Player relative to you, Auto alternates the two."},

            {"ideal_lap.sectors", "Show your fastest individual sector times (S1, S2, S3)."},
            {"ideal_lap.laps", "Show your last lap, best lap, and calculated ideal lap times."},

            {"lap_log.rows", "Number of recent laps to show."},
            {"lap_log.order", "Display order. Oldest shows oldest laps at top, Newest shows newest at top."},
            {"lap_log.gap_row", "Show live gap to personal best below the current lap time."},
            {"lap_log.headers", "Show a header row labeling each column above the lap rows."},
            {"lap_log.col_sectors", "Show sector split times (S1, S2, S3)."},

            {"friends.rows", "Maximum number of friend rows to show."},
            {"friends.headers", "Show a header row labeling each enabled column."},
            {"friends.showmode", "When to show: Always; Friends (one is in-game); or On Join (briefly). Visible = master on/off."},
            {"friends.self", "Add your own presence as a top row (labeled You) to confirm what you're broadcasting."},
            {"friends.col_server", "The friend's server name (blank when offline)."},
            {"friends.col_track", "The friend's current track."},
            {"friends.col_info", "Session, format and state (e.g. Race 2, In Progress), or In Menus / Unknown between sessions."},
            {"friends.col_timer", "The friend's session clock: MM:SS, or N TO GO / FINAL LAP / CHECKERED. A coarse snapshot, not live."},

            {"lap_consistency.style", "Display mode. Graphs shows bar chart, Numbers shows statistics only, Both shows both."},
            {"lap_consistency.reference", "Baseline for comparison. Bars show delta to this reference time."},
            {"lap_consistency.lap_count", "Number of recent laps to include in the chart and statistics."},
            {"lap_consistency.trend_mode", "Trend line overlay. Off=none, Line=connected dots, Average=3-lap moving avg, Linear=regression."},
            {"lap_consistency.stat_ref", "Show reference time row. Displays the baseline time for comparison."},
            {"lap_consistency.stat_best", "Show best lap time from the displayed laps."},
            {"lap_consistency.stat_avg", "Show average lap time across the displayed laps."},
            {"lap_consistency.stat_worst", "Show worst (slowest) lap time from the displayed laps."},
            {"lap_consistency.stat_last", "Show your most recent lap time."},
            {"lap_consistency.stat_stddev", "Show standard deviation (+/-) indicating lap time spread."},
            {"lap_consistency.stat_trend", "Show trend indicator (Faster/Stable/Slower) based on recent laps."},
            {"lap_consistency.stat_cons", "Show consistency score (%). Higher is more consistent."},

            {"telemetry.display", "Display mode. Graphs shows visual meters, Numbers shows text values, Both shows both."},
            {"telemetry.throttle", "Show throttle input percentage."},
            {"telemetry.front_brake", "Show front brake input percentage."},
            {"telemetry.rear_brake", "Show rear brake input percentage."},
            {"telemetry.clutch", "Show clutch input percentage."},
            {"telemetry.rpm", "Show engine RPM."},
            {"telemetry.front_susp", "Show front suspension travel."},
            {"telemetry.rear_susp", "Show rear suspension travel."},
            {"telemetry.gear", "Show current gear number."},

            {"performance.display", "Display mode. Graphs shows visual meters, Numbers shows text values, Both shows both."},
            {"performance.fps", "Show frames per second."},
            {"performance.cpu", "Show plugin execution time in milliseconds."},
            {"performance.benchmark", "Profiler overlay with per-callback and per-HUD timing. Exports a report when toggled off."},

            {"pitboard.show_mode", "When to show. Always: constantly; At Pit: near pit area; At Splits: after timing points."},
            {"pitboard.rider", "Show rider name row."},
            {"pitboard.session", "Show session information row."},
            {"pitboard.position", "Show race position row."},
            {"pitboard.time", "Show elapsed session time row."},
            {"pitboard.lap", "Show lap number row."},
            {"pitboard.last_lap", "Show last lap time row."},
            {"pitboard.gap", "Show gap comparison row. Displays time difference based on gap compare mode."},
            {"pitboard.gap_compare", "Auto: leader in races, session PB solo. Overall: any rider's best. Record: from Records HUD."},

            {"session.icons", "Show icons next to each row. Off displays text only."},
            {"session.track", "Show current track name."},
            {"session.format", "Show session format (time/laps) and current state."},
            {"session.server", "Show the server name (online) / 'Testing' (offline) as the headline row."},
            {"session.weather", "Show current weather conditions."},

            {"records.provider", "Data source for leaderboard records. CBR or MXB Ranked."},
            {"records.count", "Number of records to show from the leaderboard."},
            {"records.autofetch", "Automatically fetch records when entering an event."},
            {"records.col_sectors", "Show sector time columns (S1, S2, S3). Only available with MXB Ranked provider."},
            {"records.col_date", "Show date when the record was set."},
            {"records.headers", "Show a header row labeling each column above the records."},

            {"timing.label", "Show the label column (Split 1, Split 2, Lap)."},
            {"timing.time", "Show the time column with split/lap times."},
            {"timing.gap", "Gap comparison for primary row. Off hides the gap column, or select a comparison type."},
            {"timing.show", "At Splits shows timing only after crossing a split point. Always shows timing constantly."},
            {"timing.freeze", "How long to display gap values after crossing a split. Off shows no gaps."},
            {"timing.show_reference", "Show reference times next to gaps."},
            {"timing.layout", "Vertical stacks elements in one column. Horizontal puts primary side-by-side, chips below."},
            {"timing.secondary_pb", "Show session personal best as secondary chip. Disabled when set as primary."},
            {"timing.secondary_alltime", "Show all-time personal best as secondary chip. Disabled when set as primary."},
            {"timing.secondary_ideal", "Show ideal lap (sum of best sectors) as secondary chip. Disabled when set as primary."},
            {"timing.secondary_overall", "Show overall best (any rider) as secondary chip. Disabled when set as primary."},
            {"timing.secondary_record", "Show fastest record from Records HUD as secondary chip. Disabled when set as primary."},
            {"timing.secondary_lastlap", "Show gap to your previous lap as secondary chip. Disabled when set as primary."},

            {"map.range", "Zoom level. Full shows entire track, or select a specific range in meters."},
            {"map.rotation", "When On, the map rotates so your heading is always up. When Off, the map stays fixed to track."},
            {"map.outline", "Show the track outline. Helps orientation; disable for more FPS on long tracks."},
            {"map.track_width", "Thickness of the track outline line."},
            {"map.detail", "Ribbon detail. Auto adapts to map size; High/Low force a fixed density."},
            {"map.colorize", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos."},
            {"map.rider_shape", "Icon shape for rider markers on the map. Off hides non-tracked riders."},
            {"map.marker_scale", "Size of rider markers relative to the map."},
            {"map.labels", "Labels next to riders. Position shows P1/P2, Race Num shows numbers, Both shows both."},

            {"radar.mode", "When to show. Off hides radar, On shows always, Auto-hide shows only when riders are nearby."},
            {"radar.range", "Detection radius in meters. Riders outside this range are not shown."},
            {"radar.colorize", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos."},
            {"radar.rider_shape", "Icon shape for rider markers on the radar."},
            {"radar.marker_scale", "Size of rider markers on the radar."},
            {"radar.labels", "Labels next to riders. Position shows P1/P2, Race Num shows numbers, Both shows both."},
            {"radar.proximity_arrows", "Directional arrows for nearby riders. Edge shows on screen edge, Circle shows around center."},
            {"radar.alert_distance", "Distance at which proximity arrows appear."},
            {"radar.proximity_color", "Color mode for proximity arrows. Distance fades with proximity, Position uses relative colors."},
            {"radar.proximity_shape", "Icon shape for proximity warning arrows."},
            {"radar.proximity_scale", "Size of proximity warning arrows."},

            {"gap_bar.freeze", "How long to freeze the gap bar display after a change."},
            {"gap_bar.show_gap", "Show gap time text in the center of the bar."},
            {"gap_bar.show_gap_bar", "Show the green/red gap visualization bars."},
            {"gap_bar.range", "Time range displayed on the gap bar in seconds."},
            {"gap_bar.width", "Width multiplier for the gap bar."},
            {"gap_bar.marker_mode", "Ghost shows your best lap marker. Opponents shows other riders. Both shows all."},
            {"gap_bar.marker_colors", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos."},
            {"gap_bar.icon", "Icon shape used for rider markers on the bar."},
            {"gap_bar.marker_scale", "Size multiplier for rider markers."},
            {"gap_bar.labels", "Labels shown next to markers. Position, race number, or both."},

            {"general.controller", "Select which controller to use for gamepad widget and rumble effects."},
            {"appearance.compact_times", "Shorter time format. Drops leading 0: for times under a minute and uses tenths for gaps."},
            {"appearance.speed_unit", "Unit for speed display. mph or km/h."},
            {"appearance.fuel_unit", "Unit for fuel display. Liters or Gallons."},
            {"appearance.temp_unit", "Unit for temperature display. Celsius or Fahrenheit."},
            {"appearance.clock_format", "Clock time format. 24h uses 00:00-23:59, 12h uses 1:00-12:59 AM/PM."},
            {"general.grid_snap", "When enabled, HUDs snap to a grid when dragging."},
            {"general.screen_clamp", "When enabled, HUDs are clamped to stay within screen bounds when dragging."},
            {"general.menu_only_cursor", "Cursor shows only in the menu; reopen via Toggle Settings hotkey. For controllers seen as a mouse."},
            {"general.auto_save", "When enabled, settings save automatically. Disable to edit the INI manually and reload."},
            {"general.steam_friends", "Broadcast your status and see friends running the plugin. Needs the Steam build; on by default."},
            {"general.discord", "Display your track, session, and lap progress in Discord's Rich Presence."},
            {"general.analytics", "Send an anonymous usage ping at startup (random ID, no personal data) so the dev can count users. On by default."},
            {"general.web_server", "Web overlay for OBS. Add as Browser Source at the displayed port."},
            {"general.web_port", "Port for the web server. Change if the default is in use by another application."},
            {"general.auto_switch", "Automatically switch profiles based on session type."},
            {"general.copy_profile", "Copy current profile settings to another profile."},
            {"general.reset_profile", "Reset current profile to default settings."},
            {"general.reset_all", "Reset all profiles and settings to defaults."},
            {"appearance.drop_shadow", "Add a shadow behind all text for improved readability."},
            {"appearance.hud_icons", "Show identity icons in the UI: beside HUD titles, on the settings tabs, and on the settings button."},
            {"general.pb_scope", "Bike = PB per bike. Category = PB across all bikes in the same class."},

            {"updates.check_enabled", "Check for updates when the game starts."},
            {"updates.debug_mode", "Test update installation without overwriting the real plugin."},
            {"updates.channel", "Stable only shows final releases. Prerelease includes test builds."},

            {"appearance.font_title", "Font used for HUD titles."},
            {"appearance.font_normal", "Font used for normal text."},
            {"appearance.font_strong", "Font used for column headers and row/legend labels across HUDs."},
            {"appearance.font_digits", "Font used for numeric values (times, gaps, telemetry)."},
            {"appearance.font_marker", "Font used for pitboard handwritten text."},
            {"appearance.font_small", "Font used for small text and footnotes."},
            {"appearance.color_primary", "Main text color for values, times, and key data."},
            {"appearance.color_secondary", "Supporting text for live data, telemetry values, and descriptions."},
            {"appearance.color_tertiary", "Subtle labels, axis markers, and category names."},
            {"appearance.color_muted", "Placeholders, grid lines, and unavailable data."},
            {"appearance.color_background", "Background color for HUD panels."},
            {"appearance.color_accent", "Interactive elements, hover highlights, and the player/spectated rider's name in standings."},
            {"appearance.color_positive", "Faster times, improvements, and gains."},
            {"appearance.color_neutral", "Color indicating neutral values (no change)."},
            {"appearance.color_warning", "Caution indicators, low fuel, and proximity alerts."},
            {"appearance.color_negative", "Slower times, losses, and critical alerts."},

            {"rumble.enabled", "Enable or disable controller vibration for all events."},
            {"rumble.stack", "When enabled, multiple rumble effects combine. When off, only the strongest effect plays."},
            {"rumble.crashed", "Continue rumble effects while the rider is crashed."},
            {"rumble.effect_profile", "Use shared settings (Global) or separate settings per bike."},
            {"rumble.bumps", "Vibration from suspension travel over bumps. Min/Max set the suspension velocity range in m/s."},
            {"rumble.slide", "Vibration when the bike slides sideways. Min/Max set the slide angle range in degrees."},
            {"rumble.spin", "Vibration from rear wheel spin. Min/Max set the wheel slip ratio range."},
            {"rumble.lockup", "Vibration when brakes lock up. Min/Max set the wheel slip ratio range."},
            {"rumble.wheelie", "Vibration during wheelies. Min/Max set the pitch angle range in degrees."},
            {"rumble.steer", "Vibration from steering forces. Min/Max set the steering torque range in Newton-meters."},
            {"rumble.rpm", "Vibration synchronized with engine RPM. Min/Max set the RPM range."},
            {"rumble.surface", "Vibration from rough terrain. Min/Max set the vehicle speed range when off-track."},
            {"rumble.revlimiter", "Vibration near the rev limiter. Min/Max set the range as % of limiter RPM. Muted off-throttle."},
            {"rumble.pitlimiter", "Vibration while the pit-lane speed limiter is active. Light/Heavy set the intensity."},
            {"rumble.split", "Tune front/rear wheels separately. They start from the combined value, then stay independent."},

            {"widgets.lap", "Shows current lap number and total laps in the session."},
            {"widgets.position", "Shows your current race position (P1, P2, etc.)."},
            {"widgets.time", "Shows elapsed session time or time remaining."},
            {"widgets.speed", "Shows current ground speed in mph or km/h."},
            {"widgets.gear", "Shows current gear with shift indicator that turns red at shift point."},
            {"widgets.speedo", "Analog speedometer gauge showing current speed, with odometer and trip meter."},
            {"widgets.tacho", "Tachometer gauge showing engine RPM."},
            {"widgets.bars", "Vertical bars showing throttle, brake, clutch, RPM, suspension, and fuel levels."},
            {"widgets.fuel", "Shows remaining fuel level and estimated laps."},
            {"widgets.gamepad", "Visual representation of controller button inputs."},
            {"widgets.clock", "Shows current local time. Supports 12h/24h format and optional UTC display."},
            {"widgets.lean", "Shows rider lean angle in degrees with arc gauge."},
            {"widgets.gforce", "G-G diagram plotting lateral vs longitudinal G-force with a peak marker."},
            {"widgets.compass", "Compass dial showing the bike's heading with N/E/S/W labels (classic needle or modern rotating-card style)."},
            {"widgets.pointer", "Mouse pointer for interacting with HUD elements."},
            {"widgets.tyre_temp", "Shows front and rear tyre temperatures."},
            {"widgets.ecu", "Shows ECU aids: map, traction, engine braking, anti-wheeling. Chips brighten on intervention."},
            {"widgets.version", "Shows plugin version number."},
            {"widgets.settings_button", "Opens the settings menu. Hide it if you prefer to use a hotkey."},

            {"hotkeys.settings", "Toggle the settings menu on/off."},
            {"hotkeys.standings", "Toggle the standings HUD on/off."},
            {"hotkeys.map", "Toggle the track map HUD on/off."},
            {"hotkeys.radar", "Toggle the radar HUD on/off."},
            {"hotkeys.lap_log", "Toggle the lap log HUD on/off."},
            {"hotkeys.ideal_lap", "Toggle the ideal lap HUD on/off."},
            {"hotkeys.telemetry", "Toggle the telemetry HUD on/off."},
            {"hotkeys.input", "Toggle the input display HUD on/off."},
            {"hotkeys.records", "Toggle the records HUD on/off."},
            {"hotkeys.pitboard", "Toggle the pitboard HUD on/off."},
            {"hotkeys.timing", "Toggle the timing HUD on/off."},
            {"hotkeys.gap_bar", "Toggle the gap bar HUD on/off."},
            {"hotkeys.performance", "Toggle the performance HUD on/off."},
            {"hotkeys.rumble", "Toggle the rumble HUD on/off."},
            {"hotkeys.widgets", "Toggle all widgets on/off."},
            {"hotkeys.fmx", "Toggle the FMX HUD on/off."},
            {"hotkeys.lap_consistency", "Toggle the lap consistency HUD on/off."},
            {"hotkeys.session", "Toggle the session HUD on/off."},
            {"hotkeys.notices", "Toggle the notices HUD on/off."},
            {"hotkeys.segment_add", "Drop a segment boundary: 2 points time a section, close the loop for a full lap."},
            {"hotkeys.segment_remove", "Remove the last segment boundary. With none left, the Timing HUD shows normal split/lap timing."},
            {"hotkeys.stats", "Toggle the stats HUD on/off."},
            {"hotkeys.all_huds", "Toggle all HUDs on/off."},
            {"hotkeys.reload", "Reload configuration from disk."},
            {"hotkeys.event_log", "Toggle the event log HUD on/off."},
            {"hotkeys.helmet", "Toggle the helmet overlay on/off."},
            {"hotkeys.friends", "Toggle the friends HUD on/off."},
            {"hotkeys.overlay_last_lap", "Web overlay: force the fastest-last-lap board to slide in now (momentary; normal rotation resumes after)."},
            {"hotkeys.overlay_fastest_lap", "Web overlay: force the session-best lap board to slide in now (momentary; normal rotation resumes after)."},
            {"hotkeys.overlay_down_order", "Web overlay: force the down-the-order (backmarkers) carousel to slide in now (momentary; normal rotation resumes after)."},
            {"hotkeys.overlay_battle", "Web overlay: force the closest battle to slide in now (momentary; shows 'No data' if no battle is active)."},

            {"stats.visibility_mode", "When to show. Always displays on track, On Finish appears after crossing the finish line."},
            {"stats.show_lap", "Show last completed lap column with per-lap stats."},
            {"stats.show_session", "Show current session column with accumulated session stats."},
            {"stats.show_alltime", "Show all-time column with lifetime stats for this track and bike."},

            {"fmx.chain_rows", "Tricks shown in the stack. Off hides it, 1 shows the active trick, 2+ shows history."},
            {"fmx.row_trick_stats", "Show duration, distance, and peak rotation below the active trick name."},
            {"fmx.row_combo_arc", "Show combo arc with chain multiplier and score breakdown."},
            {"fmx.row_arcs", "Show pitch, yaw, and roll rotation arcs with start and peak markers."},
            {"fmx.debug_logging", "Log trick detection state to the plugin log file at 10fps (developer mode only)."},
            {"fmx.row_debug_values", "Show raw rotation values for pitch, yaw, and roll (developer mode only)."},

            {"notices.wrong_way", "Show warning when riding in the wrong direction."},
            {"notices.blue_flag", "Show notice when a faster rider is approaching to pass. Race sessions only."},
            {"notices.lapping", "Show notice when you are closing on a rider ahead who is a lap or more down. Race sessions only."},
            {"notices.overtime", "Show notice when the session timer expires in a time+laps race and extra laps begin."},
            {"notices.last_lap", "Show notice when starting the final lap of a race."},
            {"notices.finished", "Show finishing position when crossing the line (e.g. FINISHED 1ST). Updates with penalties."},
            {"notices.alltime_pb", "Show notice when setting an all-time personal best lap."},
            {"notices.fastest_lap", "Show notice when setting the fastest lap in an online race."},
            {"notices.session_pb", "Show notice when setting a session personal best lap."},
            {"notices.hazard_stationary", "Show warning when a stationary rider is on the track ahead."},
            {"notices.hazard_wrong_way", "Show warning when a rider going the wrong way is on the track ahead."},
            {"notices.default_setup", "Show warning when entering the track with the default bike setup."},
            {"notices.duration", "How long timed notices stay up. Wrong-way, blue-flag and hazard notices last while active."},

            {"event_log.icons", "Show small icons next to each event indicating the event type."},
            {"event_log.display_mode", "Off hides the log. On always shows it. Auto-hide shows it briefly when new events arrive."},
            {"event_log.duration", "How long the log stays visible after the last event in auto-hide mode."},
            {"event_log.order", "Oldest shows events chronologically top to bottom. Newest puts latest events at the top."},
            {"event_log.max_events", "Maximum number of event rows to display. The log scrolls when this limit is reached."},
            {"event_log.timestamp", "Session shows the session timer value. Clock shows your local time."},
            {"event_log.session", "Log session lifecycle: started (with format), pre-start, ended, cancelled, complete."},
            {"event_log.fastest_lap", "Log when a rider sets the overall fastest lap of the session."},
            {"event_log.penalties", "Log when a rider receives a penalty, or when a penalty is cleared or changed."},
            {"event_log.rider_out", "Log when a rider retires, is disqualified, or does not start."},
            {"event_log.overtime", "Log when a timed race enters overtime, or when a non-race session timer expires."},
            {"event_log.final_lap", "Log when a rider starts their final lap. Race sessions only."},
            {"event_log.leader_change", "Log when a different rider takes the lead position. Race sessions only."},
            {"event_log.finished", "Log when a rider crosses the finish line with their finishing position."},
            {"event_log.pit", "Log when a rider enters or exits the pits. Can be frequent in practice sessions."},

            {"helmet.helmet_enabled", "Enable the helmet overlay."},
            {"helmet.upper_tex", "Upper helmet texture (visor rim and top)."},
            {"helmet.lower_tex", "Lower helmet texture (chin bar)."},
            {"helmet.upper_offset", "Shift the upper part up or down."},
            {"helmet.lower_offset", "Shift the lower part up or down."},
            {"helmet.tilt", "How much the helmet tilts with lean angle."},
            {"helmet.vibration", "How far the helmet moves when hitting bumps. Negative inverts the direction."},
            {"helmet.vib_sensitivity", "How responsive vibration is to small bumps."},
            {"helmet.zoom", "Scale the helmet. Zoom in to see less frame."},
            {"helmet.visor_mode", "Off = no tint. Visor = tint behind helmet (full-face). Goggles = tint in front (open-face)."},
            {"helmet.visor_tint_opacity", "Strength of the tint."},
            {"helmet.visor_tint_color", "Color of the tint."},
        };
        return m;
    }
};
