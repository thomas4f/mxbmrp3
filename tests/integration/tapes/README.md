# Master callback tapes

Drop **full** recorder captures here. To make one, set `[Recorder] enabled=1` in
the plugin INI (MX Bikes only - the hidden in-plugin recorder, no HUD/hotkey) and
play a session; the game writes `*.tape` files to
`Documents\PiBoSo\MX Bikes\mxbmrp3\tapes\`. They're
**git-ignored** - masters are large (~10 MB/session) and one-shot, so they don't
belong in history. Keep them archived elsewhere (the recording zip, a GitHub
release asset, cloud); this folder is just the working home you slim from.

## Master vs fixture

- **Master** = the whole capture. Records *every* callback, because at record
  time you don't know which feature you'll test next - and you can slim a tape
  down later, but you can't un-slim it (recover dropped events) without recording
  again. Never slim a master in place.
- **Fixture** = a small, committed slice for one test, in `../tests/fixtures/`
  (gzipped). Keeps only the event types that test needs.

## Deriving a fixture

```bash
python3 ../slim_tape.py tapes/session_XXXX.tape /tmp/out.tape --profile gaps --stats
gzip -9 -c /tmp/out.tape > ../tests/fixtures/my_scenario.tape.gz
```

Profiles (see `slim_tape.py`): `min` (snapshot state → ~tiny; the golden test
uses it), `gaps` (+ track positions → live gaps / map / sectors), `all`
(+ telemetry/vehicle → big; telemetry is usually better tested via hooks).

## What to capture next

What each committed fixture can drive, since the answer is not its name:

| Fixture | Riders | Track positions | Drives |
|---|---|---|---|
| `race2_mxbclub_1lap` | solo | no | session-result golden master |
| `race_farm14_24riders` | 24 | **no** | standings, laps, penalties, gaps |
| `spotter_demo_weekend` | 6 | 1842 | everything, incl. proximity and hazards |
| `synthetic_positions_22riders` | 22 | 600 | map spacing, radar, position math |

**Track positions are what proximity, hazards, blue flags and the director need**,
and a fixture slimmed with `--profile min` has none - `race_farm14_24riders` is
the trap, because 24 riders reads like it must exercise everything and it cannot
fire a single proximity cue. `--profile gaps` is the one that keeps them.

Still worth capturing: a real multi-rider race *with* positions. The demo weekend
has them but is only six riders, so nothing there is a crowd.
