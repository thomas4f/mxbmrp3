# The shipped spotter pack, rendered

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

<sub>Generated from `mxbmrp3_data/spotters/default/spotter.ini` by `tests/unit/test_spotter_pack_census.cpp` - do not edit, edit the pack. Run the unit gate, which rewrites it and fails while this copy is stale.</sub>

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
| `trick_started` | Here we go! | Here we go! |
| `trick_started_2` | Ooh, what's this? | Ooh, what's this? |
| `trick_started_3` | Going for it! | Going for it! |
| `trick_started_4` | Oh no. Somebody call an ambulance. | Oh no. Somebody call an ambulance. |
| `trick_started_5` | Let's see it. | Let's see it. |
| `trick_started_6` | Send it! | Send it! |
| `trick_started_7` | Is this happening? | Is this happening? |
| `trick_started_8` | Don't look down. | Don't look down. |
| `trick_started_9` | Hold my beer. | Hold my beer. |
| `trick_landed` | Double backflip, stuck it! | Stuck it! |
| `trick_landed_2` | Clean double backflip! | Clean ! |
| `trick_landed_3` | Double backflip! The crowd goes mild. | ! The crowd goes mild. |
| `trick_landed_4` | That double backflip was filthy! | That was filthy! |
| `trick_landed_5` | Landed the double backflip. Barely. | Landed the. Barely. |
| `trick_landed_6` | Double backflip. Fine. I've seen better. | . Fine. I've seen better. |
| `trick_landed_7` | Oh, that double backflip was gorgeous! | Oh, that was gorgeous! |
| `trick_landed_8` | Double backflip, and you're still alive. Respect. | And you're still alive. Respect. |
| `trick_landed_9` | Textbook double backflip. Boring, but textbook. | Textbook. Boring, but textbook. |
| `trick_crashed` | Booooo! | Booooo! |
| `trick_crashed_2` | Ohhh no! Booo! | Ohhh no! Booo! |
| `trick_crashed_3` | Denied! Booooo! | Denied! Booooo! |
| `trick_crashed_4` | Sigh. And you were doing so well. | Sigh. And you were doing so well. |
| `trick_crashed_5` | Yard sale! | Yard sale! |
| `trick_crashed_6` | That's going on the blooper reel. | That's going on the blooper reel. |
| `trick_crashed_7` | My grandmother lands that. Blindfolded. | My grandmother lands that. Blindfolded. |
| `trick_crashed_8` | The crowd demands a refund! | The crowd demands a refund! |
| `trick_crashed_9` | Ouch. Somebody fetch a shovel. | Ouch. Somebody fetch a shovel. |
| `trick_combo` | Five trick combo! | Trick combo! |
| `trick_combo_2` | Combo, five tricks! | Combo, tricks! |
| `trick_combo_3` | Five in a row! Now you're just showing off. | In a row! Now you're just showing off. |
| `trick_combo_4` | That's a five trick chain. Somebody's been practising. | That's a trick chain. Somebody's been practising. |
| `trick_combo_5` | Five tricks. Okay, okay, we get it, you're good. | Tricks. Okay, okay, we get it, you're good. |
| `trick_combo_6` | Five tricks, no breaks. Breathe. | Tricks, no breaks. Breathe. |
| `trick_combo_7` | Combo! five of them. Cool, I guess. | Combo! of them. Cool, I guess. |
| `trick_combo_8` | Five in one go. Your mum would be proud. Probably. | In one go. Your mum would be proud. Probably. |
| `trick_combo_9` | A five trick combo. Somebody clip that. | A trick combo. Somebody clip that. |
| `achievement_distance` | Long Hauler, gold. Your odometer wants a word. | Long Hauler. Your odometer wants a word. |
| `achievement_distance_2` | Long Hauler, gold. That many kilometres, and still no sat nav. | Long Hauler. That many kilometres, and still no sat nav. |
| `achievement_ride_time` | Seat Time, gold. Have you considered going outside? | Seat Time. Have you considered going outside? |
| `achievement_ride_time_2` | Seat Time, gold. Your chair has a dent shaped like you now. | Seat Time. Your chair has a dent shaped like you now. |
| `achievement_laps` | Lap Counter, gold. Round and round we go. | Lap Counter. Round and round we go. |
| `achievement_laps_2` | Lap Counter, gold. Every one of them a left turn. Probably. | Lap Counter. Every one of them a left turn. Probably. |
| `achievement_fuel_burnt` | Carbon Footprint. The polar bears send their regards. | Carbon Footprint. The polar bears send their regards. |
| `achievement_gear_shifts` | Shift Happens, gold. | Shift Happens. |
| `achievement_session_laps` | Marathon, gold. One session, that many laps. Drink some water. | Marathon. One session, that many laps. Drink some water. |
| `achievement_session_laps_2` | Marathon, gold. Somebody check on this person. | Marathon. Somebody check on this person. |
| `achievement_session_time` | Iron Butt, gold. Your backside has filed a complaint. | Iron Butt. Your backside has filed a complaint. |
| `achievement_track_laps` | Local Hero, gold. You could ride this track in your sleep. You probably have. | Local Hero. You could ride this track in your sleep. You probably have. |
| `achievement_bike_km` | Loyal, gold. You and that bike should get matching tattoos. | Loyal. You and that bike should get matching tattoos. |
| `achievement_races` | Racer, gold. Another one in the books. | Racer. Another one in the books. |
| `achievement_races_2` | Racer, gold. Gate drops counted. Results not. | Racer. Gate drops counted. Results not. |
| `achievement_wins` | Winner, gold! Champagne's on you. | Winner! Champagne's on you. |
| `achievement_wins_2` | Winner, gold. Don't let it go to your head. Too late. | Winner. Don't let it go to your head. Too late. |
| `achievement_podiums` | Podium Regular, gold. They know your face up there now. | Podium Regular. They know your face up there now. |
| `achievement_podiums_2` | Podium Regular, gold. Spray the champagne, not the fans. | Podium Regular. Spray the champagne, not the fans. |
| `achievement_lapped_field` | Lapped the Field. Every last one of them. That's just rude. | Lapped the Field. Every last one of them. That's just rude. |
| `achievement_fastest_laps` | Hot Lapper, gold. Fastest lap, nobody quicker. | Hot Lapper. Fastest lap, nobody quicker. |
| `achievement_fastest_laps_2` | Hot Lapper, gold. Fastest lap again. Leave some for the rest. | Hot Lapper. Fastest lap again. Leave some for the rest. |
| `achievement_rain_races` | Mudder, gold. You finished that in the rain. Your goggles did not. | Mudder. You finished that in the rain. Your goggles did not. |
| `achievement_big_grids` | Crowd Surfer, gold. Twenty plus riders, and you made it out. | Crowd Surfer. Twenty plus riders, and you made it out. |
| `achievement_holeshots` | Holeshot-ish, gold. First to the split. Ish. | Holeshot-ish. First to the split. Ish. |
| `achievement_charger` | Charger, gold. Up through the field like they weren't even there. | Charger. Up through the field like they weren't even there. |
| `achievement_charger_2` | Charger, gold. Started at the back on purpose, sure. | Charger. Started at the back on purpose, sure. |
| `achievement_last_lap_pass` | Last Gasp, gold. Last lap pass. They'll be thinking about that one. | Last Gasp. Last lap pass. They'll be thinking about that one. |
| `achievement_survivor` | Survivor, gold. A quarter of them quit. You didn't. | Survivor. A quarter of them quit. You didn't. |
| `achievement_roost` | Roost, gold. You've eaten a lot of somebody else's dirt. | Roost. You've eaten a lot of somebody else's dirt. |
| `achievement_clean_races` | Rubber Side Down, gold. No crashes. Who are you and what have you done with the real you? | Rubber Side Down. No crashes. Who are you and what have you done with the real you? |
| `achievement_clean_races_2` | Rubber Side Down, gold. Not a scratch. Boring, but impressive. | Rubber Side Down. Not a scratch. Boring, but impressive. |
| `achievement_penalty_free` | Clean Sheet, gold. No penalties. The marshals are bored. | Clean Sheet. No penalties. The marshals are bored. |
| `achievement_wire_to_wire` | Wire to Wire, gold. Led every lap. Nobody else got a look. | Wire to Wire. Led every lap. Nobody else got a look. |
| `achievement_perfect_race` | Perfect Race. Holeshot, every lap led, fastest lap, the win. Retire now, it only gets worse. | Perfect Race. Holeshot, every lap led, fastest lap, the win. Retire now, it only gets worse. |
| `achievement_clean_laps` | Spotless, gold. Achievement unlocked. | Spotless. Achievement unlocked. |
| `achievement_steady_hands` | Steady Hands. That much riding without a crash. Suspicious. | Steady Hands. That much riding without a crash. Suspicious. |
| `achievement_metronome` | Metronome. Five laps within a tenth. Are you a robot? | Metronome. Five laps within a tenth. Are you a robot? |
| `achievement_crashes` | Skill Issue. Says it all, really. | Skill Issue. Says it all, really. |
| `achievement_crashes_2` | Skill Issue. I'm not saying it's you. It's you. | Skill Issue. I'm not saying it's you. It's you. |
| `achievement_crashes_3` | Skill Issue. Again. Shocking, truly. | Skill Issue. Again. Shocking, truly. |
| `achievement_penalties` | Rule Bender. The rules are more of a suggestion, apparently. | Rule Bender. The rules are more of a suggestion, apparently. |
| `achievement_penalty_time` | Time Served. You've done your time. Try not to reoffend. | Time Served. You've done your time. Try not to reoffend. |
| `achievement_so_close` | So Close. Under a tenth. Sigh. Just, sigh. | So Close. Under a tenth. Sigh. Just, sigh. |
| `achievement_back_marker` | Back Marker. A lap down. At least you finished. Technically. | Back Marker. A lap down. At least you finished. Technically. |
| `achievement_chain_lost` | Weakest Link. That chain was worth a lot. Was. | Weakest Link. That chain was worth a lot. Was. |
| `achievement_ninety_nine` | 99 Problems. And crashing is all of them. | 99 Problems. And crashing is all of them. |
| `achievement_rage_quit` | Rage Quit. Quitter. There, I said it. | Rage Quit. Quitter. There, I said it. |
| `achievement_bakers_dozen` | Baker's Dozen. Thirteen crashes in one session. That's not a session, that's a demolition derby. | Baker's Dozen. Thirteen crashes in one session. That's not a session, that's a demolition derby. |
| `achievement_ran_dry` | Long Walk Home. Out of fuel. The fuel gauge was right there. | Long Walk Home. Out of fuel. The fuel gauge was right there. |
| `achievement_peak_g` | Big Hit. That one's going to hurt tomorrow. | Big Hit. That one's going to hurt tomorrow. |
| `achievement_mondays` | Case of the Mondays. A five trick chain, straight into the dirt. | Case of the Mondays. A five trick chain, straight into the dirt. |
| `achievement_choke` | Choke. Leading into the last lap, and you gave it away. Wow. | Choke. Leading into the last lap, and you gave it away. Wow. |
| `achievement_choke_2` | Choke. I can't even look at you right now. | Choke. I can't even look at you right now. |
| `achievement_consolation` | Consolation Prize. Fastest lap, off the podium. Here's a participation trophy. | Consolation Prize. Fastest lap, off the podium. Here's a participation trophy. |
| `achievement_pile_up` | Pile-Up. Four of you down together. Bowling, but with motorbikes. | Pile-Up. Four of you down together. Bowling, but with motorbikes. |
| `achievement_favorite_spot` | Favorite Spot. Same place, three times. Maybe build a house there. | Favorite Spot. Same place, three times. Maybe build a house there. |
| `achievement_tracks` | Globetrotter, gold. Another track ticked off. | Globetrotter. Another track ticked off. |
| `achievement_tracks_2` | Globetrotter, gold. New track, same old you. | Globetrotter. New track, same old you. |
| `achievement_bikes` | Collector, gold. That's a lot of bikes. Your garage is full. | Collector. That's a lot of bikes. Your garage is full. |
| `achievement_bikes_2` | Collector, gold. You can only ride one at a time, you know. | Collector. You can only ride one at a time, you know. |
| `achievement_personal_bests` | Personal Best, gold. Faster than you've ever been. | Personal Best. Faster than you've ever been. |
| `achievement_personal_bests_2` | Personal Best, gold. Faster than ever. Faster than me, at least. | Personal Best. Faster than ever. Faster than me, at least. |
| `achievement_pb_leaps` | Leap Forward, gold. A whole second off your best. Where was that before? | Leap Forward. A whole second off your best. Where was that before? |
| `achievement_regular` | Regular, gold. Same time tomorrow, then. | Regular. Same time tomorrow, then. |
| `achievement_day_streak` | On a Roll, gold. Day after day after day. No judgement. Some judgement. | On a Roll. Day after day after day. No judgement. Some judgement. |
| `achievement_day_streak_2` | On a Roll, gold. Back again. Of course you are. | On a Roll. Back again. Of course you are. |
| `achievement_servers` | Well Travelled, gold. You get around. | Well Travelled. You get around. |
| `achievement_bike_classes` | Class Act, gold. | Class Act. |
| `achievement_fmx_tricks` | Show-off, gold. Yes, we saw. Everybody saw. | Show-off. Yes, we saw. Everybody saw. |
| `achievement_fmx_tricks_2` | Show-off, gold. The ground misses you. | Show-off. The ground misses you. |
| `achievement_fmx_points` | Style Points, gold. The judges are impressed. The judges are me. | Style Points. The judges are impressed. The judges are me. |
| `achievement_fmx_points_2` | Style Points, gold. Ten from the judges. Well, from me. | Style Points. Ten from the judges. Well, from me. |
| `achievement_fmx_chain` | Chain Reaction, gold. That chain was a thing of beauty. | Chain Reaction. That chain was a thing of beauty. |
| `achievement_fmx_kinds` | Trick Collector, gold. Gotta land them all. | Trick Collector. Gotta land them all. |
| `achievement_fmx_wheelie_km` | One-Wheel Tour, gold. The front wheel is basically decorative at this point. | One-Wheel Tour. The front wheel is basically decorative at this point. |
| `achievement_tyre_shredder` | Tyre Shredder, gold. Somebody has to pay for those tyres. | Tyre Shredder. Somebody has to pay for those tyres. |
| `achievement_air_miles` | Frequent Flyer, gold. Collecting air miles the hard way. | Frequent Flyer. Collecting air miles the hard way. |
| `achievement_air_miles_2` | Frequent Flyer, gold. You spend more time up there than down here. | Frequent Flyer. You spend more time up there than down here. |
| `achievement_air_distance` | Air Miles, gold. That's a lot of ground you didn't touch. | Air Miles. That's a lot of ground you didn't touch. |
| `achievement_air_height` | To the Moon, gold. Houston, we have a rider. | To the Moon. Houston, we have a rider. |
| `achievement_fmx_airtime` | Hang Time, gold. Up there long enough to check your email. | Hang Time. Up there long enough to check your email. |
| `achievement_jump_distance` | Gap Jumper, gold. Big gap, cleared. | Gap Jumper. Big gap, cleared. |
| `achievement_jump_height` | Sent It, gold. Fully, completely sent. | Sent It. Fully, completely sent. |
| `achievement_jump_height_2` | Sent It, gold. Air traffic control would like a word. | Sent It. Air traffic control would like a word. |
| `achievement_fmx_backflips` | Flipper, gold. Upside down, and back again. | Flipper. Upside down, and back again. |
| `achievement_fmx_backflips_2` | Flipper, gold. Round and round, and the right way up. | Flipper. Round and round, and the right way up. |
| `achievement_fmx_frontflips` | Somersault, gold. A frontflip. On purpose? Sure, let's say on purpose. | Somersault. A frontflip. On purpose? Sure, let's say on purpose. |
| `achievement_fmx_wheelie` | Wheelie King, gold. Long live the king. | Wheelie King. Long live the king. |
| `achievement_fmx_wheelie_2` | Wheelie King, gold. Front wheel? Never heard of it. | Wheelie King. Front wheel? Never heard of it. |
| `achievement_fmx_whips` | Whip It, gold. Whip it good. | Whip It. Whip it good. |
| `achievement_fmx_scrubs` | Scrubber, gold. Low and sideways, the way it should be. | Scrubber. Low and sideways, the way it should be. |
| `achievement_fmx_oppos` | Oppo, gold. Nose up, bars out. Stylish. | Oppo. Nose up, bars out. Stylish. |
| `achievement_fmx_endos` | Endo. Back wheel up, nose down, and you didn't go over the bars. This time. | Endo. Back wheel up, nose down, and you didn't go over the bars. This time. |
| `achievement_fmx_turn_downs` | Turn Down, gold. Achievement unlocked. | Turn Down. Achievement unlocked. |
| `achievement_all_bronze` | Bronze Sweep. Bronze in everything. Participation, but thorough. | Bronze Sweep. Bronze in everything. Participation, but thorough. |
| `achievement_all_silver` | Silver Sweep. Silver in everything. Nearly good at all of it. | Silver Sweep. Silver in everything. Nearly good at all of it. |
| `achievement_all_gold` | Gold Sweep. Gold in everything. Okay, now I'm actually impressed. | Gold Sweep. Gold in everything. Okay, now I'm actually impressed. |
| `achievement_all_platinum` | Platinum Sweep. Every single one, maxed out. Go outside. Please. I'm begging you. | Platinum Sweep. Every single one, maxed out. Go outside. Please. I'm begging you. |
| `achievement_tyre_kicker` | Tyre Kicker. You've tried every HUD. Pick one. | Tyre Kicker. You've tried every HUD. Pick one. |
| `achievement_grand_tour` | Grand Tour. Every settings tab. You read the manual, didn't you. | Grand Tour. Every settings tab. You read the manual, didn't you. |
| `achievement_on_air` | On Air. Web overlay connected. Wave to the stream. | On Air. Web overlay connected. Wave to the stream. |
| `achievement_second_screen` | Second Screen. Now the HUD is on two screens. As if one wasn't enough. | Second Screen. Now the HUD is on two screens. As if one wasn't enough. |
| `achievement_directors_cut` | Director's Cut. Achievement unlocked. | Director's Cut. Achievement unlocked. |
| `achievement_backseat` | Backseat Driver. You've heard me a lot. I'm not going anywhere. | Backseat Driver. You've heard me a lot. I'm not going anywhere. |
| `achievement_good_vibrations` | Good Vibrations. Rumble is on. Brrrr. | Good Vibrations. Rumble is on. Brrrr. |
| `achievement_keymaster` | Keymaster. Hotkey bound. Are you the gatekeeper? | Keymaster. Hotkey bound. Are you the gatekeeper? |
| `achievement_profile_hopper` | Profile Hopper. Can't make up your mind? | Profile Hopper. Can't make up your mind? |
| `achievement_fresh_coat` | Fresh Coat. Updated in-game. Smells new. | Fresh Coat. Updated in-game. Smells new. |
| `achievement_split_decision` | Split Decision. Segment timed. Very scientific. | Split Decision. Segment timed. Very scientific. |
| `achievement_armchair` | Armchair Marshal. Lots of watching, not a lot of riding. | Armchair Marshal. Lots of watching, not a lot of riding. |
| `achievement_stalker` | Stalker. That's a lot of riders you're keeping tabs on. Creepy. | Stalker. That's a lot of riders you're keeping tabs on. Creepy. |
| `achievement_breakout` | Brick Breaker. Wrong game, but nicely done. | Brick Breaker. Wrong game, but nicely done. |
| `achievement_config_reloads` | Tinkerer. Config reloaded. | Tinkerer. Config reloaded. |
| `achievement_decorator` | Interior Decorator. Ooh, a new look. Very fancy. | Interior Decorator. Ooh, a new look. Very fancy. |
| `achievement_dress_up` | Made It Yours. Pack installed. Looking sharp. | Made It Yours. Pack installed. Looking sharp. |
| `achievement_stylist` | Stylist. Custom CSS. You absolute nerd. | Stylist. Custom CSS. You absolute nerd. |
| `achievement_ungrateful` | Ungrateful. You turned the achievement toasts off. I'll just say them out loud instead. | Ungrateful. You turned the achievement toasts off. I'll just say them out loud instead. |
| `achievement_factory_fresh` | Factory Fresh. Everything back to default. All that work, gone. | Factory Fresh. Everything back to default. All that work, gone. |
| `achievement_prerelease` | Early Access. Pre-release channel. Brave. Or foolish. | Early Access. Pre-release channel. Brave. Or foolish. |
| `achievement_frame_perfect` | Frame Perfect. That's a lot of frames. Show-off. | Frame Perfect. That's a lot of frames. Show-off. |
| `achievement_phoenix` | Phoenix. Back after a crash to desktop. Let's not talk about it. | Phoenix. Back after a crash to desktop. Let's not talk about it. |
| `achievement_test_pilot` | Test Pilot. Experimental settings. What could possibly go wrong. | Test Pilot. Experimental settings. What could possibly go wrong. |
| `achievement_under_hood` | Under the Hood. You edited the settings file by hand. Hacker. | Under the Hood. You edited the settings file by hand. Hacker. |
| `achievement_backup_plan` | Backup Plan. You edited the stats file by hand. I saw that. Cheater. | Backup Plan. You edited the stats file by hand. I saw that. Cheater. |
| `achievement_homebrew` | Homebrew. You built it yourself. Respect. | Homebrew. You built it yourself. Respect. |
| `achievement_developer` | Developer. Developer mode. Don't touch anything. | Developer. Developer mode. Don't touch anything. |
| `achievement_night_owl` | Night Owl. It's the middle of the night. Go to bed. | Night Owl. It's the middle of the night. Go to bed. |
| `achievement_anniversary` | Anniversary. A year of this. Happy anniversary, I guess. | Anniversary. A year of this. Happy anniversary, I guess. |
| `achievement_solo_race` | Just Me Then. A race nobody else started. You won. Congratulations, I suppose. | Just Me Then. A race nobody else started. You won. Congratulations, I suppose. |
| `achievement_peace_out` | Peace Out. Three riders down and you rode straight through. Bye! | Peace Out. Three riders down and you rode straight through. Bye! |
| `achievement_digging` | Digging a Hole. Spinning in place. Keep going, you'll reach Australia. | Digging a Hole. Spinning in place. Keep going, you'll reach Australia. |
| `achievement_sandbagger` | Sandbagger. A PB on the last lap. Where was that all race? | Sandbagger. A PB on the last lap. Where was that all race? |
| `achievement_palindrome` | Palindrome. Same lap time backwards. Weird flex, but okay. | Palindrome. Same lap time backwards. Weird flex, but okay. |
| `achievement_deja_vu` | Deja Vu. Two identical lap times. Have we met before? | Deja Vu. Two identical lap times. Have we met before? |
| `achievement_photo_finish` | Photo Finish. Under a tenth! Too close. My heart. | Photo Finish. Under a tenth! Too close. My heart. |
| `achievement_fumes` | Running on Fumes. One percent of a tank. Living dangerously. | Running on Fumes. One percent of a tank. Living dangerously. |

288 rows.
