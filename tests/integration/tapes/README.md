# Master callback tapes

Drop **full** recorder captures here. To make one, set `[Recorder] enabled=1` in
the plugin INI (MX Bikes only — the hidden in-plugin recorder, no HUD/hotkey) and
play a session; the game writes `*.tape` files to
`Documents\PiBoSo\MX Bikes\mxbmrp3\tapes\`. They're
**git-ignored** — masters are large (~10 MB/session) and one-shot, so they don't
belong in history. Keep them archived elsewhere (the recording zip, a GitHub
release asset, cloud); this folder is just the working home you slim from.

## Master vs fixture

- **Master** = the whole capture. Records *every* callback, because at record
  time you don't know which feature you'll test next — and you can slim a tape
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

The current golden fixture (`race2_mxbclub_1lap`) is a **solo** session — great
for the session-result golden master, but it can't exercise anything relative to
*other* riders. **Live gaps, position battles, the director, map spacing, and
position deltas all need a multi-rider capture.** A short race with a handful of
riders and an overtake or two would unlock real-data tests for those — slim it
with `--profile gaps` and it stays committable (~0.7 MB gzipped for a short one).
