# Spotter voice packs

A pack is a folder holding a `<name>.ini` that names every callout, and
optionally the audio to play for them. The plugin ships one pack, **default**,
which is text only: its lines are spoken by Windows text-to-speech and shown as
subtitles. Read `default/default.ini` - it documents itself and is meant to be
copied and edited.

## Recorded voices are a separate download

Voices with recorded audio are **not bundled**. They are tens of megabytes of
wav for a feature that is off until you turn it on, so they are published
separately; the plugin works without them. Install one by extracting its folder
here (or, better, into your own `Documents\PiBoSo\<Game>\mxbmrp3\spotters\`,
which survives plugin updates) and picking it in Settings > Spotter.

## Rolling your own

Two kinds are possible and they use the same keys:

- **Text only**, like `default` - reword the callouts, mute the ones you do not
  want, add alternates so a line does not repeat every lap. No audio tools
  needed, and it works the moment you press Reload Config.
- **Recorded**, adding `_wav` and `_mix` rows pointing at your clips. `_mix`
  stitches a line from pieces at playback time, which is how a pack says a
  rider number or a lap time it never recorded.

The full guide, including every callout and variable you can use, is at
https://github.com/thomas4f/mxbmrp3/blob/main/docs/spotter.md. Baking recorded
packs needs the source tree: `tools/spottergen/` documents the audio contract
(12 kHz mono PCM, the number-chunk vocabulary, the wording the plugin expects)
and `mxbmrp3/core/spotter_cue_pack.h` is the format specification.
