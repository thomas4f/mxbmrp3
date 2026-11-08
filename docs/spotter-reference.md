# Spotter reference

This is the lookup table. The two files it goes with:

- `mxbmrp3_data/spotters/default/spotter.ini` - the shipped pack,
  which is the wording itself. Every key below has a row in it, so it
  is also the worked example; copy that folder to edit it.
- `docs/spotter.md` - the guide: what it calls and how to set it up,
  then the authoring half (optional groups, alternates, fallbacks,
  recorded packs, the chunk mixer and `[Mix] gap_ms`).

<sub>Generated from `mxbmrp3/core/spotter_cue_pack.h` (`allCueKeys()`) and `mxbmrp3/core/spotter_vars.h` (`bindings()`) by `tests/unit/test_spotter_pack_census.cpp` - do not edit. Run the unit gate, which rewrites it and fails while this copy is stale.</sub>

## Cues

A cue is a MOMENT. Each row is a key you can define in a pack's
`[Cues]` section; the phrase you write against it is what gets said.

**Ships** is whether the shipped pack speaks it out of the box -
its own row, or the row of the key it refines. There is no wording
anywhere else, so a cue nothing defines is simply not spoken, and
uncommenting its row is how you turn a *silent* one on. The heading
a cue sits under names the switch in
Settings > Spotter that mutes it, and the section it lives in in the
shipped ini - the three deliberately agree.

### General

| Cue | Ships | When it fires |
|---|---|---|
| `session_started` | on | this session is now active, any kind. The shipped line does NOT state the length: a race hears session_prestart seconds earlier and it is too late to act on by then. The kind-specific rows below keep it, having no prestart before them. If you do want it here, {session_length} is the variable - {session_remaining} can only repeat it |
| `gate_drop` | on | the gate physically falls (standing starts only) - NOT the same moment as session_started, which fires a few seconds earlier with the grid still held |
| `practice_started` | on | the same moment worded for practice; falls back to session_started |
| `quali_started` | on | the same moment worded for qualifying; falls back to session_started |
| `warmup_started` | on | the same moment worded for warm up; falls back to session_started |
| `session_prestart` | on | the session is about to go green |
| `session_ended` | on | the session is over - fires whether you are still circulating or already parked |
| `session_state` | on | the session changed to a state with no cue of its own - cancelled, sighting lap, race over. The idle gap between sessions is deliberately not announced |
| `holeshot_you` | silent | YOU took the holeshot - first of the field through the opening timing line after a gate drop, whichever line that is (the splits are numbered from start/finish, and a grid need not sit behind it). DEFAULT-QUIET: it lands in the first seconds of a race, where a voice competes with the one moment nobody wants narrated |
| `leader_you` | on | you took the lead |
| `finished_you` | on | you took the flag |
| `penalty_you` | on | you were penalised. {penalty_seconds} is THIS penalty and {penalty_total} the running total INCLUDING it - the standings column has not absorbed it yet at this instant, so the cue keeps its own tally. The total is empty on a first penalty, where it would only repeat the amount |
| `penalty_clear_you` | on | your penalty was cleared |
| `penalty_change` | on | your penalty was revised up or down |
| `disqualified_you` | on | you were disqualified |
| `retired_you` | silent | you retired from the session |
| `crashed_you` | silent | you went down |
| `fuel_low` | on | fuel down to about four laps - the point the Fuel widget turns amber. Checked as you cross the line, once each per session and only on the way down; needs two laps first, since one lap cannot tell a rate from a tank change |
| `fuel_critical` | on | ...and now about two, where it turns red; falls back to fuel_low |
| `pit_entry_you` | on | you entered the pits |
| `pit_exit_you` | on | you left the pits |
| `hotkey_triggered` | on | the Spotter hotkey was pressed |
| `voice_preview` | on | cycling onto this pack in the settings. Optional even for a recorded pack - absent, the plugin builds a preview from the number clips every pack has - and it never reaches the subtitle |

### Timing

| Cue | Ships | When it fires |
|---|---|---|
| `overtime_started` | on | the clock expired, bonus laps begin. {overtime_laps} counts the laps AFTER the current one: the clock stops with the leader mid-lap, so no laps-to-go count is true wherever they happen to be |
| `session_time_expired` | on | the clock hit zero in a session with no bonus laps to run |
| `final_lap` | silent | the LEADER starts the last lap - a fact about the race, not about you. DEFAULT-QUIET: final_lap_you covers your own last lap from any position, and hearing both says one actionable moment twice |
| `final_lap_you` | on | YOU start your last lap - a lap or more later than the leader's if you are down the order |
| `fastest_lap_you` | on | you set the fastest lap of the session, beating the whole field |
| `personal_best` | on | you beat your best ever here; suppresses fastest_lap_you on the same lap |
| `record_beaten` | on | you beat the track record; replaces personal_best rather than adding to it (MX Bikes only) |
| `session_best` | on | you beat your session best without it being an all-time best - the common good-news lap in practice |
| `lap_invalidated` | on | your lap was struck out - the only notice you get in practice or qualifying, which issue no penalties |
| `lap_completed` | on | you crossed the line - THE cue for your own lap, and what it says is entirely the template's choice ({position}, {last_lap_time}, the gaps either side). Spoken once the order includes the lap you just did |
| `position_gained` | on | you gained places ON THIS LAP, against where you stood at your last crossing. Fires at the same instant as lap_completed, which has just said where you are - so a line here wants to say only what CHANGED |
| `position_lost` | on | you lost places on this lap, same measure and the same instant |
| `gap_behind` | on | the rider behind reaches a point you already crossed - a stopwatch gap, not an estimate |
| `gap_behind_closing` | on | ...and they are closing on you |
| `gap_behind_dropping` | on | ...and they are dropping back |
| `sector_completed` | silent | you crossed a split with no best lap yet to compare it against - the lap cue's smaller sibling, three or four times a lap, which is why the split cues ship silent |
| `sector_completed_faster` | silent | ahead of your best lap at this split |
| `sector_completed_slower` | silent | behind your best lap at this split |
| `sector_best` | on | that sector ON ITS OWN beat your best of it this session - so it still fires for a great sector inside a scrappy lap, which the rows above cannot say |
| `on_pace_session_best` | on | at the last split before the line, up on your best lap of the session by at least `[Spotter] on_pace_margin_ms` |
| `on_pace_personal_best` | on | ...up on your best lap ever here (outranks the session one) |
| `on_pace_record` | on | ...up on the track record (outranks both) |
| `ten_minutes_remaining` | on | ten minutes of the session left |
| `five_minutes_remaining` | on | five minutes left |
| `halfway_point` | on | half the session gone, by clock in a timed one and by the leader's laps otherwise |

### Opponents

| Cue | Ships | When it fires |
|---|---|---|
| `fastest_lap_other` | on | someone else set the fastest lap |
| `leader_other` | on | the lead changed hands |
| `finished_other` | silent | someone else took the flag - one callout per rider as the flag falls, and none of them about your race; finished_leader is the one that is |
| `finished_leader` | on | the leader took the flag |
| `retired_other` | silent | someone else retired |
| `disqualified_other` | silent | someone else was disqualified |
| `did_not_start_other` | silent | someone else did not start |
| `crashed_other` | silent | someone else crashed |
| `penalty_other` | silent | someone else was penalised - the biggest offender of the quiet lot: replaying a real 24-rider race, other riders' cut penalties were TWENTY of the sixty callouts spoken in four laps |
| `penalty_change_other` | silent | someone else's penalty was revised |
| `penalty_clear_other` | silent | someone else's penalty cleared |
| `pit_entry_other` | silent | someone else entered the pits - a full grid's pit traffic is the noisiest event stream there is and says nothing about your race |
| `pit_exit_other` | silent | someone else left the pits |
| `spectate_target` | silent | the camera cut to a different rider - the auto-director cuts constantly, so this ships silent |

### Proximity

| Cue | Ships | When it fires |
|---|---|---|
| `rider_behind` | on | someone has closed up behind you |
| `rider_behind_clear` | on | ...and has now dropped back |
| `rider_left` | on | someone is alongside on your left |
| `rider_right` | on | someone is alongside on your right |
| `riders_both_sides` | on | you are boxed in - a rival on EACH side at once, which is the one case where hearing only one side is worse than hearing nothing |
| `lapping_traffic` | on | a backmarker is ahead of you - and {event_rider} is them. One of only two detector cues that names its subject (blue_flag is the other): both sides of the lapping pair are already known, where the proximity and hazard cues carry distances only |

### Hazards

| Cue | Ships | When it fires |
|---|---|---|
| `blue_flag` | on | a faster rider is closing to lap you, and {event_rider} is that rider - which bike to expect is the half of a blue flag you can act on. Empty if the pairing has already been recomputed away, so bracket it |
| `hazard_ahead` | on | a rider is down ahead of you |
| `wrong_way_ahead` | on | a rider is coming the wrong way |

### Tricks

| Cue | Ships | When it fires |
|---|---|---|
| `trick_started` | on | your trick has a name worth saying and is half done: a wheelie after a second held, an air trick mid-air once it has rotated into one. Never plain Air or Drift, never in a race. Deliberately NO {trick}: the name can still change before the trick is done (a scrub becomes a whip), so it is only said on trick_landed |
| `trick_landed` | on | you APPEAR to have landed it - still upright 0.4 s after touching down, which is before the trick is confirmed, so a crash can still follow. {trick} has its multiplier ("double backflip") |
| `trick_crashed` | on | you went down with a named trick or a live chain - the chain is gone. NO {trick}: a trick that never landed was never really that trick |
| `trick_combo` | on | a chain of three or more tricks banked. {combo_count} |

### Achievements

| Cue | Ships | When it fires |
|---|---|---|
| `achievement_distance` | on | you earned Long Hauler - Ride ... in total (every tier; {tier} says which) |
| `achievement_ride_time` | on | you earned Seat Time - Spend ... on track (every tier; {tier} says which) |
| `achievement_laps` | on | you earned Lap Counter - Complete ... laps (every tier; {tier} says which) |
| `achievement_fuel_burnt` | on | you earned Carbon Footprint - Burn ... of fuel |
| `achievement_gear_shifts` | on | you earned Shift Happens - Shift ... times (every tier; {tier} says which) |
| `achievement_session_laps` | on | you earned Marathon - Complete ... laps in one session (every tier; {tier} says which) |
| `achievement_session_time` | on | you earned Iron Butt - Ride ... in one day (every tier; {tier} says which) |
| `achievement_track_laps` | on | you earned Local Hero - Complete ... laps at one track (every tier; {tier} says which) |
| `achievement_bike_km` | on | you earned Loyal - One bike, ... (every tier; {tier} says which) |
| `achievement_races` | on | you earned Racer - Finish ... races (every tier; {tier} says which) |
| `achievement_wins` | on | you earned Winner - Win ... races (every tier; {tier} says which) |
| `achievement_podiums` | on | you earned Podium Regular - Finish on the podium ... times (every tier; {tier} says which) |
| `achievement_lapped_field` | on | you earned Lapped the Field - Win with every rider at least a lap down |
| `achievement_fastest_laps` | on | you earned Hot Lapper - Set the fastest lap in ... races (every tier; {tier} says which) |
| `achievement_rain_races` | on | you earned Mudder - Finish ... races in the rain (every tier; {tier} says which) |
| `achievement_big_grids` | on | you earned Crowd Surfer - Finish ... races on a 20+ rider grid (every tier; {tier} says which) |
| `achievement_holeshots` | on | you earned Holeshot-ish - Lead at the first split in ... races (every tier; {tier} says which) |
| `achievement_charger` | on | you earned Charger - Climb ... places from the first split (every tier; {tier} says which) |
| `achievement_last_lap_pass` | on | you earned Last Gasp - Pass on the last lap in ... races (every tier; {tier} says which) |
| `achievement_survivor` | on | you earned Survivor - Finish ... races where a quarter quit (every tier; {tier} says which) |
| `achievement_roost` | on | you earned Roost - Ride ... in a rider's wake (every tier; {tier} says which) |
| `achievement_clean_races` | on | you earned Rubber Side Down - Finish ... races without crashing (every tier; {tier} says which) |
| `achievement_penalty_free` | on | you earned Clean Sheet - Go ... races in a row penalty-free (every tier; {tier} says which) |
| `achievement_wire_to_wire` | on | you earned Wire to Wire - Win ... races leading every lap (every tier; {tier} says which) |
| `achievement_perfect_race` | on | you earned Perfect Race - Holeshot, every lap led, fastest lap, win |
| `achievement_clean_laps` | on | you earned Spotless - ... laps in a row, crash/penalty free (every tier; {tier} says which) |
| `achievement_crashes` | on | you earned Skill Issue - Crash ... times in total |
| `achievement_penalties` | on | you earned Rule Bender - Collect ... penalties |
| `achievement_penalty_time` | on | you earned Time Served - Serve ... of penalty time |
| `achievement_tracks` | on | you earned Globetrotter - Ride ... different tracks (every tier; {tier} says which) |
| `achievement_bikes` | on | you earned Collector - Ride ... different bikes (every tier; {tier} says which) |
| `achievement_personal_bests` | on | you earned Personal Best - Set ... personal bests (every tier; {tier} says which) |
| `achievement_pb_leaps` | on | you earned Leap Forward - Beat ... PBs by a second or more (every tier; {tier} says which) |
| `achievement_fmx_tricks` | on | you earned Show-off - Land ... freestyle tricks (every tier; {tier} says which) |
| `achievement_fmx_points` | on | you earned Style Points - Score ... FMX points (every tier; {tier} says which) |
| `achievement_fmx_chain` | on | you earned Chain Reaction - Score ... points in one chain (every tier; {tier} says which) |
| `achievement_fmx_kinds` | on | you earned Trick Collector - Land ... kinds of freestyle trick (every tier; {tier} says which) |
| `achievement_air_miles` | on | you earned Frequent Flyer - Spend ... in the air (every tier; {tier} says which) |
| `achievement_air_distance` | on | you earned Air Miles - Cover ... through the air (every tier; {tier} says which) |
| `achievement_air_height` | on | you earned To the Moon - Climb ... in jumps (every tier; {tier} says which) |
| `achievement_fmx_airtime` | on | you earned Hang Time - Stay airborne ... and land it (every tier; {tier} says which) |
| `achievement_jump_distance` | on | you earned Gap Jumper - Land a ... jump (every tier; {tier} says which) |
| `achievement_jump_height` | on | you earned Sent It - Reach ... above takeoff and land it (every tier; {tier} says which) |
| `achievement_fmx_backflips` | on | you earned Flipper - Land ... backflips (every tier; {tier} says which) |
| `achievement_fmx_wheelie` | on | you earned Wheelie King - Land a ... wheelie (every tier; {tier} says which) |
| `achievement_fmx_wheelie_km` | on | you earned One-Wheel Tour - Ride ... on the back wheel (every tier; {tier} says which) |
| `achievement_fmx_whips` | on | you earned Whip It - Land ... whips (every tier; {tier} says which) |
| `achievement_fmx_scrubs` | on | you earned Scrubber - Land ... scrubs (every tier; {tier} says which) |
| `achievement_fmx_oppos` | on | you earned Oppo - Land ... oppos (every tier; {tier} says which) |
| `achievement_fmx_endos` | on | you earned Endo - Land a ... endo or stoppie |
| `achievement_fmx_turn_downs` | on | you earned Turn Down - Land ... turn downs (every tier; {tier} says which) |
| `achievement_breakout` | on | you earned Brick Breaker - Breakout score ... |
| `achievement_config_reloads` | on | you earned Tinkerer - Reload the config |
| `achievement_photo_finish` | on | you earned Photo Finish - Win a race by under a tenth |
| `achievement_so_close` | on | you earned So Close - Lose a race by under a tenth |
| `achievement_back_marker` | on | you earned Back Marker - Finish a race at least a lap down |
| `achievement_metronome` | on | you earned Metronome - Five laps in a row, all within a tenth |
| `achievement_chain_lost` | on | you earned Weakest Link - Crash out of a ...-point chain |
| `achievement_tyre_shredder` | on | you earned Tyre Shredder - ... in burnouts and drifts (every tier; {tier} says which) |
| `achievement_tyre_kicker` | on | you earned Tyre Kicker - Try each HUD and widget |
| `achievement_grand_tour` | on | you earned Grand Tour - Open every settings tab |
| `achievement_decorator` | on | you earned Interior Decorator - Override a colour, font or theme |
| `achievement_dress_up` | on | you earned Made It Yours - Install a pack of any type |
| `achievement_on_air` | on | you earned On Air - Connect the web overlay |
| `achievement_second_screen` | on | you earned Second Screen - Show the HUD on a second screen |
| `achievement_directors_cut` | on | you earned Director's Cut - Watch ... director cuts |
| `achievement_backseat` | on | you earned Backseat Driver - Hear ... spotter calls |
| `achievement_good_vibrations` | on | you earned Good Vibrations - Turn rumble on |
| `achievement_keymaster` | on | you earned Keymaster - Bind a hotkey |
| `achievement_profile_hopper` | on | you earned Profile Hopper - Switch profiles ... times |
| `achievement_fresh_coat` | on | you earned Fresh Coat - Install an update in-game |
| `achievement_split_decision` | on | you earned Split Decision - Time a segment |
| `achievement_armchair` | on | you earned Armchair Marshal - Spectate or replay for ... |
| `achievement_regular` | on | you earned Regular - Show up on ... different days (every tier; {tier} says which) |
| `achievement_day_streak` | on | you earned On a Roll - Show up ... days in a row (every tier; {tier} says which) |
| `achievement_servers` | on | you earned Well Travelled - Join ... different servers (every tier; {tier} says which) |
| `achievement_bike_classes` | on | you earned Class Act - Ride bikes from ... classes (every tier; {tier} says which) |
| `achievement_prerelease` | on | you earned Early Access - Switch to the pre-release channel |
| `achievement_frame_perfect` | on | you earned Frame Perfect - Hit ...FPS |
| `achievement_stalker` | on | you earned Stalker - Track ... riders |
| `achievement_all_bronze` | on | you earned Bronze Sweep - Counted achievements earned |
| `achievement_all_silver` | on | you earned Silver Sweep - Counted achievements at Silver or max |
| `achievement_all_gold` | on | you earned Gold Sweep - Counted achievements at Gold or max |
| `achievement_all_platinum` | on | you earned Platinum Sweep - Counted achievements maxed out |
| `achievement_phoenix` | on | you earned Phoenix - Come back after a crash to desktop |
| `achievement_ninety_nine` | on | you earned 99 Problems - Crash counter hits 99 |
| `achievement_test_pilot` | on | you earned Test Pilot - Turn on an experimental setting |
| `achievement_under_hood` | on | you earned Under the Hood - Edit the settings file by hand |
| `achievement_backup_plan` | on | you earned Backup Plan - Edit the stats file by hand |
| `achievement_stylist` | on | you earned Stylist - Style the web overlay with custom.css |
| `achievement_homebrew` | on | you earned Homebrew - Run a build you made yourself |
| `achievement_ungrateful` | on | you earned Ungrateful - Turn achievement toasts off |
| `achievement_factory_fresh` | on | you earned Factory Fresh - Reset every setting to defaults |
| `achievement_developer` | on | you earned Developer - Enable developer mode |
| `achievement_night_owl` | on | you earned Night Owl - Ride between two and five in the morning |
| `achievement_anniversary` | on | you earned Anniversary - Ride on the anniversary of your first run |
| `achievement_sandbagger` | on | you earned Sandbagger - Set a PB on the last lap of a race |
| `achievement_palindrome` | on | you earned Palindrome - Set a lap time the same backwards |
| `achievement_deja_vu` | on | you earned Deja Vu - Set two identical lap times in a session |
| `achievement_rage_quit` | on | you earned Rage Quit - Retire from a race before the finish |
| `achievement_bakers_dozen` | on | you earned Baker's Dozen - Crash 13 times in one session |
| `achievement_steady_hands` | on | you earned Steady Hands - Ride ... without crashing |
| `achievement_fumes` | on | you earned Running on Fumes - Finish a race on 1% of a tank or less |
| `achievement_ran_dry` | on | you earned Long Walk Home - Run out of fuel out on track |
| `achievement_peak_g` | on | you earned Big Hit - Take a ...G hit |
| `achievement_solo_race` | on | you earned Just Me Then - Finish a race nobody else started |
| `achievement_mondays` | on | you earned Case of the Mondays - Crash out of a chain of five tricks |
| `achievement_choke` | on | you earned Choke - Lead into the final lap and lose the race |
| `achievement_consolation` | on | you earned Consolation Prize - Fastest lap, and off the podium with it |
| `achievement_pile_up` | on | you earned Pile-Up - Go down with three others |
| `achievement_peace_out` | on | you earned Peace Out - Ride through three riders down |
| `achievement_favorite_spot` | on | you earned Favorite Spot - Crash three times in a row in one place |
| `achievement_digging` | on | you earned Digging a Hole - Spin the rear wheel in place for ... |
| `achievement_fmx_frontflips` | on | you earned Somersault - Land ... frontflips (every tier; {tier} says which) |

## Variables

A variable is a NUMBER - it does not happen, it just is.

**Reads as** is a sample expansion, not a format string: values
arrive as words, and some of them ALREADY CARRY their direction
(`{gap_to_best_lap}` says "quicker", `{gap_to_ahead}` does not),
so a template that adds the word itself reads "up three tenths
quicker". These are the values `spotter-pack-render.md` uses.

**Every variable works in every cue.** They are read from the live
race at the moment a cue fires, not carried by the event that fired
it, so any callout can ask for your position or the gap ahead. One
with no value right now expands to nothing, and a `[bracketed
group]` containing it drops whole - which is what lets a single
template read correctly whether or not the value is there.

### What the event carries

| Variable | Meaning | Reads as |
|---|---|---|
| `{event_rider}` | who the cue is about - "rider four seventy six", or "you" | rider four seventy six |
| `{event_time}` | the lap or sector time the cue carries - at a split that is the ACCUMULATED time, the elapsed lap time the Timing HUD shows | one forty eight point two |
| `{penalty_seconds}` | how long THIS penalty is | five seconds |
| `{overtime_laps}` | the BONUS laps added when the clock expires, not a countdown | two laps |
| `{positions_changed}` | places made up or lost on this lap, unsigned | three |
| `{event_gap_to_best_lap}` | another rider's lap against your session best (their laps only) | one point one slower |
| `{event_gap_to_last_lap}` | ...and against your last lap | zero point four quicker |
| `{sector_number}` | which sector just ended | two |
| `{sector_duration}` | that sector on its own, not the elapsed lap time | twenty nine point eight |
| `{trick}` | the trick you landed, on trick_landed: with its multiplier or level and without the side | double backflip |
| `{combo_count}` | how many tricks the chain held, on trick_combo | five |
| `{achievement}` | the achievement's title, on the achievement cues | Skill Issue |
| `{tier}` | the tier just reached; empty for a one-shot, so bracket it | gold |

### Sector deltas

| Variable | Meaning | Reads as |
|---|---|---|
| `{sector_delta_best_lap}` | elapsed time at this split vs your best lap's, with "quicker" or "slower" | zero point three quicker |
| `{sector_delta_ideal}` | ...vs your best sectors summed | zero point six slower |
| `{sector_delta_last_lap}` | ...vs last lap's | zero point two quicker |
| `{sector_delta_alltime}` | ...vs your best lap ever here | one point four slower |
| `{sector_delta_record}` | ...vs the track record's | two point nine slower |
| `{pace_margin}` | how far up you are at the last split, on the on_pace cues | zero point seven |
| `{sector_best_delta}` | how much you took off your best for that sector | zero point four |

### You

| Variable | Meaning | Reads as |
|---|---|---|
| `{rider_name}` | your name, as the game has it | Alex |
| `{position}` | your position | four |
| `{lap_number}` | the lap you are on | six |
| `{last_lap_time}` | your last lap | one forty eight point two |
| `{gap_to_leader}` | your gap to P1 - seconds on the same lap, laps once you are not | twelve point four |
| `{penalty_total}` | your penalties added up. Includes the one being announced, so penalty_you can say the running total the standings column has not caught up with yet | ten seconds |
| `{fuel_laps}` | laps left in the tank | four laps |
| `{finish_time}` | your total race time; empty until you have finished | twenty three minutes, nine seconds |
| `{setup_name}` | the setup you are on ("Default" when it is the stock one) | Default |
| `{positions_since_start}` | places gained since the grid - empty outside a race | three |
| `{positions_since_lap}` | ...since your last start/finish crossing | one |
| `{positions_since_sector}` | ...since your last split | two |

### Your reference laps

| Variable | Meaning | Reads as |
|---|---|---|
| `{best_lap_time}` | your best this session | one forty seven point nine |
| `{alltime_best_time}` | your best ever here; empty on a track you have never ridden | one forty six point three |
| `{ideal_lap_time}` | your best sectors summed; empty until you have set every one | one forty five point eight |
| `{overall_best_time}` | anyone's best this session | one forty five point one |
| `{record_time}` | the track record (MX Bikes only) | one forty four point seven |
| `{gap_to_best_lap}` | your last lap vs your session best | zero point three slower |
| `{gap_to_alltime}` | ...vs your best ever here | one point nine slower |
| `{gap_to_ideal}` | ...vs your ideal lap | two point four slower |
| `{gap_to_overall}` | ...vs the session's best | three point one slower |
| `{gap_to_record}` | ...vs the track record | three point five slower |
| `{gap_to_last_lap}` | ...vs the lap before it | zero point eight quicker |

### The riders either side

| Variable | Meaning | Reads as |
|---|---|---|
| `{position_ahead}` | the position of the rider ahead | three |
| `{rider_ahead}` | who is ahead | rider sixty five |
| `{gap_to_ahead}` | how far ahead they are - the STOPWATCH reading from the last timing point you both crossed, or a lap count once they are a lap up; empty until there has been one for this rider | one point two |
| `{gained_on_ahead}` | how much of that gap changed since the last shared point | zero point three |
| `{trend_ahead}` | "gained" or "lost" - PAST tense: the number beside it is a completed change between two timing points, not a rate. Put the verb first: "gained zero point four on rider twelve" | gained |
| `{last_lap_ahead}` | the lap they just ran - the gap says where they are, this says whether you are catching them. Empty until they have completed one | one forty eight point two |
| `{position_behind}` | the position of the rider behind | five |
| `{rider_behind}` | who is behind | rider four seventy six |
| `{gap_to_behind}` | how far behind they are, measured the same way | two point one |
| `{gained_on_behind}` | how much of that gap changed since the last shared point | zero point four |
| `{trend_behind}` | "closed" or "dropped back" - past tense, same as {trend_ahead} | closed |
| `{last_lap_behind}` | the lap they just ran, measured the same way | one forty seven point nine |

### The session

| Variable | Meaning | Reads as |
|---|---|---|
| `{session_name}` | "Race 1", "Warmup", "Qualify Practice"... | Race 1 |
| `{session_state}` | "In Progress", "Cancelled", "Sighting Lap"... | In Progress |
| `{session_length}` | how long it is: minutes, laps, or both | twenty minutes plus two laps |
| `{session_remaining}` | what is left of it, in the same shape; empty until the session is running | six minutes plus two laps |
| `{laps_remaining}` | lap races only, once running; in a time+laps race, the leader's once overtime starts | three |
| `{time_remaining}` | timed sessions only, once running | six minutes |
| `{leader_name}` | who is leading ("you" when that is you -- write the line so both read well) | rider sixty five |
| `{track_name}` | the track | Farm 14 |

