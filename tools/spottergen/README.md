# mxbmrp3_spottergen - spotter voice-pack baker

`generate.py` bakes the spotter's RECORDED voice packs, which are published as
a separate download rather than bundled - tens of megabytes of wav for a
feature that ships off. Bake into `mxbmrp3_data/spotters/<voice>/` to test one
locally (that folder is where the plugin looks), but do not commit the wavs;
the only pack in the tree is the text-only `default`, whose ini carries the
built-in wording. This README is the map for extending the
spotter subsystem end to end; each mechanism's detail lives in its own
header (the repo's reading-order convention: the header first, then the
test that pins it).

## The subsystem in one paragraph

Race events funnel through `PluginData::addEventLogEntry` and the
proximity/hazard detectors tick in `SpotterManager::onTrackPositions`
(`core/spotter_manager_proximity.cpp`); both end in `emitCue()`, which resolves a
**cue key** against the active pack - variant pick, phrase template
(subtitle + TTS fallback), then the audio ladder `_mix` (chunk stitch) >
`_wav` (whole clip) > TTS. The pack format spec is the header comment of
`core/spotter_cue_pack.h`; chunk naming and the hundreds-split fallback are
in `core/spotter_mix.h`; phrase wording (racing-style numbers, lap-time
tenths) is `core/spotter_phrase.h`; the edge/cooldown state machine is
`core/spotter_hazard.h`. Settings: the Spotter tab
(`hud/settings/settings_tab_spotter.cpp`) + the `[Spotter]` INI section
(`core/settings_manager_global_features.cpp`).

## Adding a new cue (event or detector)

1. **Source.** An event-log event: add the `EventLogType` → key mapping in
   `spotter_cue_pack.h cueKeyFor()` and the built-in phrase in
   `spotter_phrase.h compose()` (+ its category in `categoryFor()`).
   A detector cue: extend `spotter_hazard.h`'s state machine and emit in
   `SpotterManager::onTrackPositions` with a new literal key.
2. **The key is frozen API** once shipped - packs reference it; renaming
   orphans every pack's override (same rule as hotkey config names).
3. **Bake it**: add the key and its one short line to `CUES` in
   `generate.py` (or to `MUTED` if the cue is only a number), rerun -
   incremental, only new clips render. Un-baked keys fall through to the
   shipped default pack's wording, so shipping the code first is safe.
4. **Tests**: wording in `tests/unit/test_spotter_phrase.cpp`, state-machine
   edges in `test_spotter_hazard.cpp`, the end-to-end decision in
   `tests/integration/tests/spotter_test.cpp` (cue log via `spotterCueLog()`).

## Adding a new voice / speech model

- **Another voice on a supported engine**: add a row to `VOICES` in
  `generate.py` (Kokoro/Kitten speaker name, an Orpheus voice id, or a
  Chatterbox reference recording) and run
  `python3 generate.py --voices <name>`. Models download themselves; the
  header lists the one-time `pip install`. Hand out the zip - wavs are never
  committed (see the top of this file).
- **A different TTS engine or a human recording**: anything may produce the
  clips as long as it meets the pack contract - 16-bit mono PCM wavs at one
  rate per pack, the cue keys in `CUES`, and `seg_p` + `num_1..40` for the
  position stitch. Number wording MUST match `SpotterPhrase::numberWords`;
  the shared contract is `tests/fixtures/spotter_number_words.txt`, asserted
  from C++ by `test_spotter_phrase.cpp` and from this tool by
  `generate.py --selftest` (the `spottergen-selftest` CI gate), which also
  checks every key in `CUES` still exists in the plugin's registry.

## Pack anatomy / player-facing format

See the header of `core/spotter_cue_pack.h` - it is written as the spec a
pack author reads: phrases, `_wav`, `_mix` recipes, `_2.._9` variants,
empty-value mutes, hot reload via the Reload Config hotkey.
