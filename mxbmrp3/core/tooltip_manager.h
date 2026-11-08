// ============================================================================
// core/tooltip_manager.h
// Manages tooltips for settings UI elements.
// Strings are compiled into the plugin (no external file).
//
// LENGTH LIMIT: tooltips render as at most 2 word-wrapped lines in the settings
// panel (LayoutMetrics::settingsTooltipCharsPerLine() chars each -- 50 with a panel
// theme, 51 without, and the guard measures the narrower one);
// anything that does not fit is cut off, with "..." when the last line has room
// for it. Don't count characters by hand — tests/unit/test_tooltip_length.cpp
// runs every entry below through the renderer's own wrap (TextWrap::wrap) and
// fails on any that truncates, so a too-long tooltip is a red unit build rather
// than something you find in-game. Note the raw character count is only a rough
// guide: where the line breaks fall matters, so a 117-char description can
// overflow while a 105-char one does not.
//
// TRANSLATED at the getters: each text is marked NC_("tooltip", ...) for the
// translation template (core/i18n.h), under the "tooltip" context so a
// translator sees which strings must fit the two-line box, and resolved through
// I18n::trc() when shown -- a hover or the Find index, never per frame. The
// same length limit applies to a translation; a shipped language pack is
// checked against it like the English is.
// ============================================================================
#pragma once

#include "i18n.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
        return (it != m.end()) ? I18n::trc("tooltip", it->second) : "";
    }

    // Get control tooltip by control ID (e.g., "common.visible", "standings.rows").
    // A DYNAMIC entry (below) wins over the compiled table.
    const char* getControlTooltip(const char* controlId) const {
        auto dyn = m_dynamic.find(controlId);
        if (dyn != m_dynamic.end()) return dyn->second.c_str();
        const auto& m = controls();
        auto it = m.find(controlId);
        return (it != m.end()) ? I18n::trc("tooltip", it->second) : "";
    }

    // A tooltip whose text lives with its DATA rather than in the table above:
    // the achievements tab registers one per catalogue row ("achievements.<id>")
    // from Achievements::Entry::tooltip, so the row and its hover are one edit.
    // Called from a settings rebuild, not per frame; the length limit above still
    // applies (test_achievements.cpp wraps them).
    void setDynamicControlTooltip(const char* controlId, const char* text) {
        m_dynamic[controlId] = text;
    }

    // Enumerate every tooltip (tab + control) as {id, text}. For tests/tooling —
    // e.g. the unit test that enforces the LENGTH LIMIT documented above.
    static std::vector<std::pair<std::string, std::string>> allTooltips() {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& kv : tabs())     out.emplace_back(kv.first, kv.second);
        for (const auto& kv : controls()) out.emplace_back(kv.first, kv.second);
        return out;
    }

private:
    TooltipManager() = default;
    ~TooltipManager() = default;
    TooltipManager(const TooltipManager&) = delete;
    TooltipManager& operator=(const TooltipManager&) = delete;

    std::unordered_map<std::string, std::string> m_dynamic;

    using Map = std::unordered_map<std::string, const char*>;

    static const Map& tabs() {
        static const Map m = {
            {"general", NC_("tooltip", "Global settings including unit preferences, profiles, and configuration options.")},
            {"appearance", NC_("tooltip", "Customize the visual style of all HUD elements including fonts and colors.")},
            {"hotkeys", NC_("tooltip", "Keyboard and controller bindings. Click a field to rebind it (ESC cancels), right-click to clear it.")},
            {"riders", NC_("tooltip", "Track specific riders with custom colors and icons. Highlighted on standings, map, and radar.")},
            {"riders.color", NC_("tooltip", "The rider's color on standings, map, and radar.")},
            {"riders.icon", NC_("tooltip", "The rider's icon on the map and radar.")},
            {"rumble", NC_("tooltip", "Configure controller vibration for racing events like bumps, wheelspin, and impacts.")},
            {"updates", NC_("tooltip", "Automatic update checking and installation. Downloads and installs new versions from GitHub.")},
            {"about", NC_("tooltip", "What this plugin is, where it came from, and who is behind it.")},
            {"more", NC_("tooltip", "The less-used HUDs and tools. Click a name to open its settings.")},
            {"more.back", NC_("tooltip", "Back to the More page.")},
            {"find", NC_("tooltip", "Settings whose name, tab or description holds every word you typed. Click one to open it.")},
            {"standings", NC_("tooltip", "Live race standings showing position, gaps, and rider information for all competitors.")},
            {"map", NC_("tooltip", "Overhead view of the track with real-time rider positions. Supports zoom and display modes.")},
            {"radar", NC_("tooltip", "Shows nearby riders relative to your position. Useful for close racing awareness.")},
            {"lap_log", NC_("tooltip", "History of your recent lap times with sector breakdowns. Shows lap-by-lap performance.")},
            {"friends", NC_("tooltip", "Steam friends in the same game: where they are, badged when on your server and track.")},
            {"session_charts", NC_("tooltip", "Progression charts: running order, race trace, gap to leader, and pace.")},
            {"ideal_lap", NC_("tooltip", "Displays your theoretical best lap time calculated from your fastest individual sectors.")},
            {"telemetry", NC_("tooltip", "Real-time input data including throttle, brake, clutch, RPM, suspension travel, and gear.")},
            {"performance", NC_("tooltip", "System performance metrics including frame rate and plugin execution time.")},
            {"records", NC_("tooltip", "Lap records from online providers (CBR, MXB-Ranked) for your current track.")},
            {"pitboard", NC_("tooltip", "Info board at pit area or timing points. Shows position, lap times, and gap comparisons.")},
            {"session", NC_("tooltip", "Session information including track, server details, player count, and password display.")},
            {"timing", NC_("tooltip", "Lap and sector timing display with comparisons to session best and personal records.")},
            {"dash", NC_("tooltip", "One card that cycles through the pages picked below.")},
            {"delta_trace", NC_("tooltip", "Your gap to a reference lap across the whole lap: green where you gained, red where you lost.")},
            {"gap_bar", NC_("tooltip", "Visual progress bar showing your gap to the rider ahead and behind.")},
            {"widgets", NC_("tooltip", "Simple HUD elements showing speed, position, lap count, session time, and other info.")},
            {"notices", NC_("tooltip", "On-screen notices for race events, personal bests, and setup warnings.")},
            {"event_log", NC_("tooltip", "Scrolling feed of race events: session changes, fastest laps, penalties, and finishes.")},
            {"fmx", NC_("tooltip", "Freestyle trick detection with scoring, chain combos, and rotation visualization.")},
            {"stats", NC_("tooltip", "Per-track statistics including lap counts, crashes, gear shifts, top speed, and riding time.")},
            {"achievements", NC_("tooltip", "Lifetime milestones as Bronze-to-Platinum progress bars, plus a toast when a tier is earned.")},
            {"helmet", NC_("tooltip", "First-person helmet overlay with lean tilt and vibration.")},
            {"director", NC_("tooltip", "Auto-director for spectating: follows the most interesting rider - battles, passes, hot laps.")},
            {"spotter", NC_("tooltip", "Spoken race callouts - fastest laps, leader changes, penalties - with optional subtitles.")},
            {"stream_chat", NC_("tooltip", "Your Twitch and YouTube chat in game, merged in one window. Read-only, no login.")},
        };
        return m;
    }

    static const Map& controls() {
        static const Map m = {
            {"common.visible", NC_("tooltip", "Show or hide this element during gameplay.")},
            {"common.title", NC_("tooltip", "Toggle the title bar at the top of this HUD.")},
            {"pager.prev", NC_("tooltip", "Previous page.")},
            {"pager.next", NC_("tooltip", "Next page.")},
            {"find.field", NC_("tooltip", "Find a setting by its name, its tab, or a word from its description. Esc clears it.")},
            {"common.texture", NC_("tooltip", "Pick the artwork this element draws from. Elements whose artwork is mandatory have no Off option.")},
            {"common.opacity", NC_("tooltip", "Background transparency. 0% is fully transparent, 100% is fully opaque.")},
            {"common.scale", NC_("tooltip", "Size multiplier: make this HUD bigger or smaller. Affects all its text and graphics.")},
            {"common.theme", NC_("tooltip", "Panel theme for this element only. Default follows the global theme.")},
            {"appearance.theme", NC_("tooltip", "Panel frame, title band and card art shared by every element.")},
            {"appearance.language", NC_("tooltip", "Language of the menus and HUDs. System follows Windows; untranslated text stays English.")},
            {"gamepad.pack", NC_("tooltip", "Controller artwork and the button positions that go with it.")},
            {"pitboard.pack", NC_("tooltip", "Pit board artwork and the row positions that go with it.")},
            {"gauges.pack", NC_("tooltip", "Dial faces and the ranges that place the needles on them.")},

            {"standings.live_updates", NC_("tooltip", "In races. Off: at the splits; Gaps: live gaps; Both: live gaps and order, also on the Position widget and Dash.")},
            {"standings.animate_positions", NC_("tooltip", "Animate position changes. Off: none; Basic: slide rows; Colored: slide + green/red tint.")},
            {"standings.filter_dns", NC_("tooltip", "Hide riders who did not start (DNS) from standings, position counts, and other displays.")},
            {"standings.session_info", NC_("tooltip", "Show a session-info row below the title (session, time remaining, leader's lap, or overtime label).")},
            {"standings.headers", NC_("tooltip", "Show a header row labeling each enabled column above the rider rows.")},
            {"standings.show_mode", NC_("tooltip", "When to show. Always: constantly; At laps: after the line; At splits: after each split and the line.")},
            {"standings.freeze", NC_("tooltip", "At laps and At splits: how long the standings show after each crossing.")},
            {"standings.rows", NC_("tooltip", "Maximum rider rows to show. More rows use more screen space.")},
            {"standings.top_positions", NC_("tooltip", "Always show the top N leaders, even when rows center on you (0 = off).")},
            {"standings.col_tracked", NC_("tooltip", "Show status icons: hazard warnings, blue flags, checkered flags, and tracked rider markers.")},
            {"standings.col_pos", NC_("tooltip", "Show position number column.")},
            {"standings.col_posgain", NC_("tooltip", "Show positions gained or lost since the race start (caret up/down with a count). Race sessions only.")},
            {"standings.col_racenum", NC_("tooltip", "Show race number column.")},
            {"standings.col_name", NC_("tooltip", "Rider name display. Off hides the column, Short shows 3-letter abbreviation, Long shows full name.")},
            {"standings.col_category", NC_("tooltip", "Show each rider's class (MX1, MX2 OEM...).")},
            {"standings.col_bike", NC_("tooltip", "Show bike model column.")},
            {"standings.col_penalty", NC_("tooltip", "Show penalty indicator when riders have active penalties.")},
            {"standings.col_bestlap", NC_("tooltip", "Show best lap time column.")},
            {"standings.col_lastlap", NC_("tooltip", "Show last lap time column (each rider's most recent lap, cuts included).")},
            {"standings.gap_mode", NC_("tooltip", "Off hides column. Player: your gap; Adjacent: riders ahead/behind; All: everyone.")},
            {"standings.gap_reference", NC_("tooltip", "Gap reference: Leader shows gaps to P1, Player relative to you, Auto alternates the two.")},

            {"dash.page_fuel", NC_("tooltip", "The Fuel widget's rows: your fuel, use per lap and laps left, beside a bar showing how full your tank is.")},
            {"dash.page_telemetry", NC_("tooltip", "The Telemetry HUD's inputs as traces across the card.")},
            {"dash.page_deltatrace", NC_("tooltip", "The Delta Trace HUD's chart: your gap to its reference lap across the lap.")},
            {"dash.page_gapchart", NC_("tooltip", "The Session Charts HUD's Gap chart: the leader, you and the riders around you.")},
            {"dash.page_eventlog", NC_("tooltip", "The Event Log's three newest events, with its icons and event filter.")},
            {"dash.page_nowplaying", NC_("tooltip", "The Now Playing widget's track, controls and volume. Needs Now playing in General.")},
            {"dash.page_session", NC_("tooltip", "The Session HUD's rows: server, track and format.")},
            {"dash.page_names", NC_("tooltip", "Shows a page's name for a moment as you step to it, then its content.")},
            {"dash.strip", NC_("tooltip", "Where the page icons sit around the card, or Off to hide them.")},
            {"dash.notice_takeover", NC_("tooltip", "A new notice takes the card over, drawn as on Notices, then your page comes back.")},
            {"dash.page_rpm", NC_("tooltip", "The RPM widget's shift lights, with the revs under them.")},
            {"dash.page_standings", NC_("tooltip", "Your row and the riders around it, drawn as on Standings.")},
            {"dash.page_timing", NC_("tooltip", "Your running lap time, with your gap to your reference lap under it.")},
            {"dash.page_gapbar", NC_("tooltip", "The Gap Bar's fill for your live gap to your reference lap, with your last and best lap.")},
            {"dash.page_flatmap", NC_("tooltip", "Every rider along the lap as the Gap Bar's markers, the race leader crowned.")},
            {"dash.page_time", NC_("tooltip", "The session clock: counts down in a timed session, up otherwise, then the laps to go.")},
            {"dash.page_laps", NC_("tooltip", "Your last three laps with their gap to your best, in the Lap Log's order.")},
            {"dash.page_ideal", NC_("tooltip", "Your last and best lap with their gap to your ideal lap, and the ideal itself.")},
            {"ideal_lap.sectors", NC_("tooltip", "Show your fastest individual sector times (S1, S2, S3).")},
            {"ideal_lap.laps", NC_("tooltip", "Show your last lap, best lap, and calculated ideal lap times.")},

            {"lap_log.rows", NC_("tooltip", "Number of recent laps to show.")},
            {"performance.graph_rows", NC_("tooltip", "Height of each graph, in text rows. Resizes the panel without scaling its text.")},
            {"telemetry.graph_rows", NC_("tooltip", "Height of the graph, in text rows. Resizes the panel without scaling its text.")},
            {"lap_log.order", NC_("tooltip", "Display order. Oldest shows oldest laps at top, Newest shows newest at top.")},
            {"lap_log.gap_row", NC_("tooltip", "Show live gap to personal best below the current lap time.")},
            {"lap_log.reference", NC_("tooltip", "Lap the gap row is measured against. All-time PB is saved; Server best is anyone's fastest. Default follows General.")},
            {"lap_log.freeze", NC_("tooltip", "How long the gap row holds the official gap after each split and the line. Default follows General.")},
            {"lap_log.headers", NC_("tooltip", "Show a header row labeling each column above the lap rows.")},
            {"lap_log.col_sectors", NC_("tooltip", "Show sector split times (S1, S2, S3).")},

            {"friends.rows", NC_("tooltip", "Maximum number of friend rows to show.")},
            {"friends.headers", NC_("tooltip", "Show a header row labeling each enabled column.")},
            {"friends.showmode", NC_("tooltip", "When to show: Always; Friends (one is in-game); or On join (briefly). Visible = master on/off.")},
            {"friends.self", NC_("tooltip", "Add your own presence as a top row (labeled You) to confirm what you're broadcasting.")},
            {"friends.col_server", NC_("tooltip", "The friend's server name (blank when offline).")},
            {"friends.col_track", NC_("tooltip", "The friend's current track.")},
            {"friends.col_info", NC_("tooltip", "Session, format and state (e.g. Race 2, In Progress), or In Menus / Unknown between sessions.")},
            {"friends.col_timer", NC_("tooltip", "The friend's session clock: MM:SS, or N TO GO / FINAL LAP / CHECKERED. Coarse snapshot, not live.")},

            {"session_charts.chart_lap", NC_("tooltip", "Lap chart: running order over the laps, one row per rider. Shows overtakes.")},
            {"session_charts.chart_trace", NC_("tooltip", "Race trace: cumulative time vs a fixed reference pace. Race only.")},
            {"session_charts.chart_gap", NC_("tooltip", "Gap chart: seconds behind the leader (best lap in practice/qual).")},
            {"session_charts.chart_pace", NC_("tooltip", "Pace chart: each rider's lap time per lap. Any session.")},
            {"session_charts.colors", NC_("tooltip", "Rider line colours: a hue per position, or bike brand colour.")},
            {"session_charts.top_n", NC_("tooltip", "Always show the top N leaders, even when rows center on you (0 = off).")},
            {"session_charts.rows", NC_("tooltip", "Total rider lines shown: the top-N plus a window centered on you.")},
            {"session_charts.graph_rows", NC_("tooltip", "Height of EACH chart, in text rows -- how tall the panel is, not how much data it holds.")},
            {"session_charts.grid", NC_("tooltip", "Show grid lines behind the chart, with a line at each lap.")},
            {"session_charts.axis_labels", NC_("tooltip", "Show axis labels: values on the left, lap numbers on the bottom.")},
            {"session_charts.legend", NC_("tooltip", "Label each line with the rider's race number at its end.")},
            {"session_charts.zero_line", NC_("tooltip", "Show the dashed reference line (the trace's reference pace).")},
            {"session_charts.dots", NC_("tooltip", "Draw a dot at each lap's point along every line.")},
            {"session_charts.filter_outliers", NC_("tooltip", "Pace chart: hide the opening lap, invalid laps, and slow outliers.")},
            {"session_charts.sector_points", NC_("tooltip", "Plot a point per sector instead of per lap on the lap, trace and gap charts. Races only.")},

            {"telemetry.display", NC_("tooltip", "Display mode. Graphs shows visual meters, Numbers shows text values, Both shows both.")},
            {"telemetry.throttle", NC_("tooltip", "Show throttle input percentage.")},
            {"telemetry.front_brake", NC_("tooltip", "Show front brake input percentage.")},
            {"telemetry.rear_brake", NC_("tooltip", "Show rear brake input percentage.")},
            {"telemetry.clutch", NC_("tooltip", "Show clutch input percentage.")},
            {"telemetry.rpm", NC_("tooltip", "Show engine RPM.")},
            {"telemetry.front_susp", NC_("tooltip", "Show front suspension travel.")},
            {"telemetry.rear_susp", NC_("tooltip", "Show rear suspension travel.")},
            {"telemetry.gear", NC_("tooltip", "Show current gear number.")},

            {"performance.display", NC_("tooltip", "Display mode. Graphs shows visual meters, Numbers shows text values, Both shows both.")},
            {"performance.fps", NC_("tooltip", "Show frames per second (FPS).")},
            {"performance.cpu", NC_("tooltip", "Time the plugin itself spends per frame, in ms. Not system CPU.")},
            {"performance.probe_sweep", NC_("tooltip", "Start or stop the draw-cost sweep. Writes a report when it ends.")},
            {"performance.benchmark", NC_("tooltip", "Plugin timing per callback and HUD. Saves a report on leaving the track, on quitting and when switched off.")},

            {"pitboard.show_mode", NC_("tooltip", "When to show. Always: constantly; At pit: near pit area; At splits: after timing points.")},
            {"pitboard.freeze", NC_("tooltip", "At splits: how long the board shows after each split and the line.")},
            {"pitboard.rider", NC_("tooltip", "Show rider name row.")},
            {"pitboard.session", NC_("tooltip", "Show session information row.")},
            {"pitboard.position", NC_("tooltip", "Show race position row.")},
            {"pitboard.time", NC_("tooltip", "Show elapsed session time row.")},
            {"pitboard.lap", NC_("tooltip", "Show lap number row.")},
            {"pitboard.last_lap", NC_("tooltip", "Show last lap time row.")},
            {"pitboard.gap", NC_("tooltip", "Show gap comparison row. Displays time difference based on gap compare mode.")},
            {"pitboard.gap_compare", NC_("tooltip", "Auto: leader in races, session PB solo. Server best: anyone's fastest. Record: from Records HUD.")},

            {"session.icons", NC_("tooltip", "Show icons next to each row. Off displays text only.")},
            {"session.track", NC_("tooltip", "Show current track name.")},
            {"session.format", NC_("tooltip", "Show session format (time/laps) and current state.")},
            {"session.server", NC_("tooltip", "Show the server name (online) / 'Testing' (offline) as the headline row.")},
            {"session.weather", NC_("tooltip", "Show current weather conditions.")},

            {"records.provider", NC_("tooltip", "Data source for leaderboard records. CBR or MXB Ranked.")},
            {"records.count", NC_("tooltip", "Number of records to show from the leaderboard.")},
            {"records.autofetch", NC_("tooltip", "Automatically fetch records when entering an event.")},
            {"records.col_sectors", NC_("tooltip", "Show sector time columns (S1, S2, S3). Only available with MXB Ranked provider.")},
            {"records.col_date", NC_("tooltip", "Show date when the record was set.")},
            {"records.headers", NC_("tooltip", "Show a header row labeling each column above the records.")},

            {"timing.time", NC_("tooltip", "Show the big time row at the top (current/frozen split or lap time).")},
            {"timing.gap", NC_("tooltip", "Show the live gap to a reference lap under the time, as the Gap Bar shows it. Default follows General.")},
            {"timing.show", NC_("tooltip", "At splits shows timing only after crossing a split point. Always shows timing constantly.")},
            {"timing.freeze", NC_("tooltip", "How long to hold the split/lap time and its gaps after a split. Off shows no gaps. Default follows General.")},
            // One entry for all seven readout toggles: they share a tooltip id
            // because they are one feature, and the row's own label already says
            // which reading it is.
            {"timing.readouts", NC_("tooltip", "Extra rows below the comparisons: position, lap, time left, session format, fuel, server and track.")},
            {"timing.target", NC_("tooltip", "Splits shows each row's time at the next split. Full lap shows its whole-lap time all the way round.")},
            {"timing.gap_pb", NC_("tooltip", "Add a comparison row against your session personal best.")},
            {"timing.gap_alltime", NC_("tooltip", "Add a comparison row against your all-time personal best.")},
            {"timing.gap_ideal", NC_("tooltip", "Add a comparison row against your ideal lap (sum of best sectors).")},
            {"timing.gap_overall", NC_("tooltip", "Add a comparison row against the best lap by anyone this session.")},
            {"timing.gap_record", NC_("tooltip", "Add a comparison row against the fastest record from the Records HUD.")},
            {"timing.gap_lastlap", NC_("tooltip", "Add a comparison row against your previous lap.")},

            {"map.mode", NC_("tooltip", "Overview shows the entire track. Follow shows the stretch around you, Range meters across.")},
            {"map.range", NC_("tooltip", "Zoom: how many meters of track Follow shows around you.")},
            {"map.range_adaptive", NC_("tooltip", "Follow shows more track the faster you go: Range when slow, up to twice it (500m at most) at speed.")},
            {"map.rotation", NC_("tooltip", "When On, the map rotates so your heading is always up. When Off, the map stays fixed to track.")},
            {"map.tilt", NC_("tooltip", "Lays the zoomed map down, seen from behind; higher tilts it further. Best with Rotate map on.")},
            {"map.outline", NC_("tooltip", "Track outline width; Off hides it. Outline doubles the map's quads - Off is fastest.")},
            {"map.lap_delta", NC_("tooltip", "Colours the track green where you gain and red where you lose. Default follows General.")},
            {"map.markers", NC_("tooltip", "Show S/F, sector markers and segment lines. Off leaves just the track and riders.")},
            {"map.track_width", NC_("tooltip", "Thickness of the track outline line.")},
            {"map.detail", NC_("tooltip", "Track detail 20-200%. Higher = smoother ribbon, more quads = more CPU/GPU cost.")},
            {"map.detail_adaptive", NC_("tooltip", "Adapt density to the on-screen map, so every track looks and costs about the same.")},
            {"map.colorize", NC_("tooltip", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos.")},
            {"map.rider_shape", NC_("tooltip", "Icon shape for rider markers on the map. Off hides non-tracked riders.")},
            {"map.marker_scale", NC_("tooltip", "Size of rider markers relative to the map.")},
            {"map.labels", NC_("tooltip", "Labels next to riders. Position shows P1/P2, Race number shows numbers, Both shows both.")},

            {"radar.mode", NC_("tooltip", "When to show. Off hides radar, On shows always, Auto-hide shows only when riders are nearby.")},
            {"radar.range", NC_("tooltip", "Detection radius in meters. Riders outside this range are not shown.")},
            {"radar.colorize", NC_("tooltip", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos.")},
            {"radar.rider_shape", NC_("tooltip", "Icon shape for rider markers on the radar.")},
            {"radar.marker_scale", NC_("tooltip", "Size of rider markers on the radar.")},
            {"radar.labels", NC_("tooltip", "Labels next to riders. Position shows P1/P2, Race number shows numbers, Both shows both.")},
            {"radar.proximity_arrows", NC_("tooltip", "Directional arrows for nearby riders. Edge shows on screen edge, Circle shows around center.")},
            {"radar.alert_distance", NC_("tooltip", "Distance at which proximity arrows appear.")},
            {"radar.proximity_color", NC_("tooltip", "Color mode for proximity arrows. Distance fades with proximity, Position uses relative colors.")},
            {"radar.proximity_shape", NC_("tooltip", "Icon shape for proximity warning arrows.")},
            {"radar.proximity_scale", NC_("tooltip", "Size of proximity warning arrows.")},
            {"delta_trace.graph_rows", NC_("tooltip", "Height of the graph, in text rows. Resizes the panel without scaling its text.")},
            {"delta_trace.reference", NC_("tooltip", "Lap the gap is measured against. All-time PB is saved; Server best is anyone's fastest. Default follows General.")},
            {"delta_trace.peaks", NC_("tooltip", "Marks where on the lap you were furthest ahead and furthest behind, with the gap there.")},
            {"gap_bar.reference", NC_("tooltip", "Lap the gap is measured against. All-time PB is saved; Server best is anyone's fastest. Default follows General.")},
            {"gap_bar.freeze", NC_("tooltip", "How long the gap holds the official gap after each split and the line. Off keeps it live. Default follows General.")},
            {"gap_bar.show_gap", NC_("tooltip", "Show gap time text in the center of the bar.")},
            {"gap_bar.show_gap_bar", NC_("tooltip", "Show the green/red gap visualization bars.")},
            {"gap_bar.splits", NC_("tooltip", "Ticks at the bar's edges where the track's splits are.")},
            {"gap_bar.range", NC_("tooltip", "Time range displayed on the gap bar in seconds. Auto fits the largest gap of the lap so far.")},
            {"gap_bar.width", NC_("tooltip", "Width multiplier for the gap bar.")},
            {"gap_bar.marker_mode", NC_("tooltip", "Ghost: your best lap marker. Opponents: other riders. Both: all of them. Off: no markers at all.")},
            {"gap_bar.marker_colors", NC_("tooltip", "Rider marker colors. Uniform uses same color, Brand uses bike colors, Position by relative pos.")},
            {"gap_bar.icon", NC_("tooltip", "Icon shape used for rider markers on the bar.")},
            {"gap_bar.marker_scale", NC_("tooltip", "Size multiplier for rider markers.")},
            {"gap_bar.labels", NC_("tooltip", "Labels shown next to markers. Position, race number, or both.")},

            {"general.controller", NC_("tooltip", "Select which controller to use for gamepad widget and rumble effects.")},
            {"appearance.compact_times", NC_("tooltip", "Drops the leading 0: for times under a minute. The web overlay also uses tenths for gaps.")},
            {"appearance.speed_unit", NC_("tooltip", "Unit for speed display. mph or km/h.")},
            {"appearance.fuel_unit", NC_("tooltip", "Unit for fuel display. Liters or Gallons.")},
            {"appearance.temp_unit", NC_("tooltip", "Unit for temperature display. Celsius or Fahrenheit.")},
            {"appearance.clock_format", NC_("tooltip", "Clock time format. 24h uses 00:00-23:59, 12h uses 1:00-12:59 AM/PM.")},
            {"general.grid_snap", NC_("tooltip", "When enabled, HUDs snap to a grid when dragging.")},
            {"general.direct_gl", NC_("tooltip", "Draw the HUD in the game's OpenGL context, not the engine. Off/on retries a Failed.")},
            {"general.screen_clamp", NC_("tooltip", "When enabled, HUDs are clamped to stay within screen bounds when dragging.")},
            {"general.auto_save", NC_("tooltip", "Auto-saves on leaving the track (never while riding); use Save anytime. Disable to edit INI.")},
            {"general.steam_friends", NC_("tooltip", "Broadcast your status and see friends running the plugin. Needs the Steam build; on by default.")},
            {"general.discord", NC_("tooltip", "Display your track, session, and lap progress in Discord's Rich Presence.")},
            {"general.now_playing", NC_("tooltip", "Music from Windows' media controls, for Now Playing. Spotify: turn on its desktop overlay for media keys.")},
            {"general.analytics", NC_("tooltip", "Anonymous usage survey: one ping at startup, no personal data. On by default.")},
            {"general.web_server", NC_("tooltip", "Web overlay for OBS. Add as Browser Source at the displayed port.")},
            {"general.web_port", NC_("tooltip", "Port for the web server. Change if the default is in use by another application.")},
            {"general.auto_switch", NC_("tooltip", "Automatically switch profiles based on session type.")},
            {"general.copy_profile", NC_("tooltip", "Copy current profile settings to another profile.")},
            {"general.copy_button", NC_("tooltip", "Overwrite the selected profile with this profile's settings.")},
            {"general.reset_profile", NC_("tooltip", "Reset current profile to default settings.")},
            {"general.reset_all", NC_("tooltip", "Reset all profiles and settings to defaults.")},
            {"appearance.drop_shadow", NC_("tooltip", "Add a shadow behind text and HUD icons for improved readability.")},
            {"appearance.ui_scale", NC_("tooltip", "Size of every HUD and this menu. Each HUD's own Scale is relative to it.")},
            {"appearance.hud_icons", NC_("tooltip", "Show icons on HUD titles, settings tabs and controls, and the Toggles strip. Off uses text like [x].")},
            {"appearance.motion", NC_("tooltip", "HUDs, notices and this menu fade and slide in and out, and the gear and odometer roll. Off draws them as before.")},
            {"appearance.glow", NC_("tooltip", "A soft halo behind the lights that call for action: RPM, gear at shift, warnings, Dash RPM and fuel.")},
            {"appearance.messages", NC_("tooltip", "Popup cards when a hotkey hides a HUD, a profile switches, a report is saved, or a file or the web overlay fails.")},
            {"appearance.display_target", NC_("tooltip", "Where the HUD draws: In-game, a Companion window you can drag to a second monitor, or Both.")},
            {"appearance.companion_background", NC_("tooltip", "Behind the Companion HUD. Green, Blue or Magenta: an OBS Chroma Key filter of that colour shows only the HUD.")},
            {"general.pb_scope", NC_("tooltip", "Bike = PB per bike. Class = PB across all bikes in the same class (MX1, MX2, ...).")},
            {"general.reference", NC_("tooltip", "The lap Gap Bar, Lap Log, Delta Trace, Timing and the map's Lap delta compare against when set to Default.")},
            {"general.freeze", NC_("tooltip", "How long Gap Bar, Lap Log and Timing hold an official gap when set to Default.")},

            {"updates.check_enabled", NC_("tooltip", "Check for updates when the game starts.")},
            {"updates.debug_mode", NC_("tooltip", "Test update installation without overwriting the real plugin.")},
            {"updates.channel", NC_("tooltip", "Stable only shows final releases. Prerelease includes test builds.")},
            {"updates.check_now", NC_("tooltip", "Check for a new version now.")},
            {"updates.retry", NC_("tooltip", "Clear the failed download so you can try again.")},
            {"updates.install", NC_("tooltip", "Download and install the new version. Restart the game to apply it.")},
            {"updates.release_notes", NC_("tooltip", "Open the full release notes on GitHub.")},

            {"appearance.font_title", NC_("tooltip", "Font used for HUD titles.")},
            {"appearance.font_normal", NC_("tooltip", "Font used for normal text.")},
            {"appearance.font_strong", NC_("tooltip", "Font used for column headers and row/legend labels across HUDs.")},
            {"appearance.font_digits", NC_("tooltip", "Font used for numeric values (times, gaps, telemetry).")},
            {"appearance.font_marker", NC_("tooltip", "Font used for pitboard handwritten text.")},
            {"appearance.font_small", NC_("tooltip", "Font used for small text and footnotes.")},
            {"appearance.color_primary", NC_("tooltip", "Main text color for values, times, and key data.")},
            {"appearance.color_secondary", NC_("tooltip", "Supporting text for live data, telemetry values, and descriptions.")},
            {"appearance.color_tertiary", NC_("tooltip", "Subtle labels, axis markers, and category names.")},
            {"appearance.color_muted", NC_("tooltip", "Placeholders, grid lines, and unavailable data.")},
            {"appearance.color_background", NC_("tooltip", "Background color for HUD panels.")},
            {"appearance.color_accent", NC_("tooltip", "Interactive elements, hover highlights, and the player/spectated rider's name in standings.")},
            {"appearance.color_positive", NC_("tooltip", "Faster times, improvements, and gains.")},
            {"appearance.color_neutral", NC_("tooltip", "Color indicating neutral values (no change).")},
            {"appearance.color_warning", NC_("tooltip", "Caution indicators, low fuel, and proximity alerts.")},
            {"appearance.color_negative", NC_("tooltip", "Slower times, losses, and critical alerts.")},

            {"rumble.enabled", NC_("tooltip", "Enable or disable controller vibration for all events.")},
            {"rumble.stack", NC_("tooltip", "When enabled, multiple rumble effects combine. When off, only the strongest effect plays.")},
            {"rumble.crashed", NC_("tooltip", "Continue rumble effects while the rider is crashed.")},
            {"rumble.effect_profile", NC_("tooltip", "Use shared settings (Global) or separate settings per bike.")},
            {"rumble.bumps", NC_("tooltip", "Vibration from suspension travel over bumps. Min/Max set the suspension velocity range in m/s.")},
            {"rumble.slide", NC_("tooltip", "Vibration when the bike slides sideways. Min/Max set the slide angle range in degrees.")},
            {"rumble.spin", NC_("tooltip", "Vibration from rear wheel spin. Min/Max set the wheel slip ratio range.")},
            {"rumble.lockup", NC_("tooltip", "Vibration when brakes lock up. Min/Max set the wheel slip ratio range.")},
            {"rumble.wheelie", NC_("tooltip", "Vibration during wheelies. Min/Max set the pitch angle range in degrees.")},
            {"rumble.steer", NC_("tooltip", "Vibration from steering forces. Min/Max set the steering torque range in Newton-meters.")},
            {"rumble.rpm", NC_("tooltip", "Vibration synchronized with engine RPM. Min/Max set the RPM range.")},
            {"rumble.surface", NC_("tooltip", "Vibration from rough terrain. Min/Max set the vehicle speed range when off-track.")},
            {"rumble.revlimiter", NC_("tooltip", "Vibration near the rev limiter. Min/Max set the range in percent of limiter RPM. Muted off-throttle.")},
            {"rumble.pitlimiter", NC_("tooltip", "Vibration while the pit-lane speed limiter is active. Light/Heavy set the intensity.")},
            {"rumble.split", NC_("tooltip", "Tune front/rear wheels separately. They start from the combined value, then stay independent.")},

            {"widgets.lap", NC_("tooltip", "Shows current lap number and total laps in the session.")},
            {"widgets.position", NC_("tooltip", "Shows your current race position (P1, P2, etc.).")},
            {"widgets.time", NC_("tooltip", "Shows elapsed session time or time remaining.")},
            {"widgets.speed", NC_("tooltip", "Shows current ground speed in mph or km/h.")},
            {"widgets.gear", NC_("tooltip", "Shows current gear with shift indicator that turns red at shift point.")},
            {"widgets.crashes", NC_("tooltip", "A crash tally for streaming - counts across sessions and restarts until you reset it.")},
            {"widgets.speedo", NC_("tooltip", "Analog speedometer gauge showing current speed, with odometer and trip meter.")},
            {"widgets.tacho", NC_("tooltip", "Tachometer gauge showing engine RPM.")},
            {"widgets.rpm", NC_("tooltip", "Shift lights: green, amber, then red from the shift point. The red ones flash on the limiter.")},
            {"widgets.bars", NC_("tooltip", "Vertical bars showing throttle, brake, clutch, RPM, suspension, and fuel levels.")},
            {"widgets.fuel", NC_("tooltip", "Shows remaining fuel level and estimated laps.")},
            {"widgets.gamepad", NC_("tooltip", "Visual representation of controller button inputs.")},
            {"widgets.toggles", NC_("tooltip", "Settings and every switch in one strip; drag it to an edge to turn it. Rest on one that is off to preview it.")},
            {"widgets.clock", NC_("tooltip", "Shows current local time. Supports 12h/24h format and optional UTC display.")},
            {"widgets.lean", NC_("tooltip", "Shows rider lean angle in degrees with arc gauge.")},
            {"widgets.gforce", NC_("tooltip", "G-G diagram plotting lateral vs longitudinal G-force with a peak marker.")},
            {"widgets.compass", NC_("tooltip", "Bike heading dial with N/E/S/W labels (classic needle or modern rotating-card style).")},
            {"widgets.pointer", NC_("tooltip", "On = shown while racing; Off = only in the settings menu (helps controllers seen as a mouse).")},
            {"widgets.tyre_temp", NC_("tooltip", "Shows front and rear tyre temperatures.")},
            {"widgets.ecu", NC_("tooltip", "Shows ECU aids: map, traction, engine braking, anti-wheeling. Chips brighten on intervention.")},
            {"widgets.now_playing", NC_("tooltip", "Spotify or any player in Windows' media controls, with its controls and volume. Needs Now playing in General.")},
            {"widgets.prestige", NC_("tooltip", "Earned by trading in a completed achievement ladder. Texture picks the badge.")},

            {"hotkeys.settings", NC_("tooltip", "Toggle the settings menu on/off.")},
            {"hotkeys.standings", NC_("tooltip", "Toggle the standings HUD on/off.")},
            {"hotkeys.map", NC_("tooltip", "Toggle the track map HUD on/off.")},
            {"hotkeys.radar", NC_("tooltip", "Toggle the radar HUD on/off.")},
            {"hotkeys.lap_log", NC_("tooltip", "Toggle the lap log HUD on/off.")},
            {"hotkeys.ideal_lap", NC_("tooltip", "Toggle the ideal lap HUD on/off.")},
            {"hotkeys.telemetry", NC_("tooltip", "Toggle the telemetry HUD on/off.")},
            {"hotkeys.input", NC_("tooltip", "Toggle the input display HUD on/off.")},
            {"hotkeys.records", NC_("tooltip", "Toggle the records HUD on/off.")},
            {"hotkeys.pitboard", NC_("tooltip", "Toggle the pitboard HUD on/off.")},
            {"hotkeys.timing", NC_("tooltip", "Toggle the timing HUD on/off.")},
            {"hotkeys.gap_bar", NC_("tooltip", "Toggle the gap bar HUD on/off.")},
            {"hotkeys.performance", NC_("tooltip", "Toggle the performance HUD on/off.")},
            {"hotkeys.rumble", NC_("tooltip", "Toggle the rumble HUD on/off.")},
            {"hotkeys.widgets", NC_("tooltip", "Toggle all widgets on/off.")},
            {"hotkeys.fmx", NC_("tooltip", "Toggle the FMX HUD on/off.")},
            {"hotkeys.session_charts", NC_("tooltip", "Toggle the Charts HUD on/off.")},
            {"hotkeys.session", NC_("tooltip", "Toggle the session HUD on/off.")},
            {"hotkeys.notices", NC_("tooltip", "Toggle the notices HUD on/off.")},
            {"hotkeys.segment_add", NC_("tooltip", "Drop a segment boundary: 2 points time a section, close the loop for a full lap.")},
            {"hotkeys.segment_remove", NC_("tooltip", "Remove the last segment boundary. With none left, the Timing HUD shows normal split/lap timing.")},
            {"hotkeys.stats", NC_("tooltip", "Toggle the stats HUD on/off.")},
            {"hotkeys.all_huds", NC_("tooltip", "Toggle all HUDs on/off.")},
            {"hotkeys.crash_reset", NC_("tooltip", "Reset the Crashes widget's tally to zero.")},
            {"hotkeys.reload", NC_("tooltip", "Reload configuration from disk.")},
            {"hotkeys.event_log", NC_("tooltip", "Toggle the event log HUD on/off.")},
            {"hotkeys.stream_chat", NC_("tooltip", "Show or hide the Stream Chat HUD. It stays connected while hidden.")},
            {"hotkeys.helmet", NC_("tooltip", "Toggle the helmet overlay on/off.")},
            {"hotkeys.director_toggle", NC_("tooltip", "Toggle the auto-director on/off.")},
            {"hotkeys.spotter_cue", NC_("tooltip", "Speak your pack's hotkey_triggered line. Nothing is built in - write it first.")},
            {"hotkeys.director_lock", NC_("tooltip", "Lock the director onto the current rider so it won't cut away. Press again to release.")},
            {"hotkeys.friends", NC_("tooltip", "Toggle the friends HUD on/off.")},
            {"hotkeys.delta_trace", NC_("tooltip", "Toggle the delta trace HUD on/off.")},
            {"hotkeys.dash", NC_("tooltip", "Toggle the Dash on/off.")},
            {"hotkeys.dash_next", NC_("tooltip", "Show the Dash's next page.")},
            {"hotkeys.dash_prev", NC_("tooltip", "Show the Dash's previous page.")},
            {"hotkeys.overlay_last_lap", NC_("tooltip", "Web overlay: force the fastest-last-lap board in now (momentary).")},
            {"hotkeys.overlay_fastest_lap", NC_("tooltip", "Web overlay: force the session-best lap board in now (momentary).")},
            {"hotkeys.overlay_down_order", NC_("tooltip", "Web overlay: force the down-the-order (backmarkers) rundown in now (momentary).")},
            {"hotkeys.overlay_sectors", NC_("tooltip", "Web overlay: force the best-sectors carousel in now, one sector per page (momentary).")},
            {"hotkeys.overlay_charts", NC_("tooltip", "Web overlay: force the session-charts carousel in now, one chart per page (momentary).")},

            {"stats.visibility_mode", NC_("tooltip", "When to show. Always displays on track, On finish appears after crossing the finish line.")},
            {"stats.show_lap", NC_("tooltip", "Show last completed lap column with per-lap stats.")},
            {"stats.show_session", NC_("tooltip", "Show current session column with accumulated session stats.")},
            {"stats.show_alltime", NC_("tooltip", "Show all-time column with lifetime stats for this track and bike.")},
            {"achievements.toasts", NC_("tooltip", "The toast card shown when a tier is earned. Off still tracks.")},
            {"achievements.toast_duration", NC_("tooltip", "How long each toast stays on screen.")},
            {"achievements.prestige", NC_("tooltip", "Trades every achievement and its counters for a badge. Lap records kept. Click twice; no way back.")},

            {"fmx.chain_rows", NC_("tooltip", "Tricks shown in the stack. Off hides it, 1 shows the active trick, 2+ shows history.")},
            {"fmx.row_trick_stats", NC_("tooltip", "Show duration, distance, jump height and peak rotation below the active trick name.")},
            {"fmx.row_combo_arc", NC_("tooltip", "Show combo arc with chain multiplier and score breakdown.")},
            {"fmx.row_arcs", NC_("tooltip", "Show pitch, yaw, and roll rotation arcs with start and peak markers.")},
            {"fmx.debug_logging", NC_("tooltip", "Log trick detection state to the plugin log file at 10fps (developer mode only).")},
            {"fmx.row_debug_values", NC_("tooltip", "Show raw rotation values for pitch, yaw, and roll (developer mode only).")},

            {"notices.wrong_way", NC_("tooltip", "Show warning when riding in the wrong direction.")},
            {"notices.blue_flag", NC_("tooltip", "Show notice when a faster rider is approaching to pass. Race sessions only.")},
            {"notices.lapping", NC_("tooltip", "Show notice when you are closing on a rider ahead who is a lap or more down. Race sessions only.")},
            {"notices.overtime", NC_("tooltip", "Show notice when the session timer expires in a time+laps race and extra laps begin.")},
            {"notices.last_lap", NC_("tooltip", "Show notice when starting the final lap of a race.")},
            {"notices.finished", NC_("tooltip", "Show finishing position when crossing the line (e.g. FINISHED 1ST). Updates with penalties.")},
            {"notices.alltime_pb", NC_("tooltip", "Show notice when setting an all-time personal best lap.")},
            {"notices.fastest_lap", NC_("tooltip", "Show notice when setting the fastest lap in an online race.")},
            {"notices.session_pb", NC_("tooltip", "Show notice when setting a session personal best lap.")},
            {"notices.track_changed", NC_("tooltip", "Show the new track's title and artist when Now playing changes track.")},
            {"notices.hazard_stationary", NC_("tooltip", "Show warning when a stationary rider is on the track ahead.")},
            {"notices.hazard_wrong_way", NC_("tooltip", "Show warning when a rider going the wrong way is on the track ahead.")},
            {"notices.default_setup", NC_("tooltip", "Show warning when entering the track with the default bike setup.")},
            {"notices.duration", NC_("tooltip", "How long timed notices stay up. Wrong-way, blue-flag and hazard notices last while active.")},

            {"event_log.icons", NC_("tooltip", "Show small icons next to each event indicating the event type.")},
            {"event_log.display_mode", NC_("tooltip", "Off hides the log. On always shows it. Auto-hide shows it briefly when new events arrive.")},
            {"event_log.duration", NC_("tooltip", "How long the log stays visible after the last event in auto-hide mode.")},
            {"event_log.order", NC_("tooltip", "Oldest shows events chronologically top to bottom. Newest puts latest events at the top.")},
            {"event_log.max_events", NC_("tooltip", "Maximum number of event rows to display. The log scrolls when this limit is reached.")},
            {"event_log.timestamp", NC_("tooltip", "Session shows the session timer value. Clock shows your local time.")},
            {"event_log.session", NC_("tooltip", "Log session lifecycle: started (with format), pre-start, ended, cancelled, complete.")},
            {"event_log.fastest_lap", NC_("tooltip", "Log when a rider sets the overall fastest lap of the session.")},
            {"event_log.penalties", NC_("tooltip", "Log when a rider receives a penalty, or when a penalty is cleared or changed.")},
            {"event_log.rider_out", NC_("tooltip", "Log when a rider retires, is disqualified, or does not start.")},
            {"event_log.overtime", NC_("tooltip", "Log when a timed race enters overtime, or when a non-race session timer expires.")},
            {"event_log.final_lap", NC_("tooltip", "Log when a rider starts their final lap. Race sessions only.")},
            {"event_log.leader_change", NC_("tooltip", "Log when a different rider takes the lead position. Race sessions only.")},
            {"event_log.finished", NC_("tooltip", "Log when a rider crosses the finish line with their finishing position.")},
            {"event_log.pit", NC_("tooltip", "Log when a rider enters or exits the pits. Can be frequent in practice sessions.")},
            {"event_log.session_pb", NC_("tooltip", "Log your lap when it beats your best lap of this session.")},
            {"event_log.alltime_pb", NC_("tooltip", "Log your lap when it sets a new all-time PB.")},
            {"event_log.director", NC_("tooltip", "Show the auto-director's shot decisions and state changes in the log. Frequent, so off by default.")},

            {"twitch.channel", NC_("tooltip", "Channel to read. Click, type or paste a name or twitch.tv link, then press Enter.")},
            {"twitch.status", NC_("tooltip", "Click to connect or disconnect; hiding the chat keeps it connected. Not found? is a typo.")},
            {"youtube.channel", NC_("tooltip", "Channel to read. Click, type or paste your @handle or channel link, then press Enter.")},
            {"youtube.status", NC_("tooltip", "Click to connect or disconnect. YouTube is read unofficially and may stop working.")},
            {"stream_chat.display_mode", NC_("tooltip", "Off hides the chat. Auto-hide shows it briefly when a message arrives.")},
            {"stream_chat.duration", NC_("tooltip", "How long the chat stays visible after the last message in auto-hide mode.")},
            {"stream_chat.order", NC_("tooltip", "Oldest puts the newest message at the bottom, as on Twitch and YouTube. Newest puts it at the top.")},
            {"stream_chat.max_lines", NC_("tooltip", "Lines of chat to show. Long messages wrap onto several lines.")},
            {"stream_chat.width", NC_("tooltip", "Panel width in characters. Messages wrap to fit it.")},
            {"stream_chat.timestamp", NC_("tooltip", "Show the local time each message was sent.")},
            {"stream_chat.platform_icons", NC_("tooltip", "A Twitch or YouTube icon on each line. Auto shows it while both are switched on.")},
            {"stream_chat.name_colors", NC_("tooltip", "Twitch names in the chatter's color, YouTube names by role. Off uses your primary color.")},
            {"stream_chat.text_colors", NC_("tooltip", "Also show message text in the chatter's name color.")},
            {"stream_chat.roles", NC_("tooltip", "Role icons in each platform's badge colors: owner, moderators, VIPs, members and more.")},
            {"stream_chat.emotes", NC_("tooltip", "Keep emote names such as Kappa as text. Off drops them; emoji never render.")},
            {"stream_chat.hide_commands", NC_("tooltip", "Hide bot commands: messages starting with !.")},
            {"stream_chat.hide_bots", NC_("tooltip", "Hide messages from common chat bots such as Nightbot and StreamElements.")},
            {"stream_chat.hide_links", NC_("tooltip", "Hide messages that contain a link.")},

            {"helmet.helmet_enabled", NC_("tooltip", "Enable the helmet overlay.")},
            {"helmet.upper_tex", NC_("tooltip", "Upper helmet texture (visor rim and top).")},
            {"helmet.lower_tex", NC_("tooltip", "Lower helmet texture (chin bar).")},
            {"helmet.upper_offset", NC_("tooltip", "Shift the upper part up or down.")},
            {"helmet.lower_offset", NC_("tooltip", "Shift the lower part up or down.")},
            {"helmet.tilt", NC_("tooltip", "How much the helmet tilts with lean angle.")},
            {"helmet.vibration", NC_("tooltip", "How far the helmet moves when hitting bumps. Negative inverts the direction.")},
            {"helmet.vib_sensitivity", NC_("tooltip", "How responsive vibration is to small bumps.")},
            {"helmet.impact_vignette", NC_("tooltip", "The screen edges darken when you take a hard hit, further the harder it is, then clear.")},
            {"helmet.impact_vignette_fade", NC_("tooltip", "How long the impact vignette takes to clear after the hardest hits; lighter hits clear sooner.")},
            {"helmet.zoom", NC_("tooltip", "Scale the helmet. Zoom in to see less frame.")},
            {"helmet.visor_mode", NC_("tooltip", "Off = no tint. Visor = tint behind helmet (full-face). Goggles = tint in front (open-face).")},
            {"helmet.visor_tint_opacity", NC_("tooltip", "Strength of the tint.")},
            {"helmet.visor_tint_color", NC_("tooltip", "Color of the tint.")},
            {"director.enabled", NC_("tooltip", "Enable the auto-director. Acts only while spectating or watching a replay.")},
            {"director.min_shot", NC_("tooltip", "Shortest time on a shot before cutting away - the main pace dial. Raise it to slow cuts down.")},
            {"director.max_shot", NC_("tooltip", "Longest time on one rider before cutting away. Off = cut only for stories, then back to your rider.")},
            {"director.battle_gap", NC_("tooltip", "How close (seconds) two riders must be to count as a battle. Used by camera and overlay.")},
            {"director.battle_max_pos", NC_("tooltip", "Ignore battles/incidents/overtakes past this position. Off = no limit. Gates the overlay.")},
            {"director.resume_after", NC_("tooltip", "After manual control, auto-resume once the controller is idle this long. Off = stay paused.")},
            {"director.gamepad_takeover", NC_("tooltip", "Push a stick to grab Free-Roam and pause the director. Auto-resumes when the stick goes idle.")},
            {"director.follow_battles", NC_("tooltip", "Let close battles steer the camera. Off holds the leader/solo baseline.")},
            {"director.follow_incidents", NC_("tooltip", "Cut to a rider the instant they crash (or go off / stall). Highest priority.")},
            {"director.follow_fastest", NC_("tooltip", "Briefly flash to a rider who just set the overall fastest lap of the race.")},
            {"director.follow_pace", NC_("tooltip", "Cut to a rider setting a session-best sector - a hot lap forming. Practice/quali only.")},
            {"director.finish_lock", NC_("tooltip", "On the leader's final lap, lock onto the front so the win is never missed.")},
            {"director.catch_overtakes", NC_("tooltip", "Cut to a rider who just completed an on-track pass. Multi-place moves score higher.")},
            {"director.follow_drops", NC_("tooltip", "Follow a rider tumbling down the order - losing several places fast. Below a real battle.")},
            {"director.variety_every", NC_("tooltip", "How often to dip into an onboard camera: every Nth cut. Needs Max shot on; Off = always trackside.")},
            {"director.hold", NC_("tooltip", "How long to hold a story shot (crash / fastest lap) before cutting back.")},
            {"director.cam_fender", NC_("tooltip", "Fender onboards to allow: Off, Front (rider ahead), Rear (a chaser), or Both.")},
            {"director.cam_helmet", NC_("tooltip", "Helmet views to allow: Off, Helmet 1 (POV), Helmet 2 (side), or Both. All forward-facing.")},
            {"spotter.enabled", NC_("tooltip", "Speak callouts aloud (Windows text-to-speech). Subtitles work without it.")},
            {"spotter.subtitles", NC_("tooltip", "Show each callout as on-screen text for a few seconds. Drag the widget to place it.")},
            {"spotter.volume", NC_("tooltip", "Callout volume, separate from the game's audio.")},
            {"spotter.speed", NC_("tooltip", "Playback speed for every callout. 1.00x is as recorded.")},
            {"spotter.lateral_m", NC_("tooltip", "How far ACROSS the track a rider still counts as near. "
                                  "Wider is ignored, as the Radar ignores it.")},
            {"spotter.behind_on_m", NC_("tooltip", "How close a rider behind must be to be called. "
                                    "Shorter means fewer, later warnings.")},
            {"spotter.alongside_on_m", NC_("tooltip", "How far BEHIND you a rider still counts as "
                                       "alongside - your blind spot.")},
            {"spotter.alongside_ahead_m", NC_("tooltip", "How far AHEAD still counts. Keep it short: "
                                          "you can see those riders. 0 = none.")},
            {"spotter.cat_general", NC_("tooltip", "The session and your own status: flags, your penalty, your pit lane.")},
            {"spotter.cat_timing", NC_("tooltip", "Your numbers: lap and sector times, position, gaps, time left.")},
            {"spotter.cat_opponents", NC_("tooltip", "The field's race news: others' laps, penalties, pit stops, retirements.")},
            {"spotter.cat_proximity", NC_("tooltip", "Riders around you right now: behind, alongside, boxed in, backmarkers.")},
            {"spotter.cat_hazard", NC_("tooltip", "Dangers: blue flag, rider down ahead, wrong-way rider.")},
            {"spotter.cat_tricks", NC_("tooltip", "Your freestyle tricks, landings and crashes. Never in a race.")},
            {"spotter.cat_achievements", NC_("tooltip", "A line for every achievement you earn, good or bad.")},
            {"spotter.pack", NC_("tooltip", "Which pack's wording and clips to use. 'default' is the shipped one.")},
            {"spotter.tts_voice", NC_("tooltip", "Which Windows voice speaks the text-to-speech cues.")},
        };
        return m;
    }
};
