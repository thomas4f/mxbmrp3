# Spotter voice

A spotter in your ear while you ride. The first half of this page is what it does and how to set it up; [Writing a voice pack](#writing-a-voice-pack) is how to reword it or give it your own voice. [`spotter-reference.md`](spotter-reference.md) is the lookup table - every callout with when it fires and which switch mutes it, generated from the plugin itself so it can't fall behind.

Turn on **Spoken audio** (Settings > Spotter, or the checkbox beside the tab).

## What it calls

- **Around you** - a rider behind you and when you're clear again, someone alongside on your left or right, and a separate call when you're boxed in on both sides at once. A backmarker ahead, and blue flags when you're the one being lapped.
- **Hazards** - a rider down, or one coming the wrong way.
- **Each time you cross the line** - your position, then the gap to the rider ahead and the rider behind, and whether each is closing or stretching.
- **The session** - the start, the milestones on the way through (ten minutes, five, halfway), the last lap, and who has finished.

The gap to the rider *behind* deliberately waits until they reach a split or the line you've already crossed. That costs a few seconds of freshness and buys a number that actually happened, rather than one extrapolated from where they were.

## Voices

Out of the box the callouts are read by Windows text-to-speech, using whichever installed voice you cycle to under **TTS voice**. That list covers both places Windows keeps voices - the classic "Desktop" ones and the newer set, which is also where any voice you add under Windows Settings > Time & Language > Speech ends up. So a voice you installed and never saw offered anywhere should appear here.

**Speed** changes the pace of the voice without changing its pitch.

Voice packs with *recorded* audio are a separate download - tens of megabytes that most installs never need. Once extracted into `spotters\` under your [Documents plugin folder](../README.md#modding) they appear under **Voice pack**.

Cycling either one plays a sample line, so you pick by ear rather than by name. It's the same line every time, on purpose - it's a comparison - and it's built to expose what actually differs between voices: a rider number and a lap time, both stitched together from that voice's number clips, which is where packs sound most unlike each other.

## What gets announced

Five **Callouts** switches - General, Timing, Opponents, Proximity and Hazards - decide what you hear. Mute the ones you don't want. The two worth telling apart are **Proximity**, the spotting half (who is beside or behind *you*), and **Opponents**, the news half (the rest of the field's laps, penalties and pit stops). You can keep one without the other - plenty of people want to be spotted and don't want a running commentary.

**Subtitles** shows every callout as on-screen text on its own draggable widget, and works with the audio off. Run the spotter silent and captioned if you'd rather read it, or leave both on so you can check what you just heard.

Callouts follow whoever you're watching, so the spotter works while spectating and in replays as well as while riding.

## Tuning

The Spotter tab carries the distances that decide when a proximity call fires, including how far behind counts as behind. If a call still comes too early or too often for your taste, the rest - the repeat timings and cooldowns behind the proximity and hazard calls - are in the `[Spotter]` section of the [INI file](../README.md#advanced-settings).

## Changing the words

The wording is a file you can edit. `mxbmrp3_data\spotters\default\spotter.ini` holds every line the spotter says - and it *is* the wording, not an override on something built in, so commenting a row out silences that callout outright. Copy the folder into your [Documents plugin folder](../README.md#modding) and your version survives updates.

The full authoring guide, including recording your own voice, is below.

## Writing a voice pack

A spotter voice is a **pack**: a folder holding a `spotter.ini` that names every callout, and optionally the audio to play for them. Add an optional `[pack]` section with `name = Your Voice` to give it a title in the picker; without one it shows as its folder name. That section is spelled the same in every pack type, so it is the one line you already know from writing a theme or a pit board. Two kinds are possible and they use the same keys.

**The shipped `default` pack is the spotter's vocabulary**, not a set of overrides on something built into the plugin - there is nothing behind it. Comment a row out of it and that callout stops happening. Any other pack you pick **layers over** it: what your pack defines wins, what it leaves out is answered from `default`, so a recorded voice covering twenty callouts still speaks the rest in the shipped words. `key =` with an empty value is how a pack mutes a callout outright instead of inheriting it.

**Text only.** This is what ships - `spotters\default\spotter.ini`, every line the spotter says, read by Windows text-to-speech. Rewording a callout, muting one (`key =` with nothing after it), or adding alternates needs no audio tools at all. Copy the folder into `spotters\` under your [Documents plugin folder](../README.md#modding), rename the folder, edit, and pick it in Settings > Spotter. The ini is called `spotter.ini` in every pack, so the folder name is the only thing to change. It's grouped under the same five headings as the Callouts switches there - **General**, **Timing**, **Opponents**, **Proximity**, **Hazards** - so silencing a whole group is a switch, not an edit; editing is for the finer cut.

**Recorded.** Packs with real audio are a separate download rather than part of the installer - tens of megabytes that most installs never need. They add `_wav` and `_mix` rows pointing at their clips:

```
spotters\
  default\      default.ini
  am_michael\   am_michael.ini   blue_flag.wav  clear.wav  rider_behind.wav  num_0.wav ...
```

The `.ini` is the whole format. One rule before the example: **inside `[Cues]`, a
`;` is part of the value, not a comment** - phrases legitimately contain
anything, so comments there are whole lines only (a line *starting* with `;`).
An inline `; comment` on a cue row would be spoken aloud, and on a `_wav` row it
becomes part of the filename.

```ini
[Cues]
; rider_behind: the words (subtitle, and what text-to-speech reads).
rider_behind = Rider behind.
; rider_behind_wav: the clip to play instead.
rider_behind_wav = rider_behind.wav
; rider_behind_2: an alternate, picked at random when the cue fires.
rider_behind_2 = On your tail.
; penalty_other_mix: stitched from clips as it's spoken.
penalty_other_mix = seg_penalty.wav {event_rider} {penalty_seconds}
; An EMPTY value mutes the cue: never say this one.
pit_entry_other =

[Mix]
gap_ms = -40    ; how tightly stitched clips are joined ([Mix] does strip these)
```

**Callouts are moments; variables are numbers.** A callout is something that *happened* - you crossed the line, a sector ended, someone crashed. A variable is a value that simply *is*, read fresh whenever a callout fires, so **every `{variable}` works in every line**: you can ask any callout for your position, the gap ahead, the laps remaining. Numbers arrive spoken, so `{gap_to_ahead}` is "one point two", not "1.2".

```ini
lap_completed = P {position}[, {gap_to_ahead} to {rider_ahead}][, {trend_ahead}].
```

A variable with no value *right now* - the gap ahead when you're leading, a last lap time on lap one - expands to nothing. `[square brackets]` mark an **optional group**, dropped whole when any variable inside it is empty, which is what keeps that line reading as a sentence from P1 as well as from P8. A name that isn't a variable is left on screen exactly as typed - that's how you spot a misspelling.

**When you need a group.** Empty punctuation is cleaned up for you, so `Penalty, penalty, {penalty_seconds}.` reads fine with no amount. An orphaned *word* isn't - the `to` above, or the `P` in `P {position}` - so anything where a word only makes sense with its value needs the brackets. That's why the shipped pack uses `P {position}` bare on the lap callouts and brackets it elsewhere: those callouts don't fire at all for an unclassified rider, so it can't be empty there. If you're writing a line for a callout you haven't checked, bracket it.

**Alternates keep a line from wearing out.** `<key>_2` through `_9` are extra rows for the same callout and one is picked at random each time it fires, so the twenty-times-a-race lines stop sounding like a recording. The shipped pack is the worked example - `lap_completed` carries several rows, and they deliberately report *different things* about the same moment: the rider ahead, your own lap time and how it compares, the gap behind, the clock. Each row is a whole template, so that is where the variables get interesting.

Three things to know before you add your own. **Number them without gaps from `_2`** - the scan stops at the first missing number, so a `_4` written without a `_3` sits in the file looking like a line and is never spoken (the build checks this for the shipped pack). **Keep the urgent ones short** - `rider_left` stays two words in every row, because you need it before the corner. And **not every callout wants variety**: `hotkey_triggered` has no alternates on purpose, since you press that key to get the same status report every time. One more, if you're recording: a pack that rewords a callout **replaces its alternates too**, so it can never roll a line it has no clip for.

**Read your lines back with the values missing.** [`spotter-pack-render.md`](spotter-pack-render.md) is the whole shipped pack rendered both ways - every value filled, and every value absent - and it's generated from the pack itself, so it's also the worked answer to "what does this actually say". The column that catches things is the empty one: a row whose words sit outside its brackets collapses to a fragment there (`{rider_behind} is {gap_to_behind} back.` reads "is back." when you're leading), which is the one mistake the format can't clean up for you. Capitalisation it does handle - the line is capitalised after expansion, so a row may open with a `{variable}`.

Some keys are refinements of a more general one and **fall back to it** - `sector_completed_faster` to `sector_completed`, `practice_started` to `session_started` - so one line covers the general case, and you define the specific keys only when you want different *wording* per case, or when you're recording a voice that can't say "quicker" out of a template. Audio falls back down its own ladder: a `_mix` recipe if there is one, then `_wav`, then text-to-speech. Audio does **not** layer across packs the way words do - a clip is only findable in the folder it shipped in - so `_wav` and `_mix` come from your pack alone. `{event_rider}`, `{penalty_seconds}` and lap times are assembled at runtime from the pack's number clips (`num_0.wav` upward), which is why a pack can say a rider number it never recorded.

`[Mix] gap_ms` tunes how those stitched pieces meet, and it's the knob to reach for if a number sounds like two words rather than one. Positive is silence between clips (60 ms if the key is absent), `0` butt-joins them, and a **negative** value overlaps them with a crossfade. A pack carrying only `num_0..99` says every three-digit rider number as a join, so `965` is "nine" + "sixty five" and `-40` is the difference between hearing a number and hearing two. The value is in your voice's own timing, so the Speed setting scales it.

Bind **Spotter Cue** (Settings > Hotkeys) and it speaks your `hotkey_triggered` line on demand - the fastest way to hear a line you're writing without waiting for the race to produce the event it belongs to. To record your own, start from a downloaded pack's folder, rename the folder, and replace the clips: **16-bit mono PCM WAV**, which the reader requires and will otherwise skip the clip for. The sample RATE is yours to choose - the published voices use 12 kHz - but keep one rate across the pack, since stitching a number from clips of mixed rates produces nothing at all. Your voice appears under **Voice pack** (Settings > Spotter) and the choice is stored **by name**, so adding or removing other packs never reassigns it. The **Reload Config** hotkey re-copies your folder from Documents and re-reads it without restarting - including changed audio, which themes and pit boards can't do - so rewording a line is edit, press, listen.
