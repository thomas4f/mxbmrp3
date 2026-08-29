# The shipped spotter pack, rendered

GENERATED from `mxbmrp3_data/spotters/default/spotter.ini` by
`test_spotter_pack_census.cpp`, which rewrites it and fails if this
copy is stale. Do not edit by hand - edit the pack.

Every live row of the shipped pack, as the subtitle shows it and
text-to-speech reads it. **Filled** is every variable at its sample
value from `spotter-reference.md`; **empty** is the same line with
every value missing - leading the race, lap one, nothing measured
yet. Both matter: a template is written against the first and
shipped against the second.

A line reading as a fragment in the **empty** column has words
outside its `[optional groups]` that only make sense with a value.
That is not always a bug - a cue whose emitter cannot fire without
a position may say `P {position}` bare - but it is always worth a
look, which is the point of generating this rather than asserting
a rule about it.

| Cue | Filled | Empty |
|---|---|---|
| `session_started` | Race 1 underway. | Underway. |
| `session_started_2` | Race 1 is live. | Is live. |
| `practice_started` | Race 1 underway, twenty minutes plus two laps. | Underway. |
| `quali_started` | Race 1 underway, twenty minutes plus two laps. | Underway. |
| `warmup_started` | Race 1 underway, twenty minutes plus two laps. | Underway. |
| `gate_drop` | Green green green. | Green green green. |
| `gate_drop_2` | Gate's down, go go go. | Gate's down, go go go. |
| `gate_drop_3` | We're racing. | We're racing. |
| `session_prestart` | Race 1 starting, twenty minutes plus two laps, get ready. | Starting, get ready. |
| `session_prestart_2` | On the gate for Race 1, twenty minutes plus two laps. | On the gate. |
| `session_ended` | Race 1 complete, you're P four. | Complete. |
| `session_ended_2` | That's Race 1 done, P four. | That's done. |
| `session_state` | Race 1, In Progress. | . |
| `leader_you` | You're the leader now, two point one to rider four seventy six. | You're the leader now. |
| `leader_you_2` | You've got the lead, two point one back to P five. | You've got the lead. |
| `leader_you_3` | P one, the lead is yours, three to go. | P one, the lead is yours. |
| `finished_you` | Checkered flag, P four, twenty three minutes, nine seconds, twelve point four off the lead. | Checkered flag. |
| `finished_you_2` | That's the flag, P four, best lap one forty seven point nine. | That's the flag. |
| `penalty_you` | Penalty, penalty, five seconds, ten seconds in total. | Penalty, penalty. |
| `penalty_you_2` | You've picked up a penalty, five seconds, ten seconds in total. | You've picked up a penalty. |
| `penalty_you_3` | Penalty for you, five seconds. | Penalty for you. |
| `penalty_clear_you` | Penalty cleared, you're good. | Penalty cleared, you're good. |
| `penalty_clear_you_2` | That penalty's gone. | That penalty's gone. |
| `penalty_change` | Penalty changed. | Penalty changed. |
| `disqualified_you` | Disqualified. | Disqualified. |
| `fuel_low` | Fuel getting low, four laps. | Fuel getting low. |
| `fuel_low_2` | Watch the fuel, about four laps left. | Watch the fuel, about left. |
| `pit_entry_you` | Entering pit lane. | Entering pit lane. |
| `pit_entry_you_2` | Into the pits. | Into the pits. |
| `pit_exit_you` | Pit exit, up to speed, Race 1, six minutes plus two laps left, on the Default setup. | Pit exit, up to speed. |
| `hotkey_triggered` | Alex, P four, lap six, six minutes plus two laps left, one point two to rider sixty five. | . |
| `voice_preview` | Spotter ready. | Spotter ready. |
| `lap_completed` | P four, one point two to rider sixty five, gained zero point three. | P. |
| `lap_completed_2` | P four, one forty eight point two, zero point three slower than your best. | P. |
| `lap_completed_3` | That's one forty eight point two, P four, two point one to rider four seventy six. | P. |
| `lap_completed_4` | P four, twelve point four off the lead, three to go. | P. |
| `lap_completed_5` | P four, one point two to rider sixty five, who ran one forty eight point two. | P. |
| `position_gained` | Up three. | Up. |
| `position_gained_2` | Made up three, P four. | Made up. |
| `position_gained_3` | Up three, keep it going. | Up , keep it going. |
| `position_lost` | Down three. | Down. |
| `position_lost_2` | Lost three, P four. | Lost. |
| `position_lost_3` | Dropped three, settle in. | Dropped , settle in. |
| `sector_best` | Best sector two, twenty nine point eight. | Best sector. |
| `sector_best_2` | That's your best sector two, zero point four off it. | That's your best sector. |
| `sector_best_3` | Sector two personal best, twenty nine point eight. | Sector personal best. |
| `on_pace_session_best` | On for your session best, up zero point seven. | On for your session best. |
| `on_pace_session_best_2` | Session best is on, you're zero point seven up. | Session best is on. |
| `on_pace_personal_best` | This is personal best pace, up zero point seven. | This is personal best pace. |
| `on_pace_personal_best_2` | Personal best on the cards, zero point seven up. Bring it home. | Personal best on the cards. Bring it home. |
| `on_pace_record` | Track record pace, up zero point seven. Nail this one. | Track record pace. Nail this one. |
| `on_pace_record_2` | Record pace, zero point seven up. Don't leave anything out. | Record pace. Don't leave anything out. |
| `lap_invalidated` | That lap didn't count. | That lap didn't count. |
| `lap_invalidated_2` | Lap's been struck out. | Lap's been struck out. |
| `lap_invalidated_3` | No good, that one's gone. | No good, that one's gone. |
| `fastest_lap_you` | Fastest lap, nice work, one forty eight point two. | Fastest lap, nice work. |
| `fastest_lap_you_2` | You're quickest, one forty eight point two. | You're quickest. |
| `fastest_lap_you_3` | That's the fastest of the session, one forty eight point two. | That's the fastest of the session. |
| `personal_best` | New personal best, one forty eight point two. | New personal best. |
| `personal_best_2` | That's a new best for you, one forty eight point two. | That's a new best for you. |
| `personal_best_3` | Personal best, one forty eight point two, two point four slower than your ideal. | Personal best. |
| `record_beaten` | New track record! one forty eight point two. | New track record! |
| `record_beaten_2` | Track record, one forty eight point two. That's the new benchmark. | Track record. That's the new benchmark. |
| `session_best` | Session best, one forty eight point two. | Session best. |
| `session_best_2` | Best of your session, one forty eight point two, one point nine slower than your all time. | Best of your session. |
| `gap_behind` | Behind, two point one, closed zero point four. | Behind. |
| `gap_behind_2` | Behind you, rider four seventy six, two point one, closed zero point four. | Behind you. |
| `gap_behind_3` | Behind you now, two point one, closed zero point four. | Behind you now. |
| `gap_behind_4` | Behind, two point one, they ran one forty seven point nine. | Behind. |
| `final_lap_you` | Last lap, make it count. | Last lap, make it count. |
| `final_lap_you_2` | This is your last lap. | This is your last lap. |
| `final_lap_you_3` | Last one, one point two to rider sixty five. | Last one. |
| `overtime_started` | Overtime, two laps after this one, you're P four. | Overtime, after this one. |
| `session_time_expired` | Time's up. | Time's up. |
| `session_time_expired_2` | That's time. | That's time. |
| `ten_minutes_remaining` | Ten minutes to go. | Ten minutes to go. |
| `ten_minutes_remaining_2` | Ten minutes left, you're P four. | Ten minutes left. |
| `five_minutes_remaining` | Five minutes left. | Five minutes left. |
| `five_minutes_remaining_2` | Five to go, P four. | Five to go. |
| `halfway_point` | Halfway there. | Halfway there. |
| `halfway_point_2` | Half distance, you're P four. | Half distance. |
| `fastest_lap_other` | Fastest lap, rider four seventy six, one forty eight point two, one point one slower than your best. | Fastest lap. |
| `fastest_lap_other_2` | Rider four seventy six goes quickest, one forty eight point two. | Goes quickest. |
| `leader_other` | New leader, rider four seventy six. | New leader. |
| `leader_other_2` | Rider four seventy six has the lead now. | Has the lead now. |
| `leader_other_3` | Lead change, rider four seventy six out front. | Lead change, out front. |
| `finished_leader` | Leader's taken the flag. | Leader's taken the flag. |
| `finished_leader_2` | Flag's out, the leader's home. | Flag's out, the leader's home. |
| `rider_behind` | Rider behind. | Rider behind. |
| `rider_behind_2` | Someone's tucked in behind you. | Someone's tucked in behind you. |
| `rider_behind_3` | Company behind, rider four seventy six. | Company behind. |
| `rider_behind_4` | You've got a rider on you. | You've got a rider on you. |
| `rider_behind_clear` | Clear. | Clear. |
| `rider_behind_clear_2` | All clear behind. | All clear behind. |
| `rider_behind_clear_3` | Track's yours, two point one back. | Track's yours. |
| `rider_left` | Rider left. | Rider left. |
| `rider_left_2` | Left side. | Left side. |
| `rider_left_3` | On your left. | On your left. |
| `rider_right` | Rider right. | Rider right. |
| `rider_right_2` | Right side. | Right side. |
| `rider_right_3` | On your right. | On your right. |
| `riders_both_sides` | Riders both sides, hold your line. | Riders both sides, hold your line. |
| `riders_both_sides_2` | You're sandwiched, hold your line. | You're sandwiched, hold your line. |
| `riders_both_sides_3` | Riders left and right. | Riders left and right. |
| `lapping_traffic` | Backmarker ahead, rider four seventy six. | Backmarker ahead. |
| `lapping_traffic_2` | Lapper ahead, pick your line. | Lapper ahead, pick your line. |
| `lapping_traffic_3` | Traffic ahead, rider four seventy six. | Traffic ahead. |
| `blue_flag` | Blue flag, faster rider closing, rider four seventy six. | Blue flag, faster rider closing. |
| `blue_flag_2` | Blue flag, let them by. | Blue flag, let them by. |
| `blue_flag_3` | Blue flag, rider four seventy six closing, hold your line and let them by. | Blue flag, hold your line and let them by. |
| `hazard_ahead` | Caution, rider down ahead. | Caution, rider down ahead. |
| `hazard_ahead_2` | Rider down ahead, eyes up. | Rider down ahead, eyes up. |
| `hazard_ahead_3` | Careful, someone's down on track ahead. | Careful, someone's down on track ahead. |
| `wrong_way_ahead` | Heads up, wrong way rider ahead! | Heads up, wrong way rider ahead! |
| `wrong_way_ahead_2` | Wrong way rider ahead, be ready. | Wrong way rider ahead, be ready. |

115 rows.
