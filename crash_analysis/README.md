# Crash Analysis

Tools + catalogue for triaging plugin/game crash minidumps. Catalogued crashes so far all
fault **inside `mxbikes.exe` (the game), not the plugin** - in every captured dump the
faulting instruction is in `mxbikes.exe` and the live stack is game + GPU/audio driver code,
with the plugin only present as stale stack residue. The plugin "owns" the dumps because its
crash handler is a process-global filter that catches any fault in the process.

Drop crash files here for triage. **Only the `.dmp` is required** - logs are optional.

- `*.dmp` - minidump written by the SEH crash filter to `<savePath>\mxbmrp3\crashes\`
- `*.log` - paired session log (optional; adds *what the player was doing*, not crash identity)

> **Dumps and logs are git-ignored and never committed.** They're per-investigation and
> belong on throwaway analysis branches that don't merge. Only the durable artifacts live
> in the repo on the main line: `known_game_crashes.json` (registry), `KNOWN_GAME_CRASHES.md`
> (catalogue), this README, and the `tools/mdmp_analyze.py` tooling. Per-dump deep analysis
> (disassembly, stack scans, session narrative) stays on the investigation branch.

## Analyze

```
python3 tools/mdmp_analyze.py crash_analysis/<file.dmp>
```

Compare two dumps' signatures (also flags if they're different game builds, and if they're
the same re-sent file):

```
python3 tools/mdmp_analyze.py --compare crash_analysis/<a.dmp> crash_analysis/<b.dmp>
```

## Source-file provenance (`samples`)

Once a crash is catalogued, you'll often want to remember **which `.dmp`/`.log` files prove
it** (and attach context like a video). That lives in an optional `samples` list inside each
crash entry in `known_game_crashes.json` — record it with `--record`:

```
python3 tools/mdmp_analyze.py --record crash_analysis/<file.dmp>
```

This analyzes the dump, finds the matching catalogued crash, and appends a sample
(`source` filename, `sha256`, `captured`, `build`, `fault`, `note`) to that crash.
Behaviour:

- **Matched crash → appended** (idempotent by `sha256`; re-running won't duplicate).
- **No match → reported, not added.** The catalogue tracks only *understood* crashes, so a
  dump with an unrecognised signature is never recorded — catalogue the crash first.
- **Add a note** (e.g. a video link or session context) with `--note`, which also works on a
  dump that's already on file (it updates that sample's note):

  ```
  python3 tools/mdmp_analyze.py --record --note "https://youtu.be/…" crash_analysis/<file.dmp>
  ```

  Notes are **never auto-filled** — they're yours to write. `--note` without `--record` is ignored.

`samples` is **internal provenance** (`source`/`sha256`/`fault`/…) and is **not** emitted into the
player-facing `KNOWN_GAME_CRASHES.md`. The `source` basename is the real crash filename, whose
`<date>_<pid>` suffix also names the paired `.log`. It records only the files behind a
*catalogued* crash; an unmatched dump is never stored (catalogue the crash first).

## Upload-and-go workflow

Everything needed to identify a crash is **inside the `.dmp`**, so the tool works on any
dump regardless of plugin version, game version, or whether a log was uploaded:

1. **Game build** - the host EXE's PE `TimeDateStamp` is read from the dump's module
   record (the `build:` line) - the same value the plugin logs as `Host build:` and that
   Windows' Application event log shows as the faulting image's time stamp. This does
   **not** depend on the `.log` or on the plugin emitting that line - the log line is just
   a convenience for reading the build without the tool.
2. **Crash identity** - the tool matches the faulting **instruction bytes** against
   `known_game_crashes.json` and prints a `[KNOWN CRASH]` section. Matching is on bytes + AV
   kind, which are **build-independent** (the same function has the same bytes across game
   builds), so a dump from an older or future build still matches - it just reports that
   the build isn't catalogued yet and that offsets will differ.
3. **New signatures** - if nothing matches, the tool says so and points you here to add it.

So a bare `.dmp` from any build → build fingerprint + known-crash match (or "new
signature") with zero other inputs. A `.log`, if present, adds the circumstance.

## Maintaining the catalogue

`known_game_crashes.json` is the **single source of truth** (byte signatures, AV kind,
mechanism, per-build offsets). `KNOWN_GAME_CRASHES.md` is a **generated, human-friendly
view** - never hand-edit it. After changing the JSON, regenerate the Markdown:

```
python3 tools/gen_known_crashes.py
```

To add or extend an entry in `known_game_crashes.json`:

- **New crash:** run `mdmp_analyze.py` on the dump, copy the 16-byte `bytes:` window and
  the AV kind into a new `crashes[]` entry (`match.bytes`, `match.av`), and fill `name`,
  `observed`, `when`, optional `trigger`, `summary`, `workaround`, `first_seen`/`last_seen`
  (the dump's date, `YYYY-MM-DD`), and a `builds` map (`"0x<TDS>": "+0x<offset>"`). Then
  regenerate the table.
- **Seen again:** bump `last_seen` to the new dump's date (and add the build to `builds`
  if it's a build not already listed), then regenerate.
- **Naming a build:** build headings auto-show the exe **compile date** (decoded from the
  `TimeDateStamp` - no lookup). The marketing version (e.g. `beta21d`) can't be derived
  reliably from the binary; if you learn it (a reporter says which beta they ran), record it
  in the optional top-level `build_versions` map (`"0x<TDS>": "beta21d"`) and it renders in
  the heading.

Override the registry path the analyzer reads with `--known <file.json>` or `$MDMP_KNOWN`.
