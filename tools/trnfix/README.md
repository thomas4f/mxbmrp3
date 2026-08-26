# mxbmrp3_trnfix

Finds MX Bikes trainer (`.trn`) files that crash or hang the game on track load,
and repairs them. It is a **single HTML page**, `web/index.html`: no build step,
no dependency, and no network call of any kind. Open it locally or host it
anywhere static.

There used to be an `.exe` alongside it. It is gone: the page does the same job
without asking a player to download and run a binary from a stranger, and two
implementations of the same byte logic only stay honest while someone maintains
the tests that compare them. `tests/web/tests/trnfix_web.spec.js` drives the page
in a real browser and is now its only gate. The git history has the exe if the
byte-level instruments (the graft bisection, the leaked-pointer scan) are
ever wanted back; the last revision with them is the parent of the commit that
deleted them.

## Using it

Drop your trainers folder onto the page, or pick files, or a `.zip` of the
folder. Damaged trainers can be taken one at a time or all at once as a zip;
only the files it would actually change are written, so nobody is handed back a
trainer that was already fine. Everything runs in the browser: no upload, no
server.

## What is wrong (established)

The game writes part of its trainer record without clearing the bytes it does
not use, so leftover memory is serialised into the file and trusted on the next
load. It then passes one of those fields to an `sprintf`-family call as a `%s`:
`msvcr90` `strnlen` measures it and `write_string` copies it into a stack
buffer. A dead pointer faults during the measure (access violation); a
readable-but-long one overruns the buffer and trips the `/GS` cookie
(`0xC0000409`). Only `mxbikes.exe` ever opens the file - no plugin does.

Full analysis, dumps and captures: `crash_analysis/`.

## Three repairs were tried and failed

In order, each tested in game:

1. Zeroing a flag at `0x4a` - failed.
2. Zeroing a leaked pointer at `0x52` - failed.
3. Copying the whole `0x2b..0x5a` header block from a trainer that loads
   reliably - **also failed**.

The third is the informative one. If grafting that entire range from a good file
does not stop the crash, then whatever the game chokes on **is not in that range
at all**, and the first two were patching bytes that merely correlated with the
save-time plugin set.

## What a proper diff of the matched pair shows

Same track, same bike, same session, one loading and one crashing:

- **~50 differing regions spread across `0x00..0x2c8`** - not one field.
- The record is fixed-size to about `0x2c8`, then identical padding to `0x400`,
  then lap data which differs entirely because the laps differ.
- **Both files carry leaked pointers**: 11 in the crashing one, **4 in the one
  that loads fine**. So "carries a leaked pointer" does not mean "will crash" -
  and the earlier detector only made the good file look clean because it scanned
  47 bytes of a ~700-byte record.
- No field in the prefix predicts the file size, so the lap section is not
  described by a count that could be checked for consistency.

All the string slots are properly NUL-terminated in both, which rules out the
other obvious candidate for an `sprintf` `%s` running away.

## Confirmed: the bad bytes are in the metadata record

Tested in game:

| tried | result |
|---|---|
| zero every leaked pointer in the file | **still crashes** |
| graft `0x0-0x2c8` from a trainer that loads | **loads** |

Two things follow. The leaked addresses are **not** the cause - they are a
symptom of the same uninitialised write, and zeroing them changes nothing. And
whatever the game chokes on lives inside the fixed-size metadata record, since
the graft leaves the entire lap section untouched.

Combined with every string slot being properly NUL-terminated in both files, the
likely culprit is a **numeric** field - an index, count or offset the game adds
to a base to produce the pointer it then formats with `%s`. That also explains
why zeroing pointers in the file was never going to help.

Within `0x00..0x2c8` the matched pair differs in **48 regions totalling 245
bytes**, which is what the bisection below narrowed.

## The bisection, and where it landed

`graft` copies an arbitrary byte range out of a trainer known to load, so the
bad bytes can be hunted by halving. **The oracle is asymmetric, and that is what
made it feasible.** A crash is decisive: the poison lies outside the range you
copied. Surviving is only probabilistic, because the fault is a coin flip on
every launch - so bisect toward crashes and treat ~5 clean loads as "probably
inside" rather than proof.

Run in game against the matched pair, prefix grafts from the good file into the
bad one:

| graft | result |
|---|---|
| `0x0-0x2c8` | loads |
| `0x0-0x19a` | loads |
| `0x0-0x112` | loads |
| `0x0-0xb3`  | loads |
| `0x0-0x9b`  | loads |
| `0x0-0x86`  | loads |
| `0x0-0x7f`  | **crashes** |
| `0x0-0x4a`  | **crashes** |

`0x0-0x86` loading and `0x0-0x7f` crashing brackets the fault to **`0x7f..0x85`,
seven bytes**. (`0x0-0x9b` is a superset of `0x0-0x86`, so it loading adds
confirmation but no narrowing.)

## What those seven bytes are

The metadata record opens with the bike name in a fixed-width buffer at `0x62`.
The name is NUL-terminated inside it, and the slack from that terminator to
`0x92` is never written by the game:

```
        0x7e                                          0x92
 good   00 | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
 bad    00 | 7f 00 00 f0 f7 2f 01 00 00 00 00 30 fb ff ff e8 04 00 00
                ^^^^^^^^^^^^^^^^^^^^ 0x7f..0x85, the bracketed bytes
```

That slack is zero in **all six** trainers here that load, and dirty in **all
five** that have crashed. `0x82` in the bad file holds `0x012ff7f0` - a 32-bit
stack address, exactly the shape of a value that becomes a wild `%s` argument.

This also explains the "coin flip": the leftovers have to be *unlucky* - a
readable-but-unterminated address - for the format call to run away. Most saves
leave harmless junk there, which is why plenty of dirty files load fine.

## The repair

Zero `[name terminator + 1, 0x92)`. No donor file needed, nothing else touched,
and a file whose slack is already clear comes back byte-identical.

**Confirmed in game** on the crashing half of the matched pair - the same file
the bisection was run against. That is one file and one reporter, so the ~5-load
protocol below still applies to anything new. Two things to keep in mind:

- The name offset `0x62` and the end `0x92` are read off one bike on one game
  build. The tool refuses the file rather than guessing when the bytes at `0x62`
  are not a NUL-terminated ASCII name - guessing where the name ends means
  zeroing somebody's lap data.
- Only `0x7f..0x85` is bracketed; zeroing to `0x92` also clears `0x8a..0x91`,
  which is unproven but matches every good file.
- Clearing only the slack fixing the file also means the garbage elsewhere in
  the record (`0x2b..0x5f`, and everything past `0x92`) is harmless. The
  `0x0-0x86` graft had replaced that too, so this was the open question; it is
  now answered.

## Evaluating a repair on a new file

There is no shortcut, because **the fault is a coin flip on every launch**.

1. Get a trainer that has actually crashed the game at least once.
2. Copy it somewhere safe. Repair it.
3. Load that track **~5 times**. Any crash or hang means it did not work.
4. Five clean loads is **weak evidence, not proof**. Repeat on another file.

A single clean load proves nothing. That is precisely how the first attempt
fooled itself, and how at least one player concluded there was "no reliable
reproduction" when there was.

## Testing it

```
./tests/web/run.sh tests/trnfix_web.spec.js
```

The spec pins the exact reach of the repair (only `[name terminator + 1, 0x92)`,
name intact, nothing outside), that an unrecognised layout is declined rather
than half-repaired, that an already-clear file comes back byte-identical, that
the zip carries trainers and nothing else, and that the page makes zero network
requests. It also asserts the page raises no runtime errors during a full pass:
the JS is inline in the `.html`, so `tests/web/lint.sh` never sees it, and a
stale variable reference once stopped the table drawing with every other test
still green.
