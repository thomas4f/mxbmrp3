# Spotter voice packs

A pack is a folder holding a `spotter.ini` that names every callout, and
optionally the audio to play for them. The plugin ships one pack, **default**,
which is text only: its lines are spoken by Windows text-to-speech and shown as
subtitles. Read `default/spotter.ini` - it documents itself and is meant to be
copied and edited.

Voices with recorded audio are **not bundled**: they are tens of megabytes of
wav that most installs never need, so they are published separately, and the
plugin works without them. Install one by extracting its folder into your own
`Documents\PiBoSo\<Game>\mxbmrp3\spotters\` (which survives plugin updates)
and picking it in Settings > Spotter.

How to reword the callouts, mute them, add alternates, or record your own
voice, with every callout and variable you can use:
https://github.com/thomas4f/mxbmrp3/blob/main/docs/spotter.md. Baking recorded
packs needs the source tree: `tools/spottergen/` documents the audio contract.
