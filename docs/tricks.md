# FMX tricks

GENERATED from `mxbmrp3/core/fmx_types.h` and `mxbmrp3/core/fmx_scoring.h` by `tests/unit/test_fmx_scoring.cpp` - do not edit. When a trick or a number changes, run the unit gate and copy `/tmp/tricks.new.md` over this file.

What the FMX HUD recognises, what each trick is worth and how a score is built, at the defaults. Left and right variants are one trick here, as in the settings: `trickEnabled_<INI key>=0` under `[FmxHud]` in the settings file turns one off, both directions at once. *Base* is the score before the multipliers below; *Scored on* is the axis whose rotation scales it; *Full progress* is what fills the HUD's bar.

## Ground tricks

A ground trick counts once it reaches 25% of its full progress, so a momentary blip never scores.

| Trick | INI key | Base | Scored on | Full progress | Recognised by |
|---|---|---|---|---|---|
| Wheelie | `Wheelie` | 10 | pitch | 2 s held | Rear wheel only, nose up past 15 deg (ends below half that) |
| Coaster Wheelie | `CoasterWheelie` | 10 | pitch | 2 s held | A wheelie with the clutch held in; letting it out drops it back to a wheelie |
| Endo | `Endo` | 15 | pitch | 2 s held | Front wheel only, nose down past 15 deg, moving |
| Stoppie | `Stoppie` | 20 | pitch | 2 s held | An endo under 9 km/h |
| Burnout | `Burnout` | 5 | - | 3 s held | Rear wheel spinning 18 km/h faster than the bike, under 9 km/h |
| Donut | `Donut` | 25 | yaw | 3 s held | A burnout turned through 45 deg |
| Drift (L/R) | `Drift` | 15 | - | 3 s held | Sliding at a slip angle past 30 deg, moving |
| Pivot (L/R) | `Pivot` | 40 | yaw | 180 deg of yaw | A wheelie or endo turned through 67.5 deg under 5 km/h |

## Air tricks

An air trick classifies once the bike has been off the ground 0.3 s; before that a bump is not a trick. Full rotations (270 deg) are read first, then the turns, then whip and scrub, then plain air.

| Trick | INI key | Base | Scored on | Full progress | Recognised by |
|---|---|---|---|---|---|
| Air | `Air` | 5 | - | 2 s airborne | Airborne 0.3 s with no rotation past a threshold |
| Backflip | `Backflip` | 100 | pitch | 360 deg of pitch | Pitched backward through 270 deg |
| Frontflip | `Frontflip` | 150 | pitch | 360 deg of pitch | Pitched forward through 270 deg |
| Barrel Roll (L/R) | `BarrelRoll` | 80 | roll | 360 deg of roll | Rolled through 270 deg |
| Scrub (L/R) | `Scrub` | 30 | roll | 90 deg of roll | Rolled, or took off leaned, past 30 deg, short of a barrel roll |
| Whip (L/R) | `Whip` | 25 | yaw | 90 deg of yaw | Yawed past 30 deg with the nose level, short of a spin |
| Spin (L/R) | `Spin` | 120 | yaw | 360 deg of yaw | Yawed through 270 deg |
| Oppo (L/R) | `Oppo` | 60 | yaw | 90 deg of yaw | Yawed past 67.5 deg with the nose up past 67.5 deg |
| Turn Down (L/R) | `TurnDown` | 60 | yaw | 90 deg of yaw | Yawed past 67.5 deg with the nose down past 67.5 deg |
| Flat 360 (L/R) | `Flat360` | 180 | pitch | 360 deg of pitch or roll | A flip rolled between 80 deg and 180 deg |

## Scoring

- **Rotation.** A trick with an axis is scaled by its peak rotation over 360 deg, never below 1x: a 540 deg backflip is 1.5x the base.
- **Air bonus.** An air trick is then scaled by 1 + 0.25 per second airborne + 0.01 per metre covered.
- **Ground bonus.** A ground trick is scaled by its duration over its full-progress time (never below 1x) + 0.01 per metre covered, so a stationary trick earns no distance.
- **Coaster.** A coaster wheelie adds up to 10 points, in proportion to how much of it the clutch was held, before the ground bonus.
- **Chain.** A landed trick is confirmed after 0.75 s (a crash in that window fails it), then 2 s are open for the next trick. Every trick after the first adds 0.5 to the chain's multiplier (two tricks 1.5x, three 2x); a trick already in the chain adds 0.5x what its previous occurrence did. A crash loses the whole chain.
