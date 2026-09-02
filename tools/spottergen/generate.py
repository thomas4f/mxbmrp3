#!/usr/bin/env python3
# ============================================================================
# tools/spottergen/generate.py
# Bakes complete spotter voice packs - the folders users drop into
# mxbmrp3_data/spotters/ and pick in Settings -> Spotter -> Voice pack. One
# pack per Kokoro voice, each fully self-contained:
#
#   - every FIXED cue as a whole radio-FX wav (squelch clicks + static bed),
#     including the _2/_3 variants the plugin rotates at random;
#   - the stitchable chunk vocabulary (num_0..num_999 racing-style, rider,
#     point) plus phrase segments, radio-toned but WITHOUT the noise bed or
#     clicks (a per-chunk bed drops out in the stitch gaps; clicks mark whole
#     transmissions, not words);
#   - the spotter.ini wiring every cue: phrase (subtitle + TTS fallback),
#     _wav for fixed cues, _mix recipes for dynamic ones.
#
# The wavs are GENERATED artifacts and deliberately NOT committed - edit the
# tables below (or add a voice), rerun, and ship the zip; never hand-edit a
# clip, it silently stops matching its siblings' voice/level/pacing. Output
# is 12kHz PCM16 mono: the radio bandpass keeps nothing above ~4.3kHz, so
# 24kHz sampling would be double the bytes for identical sound. The tanh
# drive DOES add harmonics above 6kHz, hence the second lowpass before the
# decimation - remove it and the downsample aliases audibly.
#
# Why bespoke (CLAUDE.md rule): synthesis is off the shelf (kokoro-onnx,
# Apache-2.0, ~2x realtime on CPU). This script holds what no standard tool
# knows: the cue tables, the racing-style number wording that MUST match
# SpotterPhrase::numberWords verbatim (a pack whose num_476.wav says
# something else desyncs wav playback from TTS and subtitles), the frozen
# chunk-name convention shared with spotter_mix.h, and the radio chain.
#
# Setup (one-time, ~340MB):
#   pip install kokoro-onnx soundfile numpy
#   curl -LO https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/kokoro-v1.0.onnx
#   curl -LO https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/voices-v1.0.bin
#
# Usage:
#   python3 generate.py [--voices am_michael,bm_george,af_heart]
#       [--model kokoro-v1.0.onnx] [--voices-bin voices-v1.0.bin]
#       [--out packs] [--speed 1.15]
#
# Re-runs are incremental: existing wavs are kept (delete a file or the pack
# folder to force a re-bake), the ini and zip are always rewritten. A full
# bake is ~1030 clips per voice, ~10 min each on a desktop CPU.
# ============================================================================
import argparse
import os
import re
import sys
import zipfile

# numpy/soundfile are needed only to BAKE. Deferred (like kokoro_onnx below)
# so --selftest and --help run without them - the spottergen-selftest gate
# lists python3 as its only tool, and a top-level import would turn a missing
# pip module into a gate FAILURE on unrelated changes.
np = None
sf = None


def _import_audio_deps():
    global np, sf
    import numpy
    import soundfile
    np = numpy
    sf = soundfile

# ---------------------------------------------------------------------------
# Cue tables - the editable part. Keys are the plugin's frozen cue-key API
# (see core/spotter_cue_pack.h); <key>_2.. are random-rotation variants.
# ---------------------------------------------------------------------------
WAV_CUES = {
    "session_started": "Green green green.",
    "practice_started": "Practice underway.",
    "quali_started": "Qualifying is live.",
    "warmup_started": "Warm up underway.",
    "session_prestart": "Session starting, get ready.",
    "session_ended": "Session complete.",
    "session_state": "Session update.",
    "overtime_started": "Overtime, finish the lap count.",
    "session_time_expired": "Time's up.",
    "fastest_lap_you": "Fastest lap, nice work.",
    "final_lap": "Final lap.",
    "leader_you": "You're the leader now.",
    "finished_you": "Checkered flag, good race.",
    "finished_leader": "Leader's taken the flag.",
    "ten_minutes_remaining": "Ten minutes to go.",
    "five_minutes_remaining": "Five minutes left.",
    "halfway_point": "Halfway there.",
    "personal_best": "New personal best.",
    "lapping_traffic": "Backmarker ahead.",
    "disqualified_you": "Disqualified.",
    "penalty_you": "Penalty, penalty.",
    "penalty_clear_you": "Penalty cleared, you're good.",
    "penalty_clear_other": "Penalty cleared.",
    "penalty_change": "Penalty changed.",
    "pit_entry_you": "Entering pit lane.",
    "pit_exit_you": "Pit exit, up to speed.",
    "rider_behind": "Rider behind.",
    "rider_left": "Rider left.",
    "rider_right": "Rider right.",
    "rider_behind_clear": "Clear.",
    "blue_flag": "Blue flag, faster rider closing.",
    "hazard_ahead": "Caution, rider down ahead.",
    "wrong_way_ahead": "Heads up, wrong way rider ahead.",
    # Variants: rotate randomly with the base at playback.
    "session_started_2": "Gate drop, race is live.",
    "rider_behind_2": "On your tail.",
    "rider_behind_3": "Pressure from behind.",
    "rider_left_2": "On your left.",
    "rider_right_2": "On your right.",
    "rider_behind_clear_2": "You're clear.",
    "rider_behind_clear_3": "Gap behind, breathe.",
    "blue_flag_2": "Blue flag, let him through.",
    "final_lap_2": "White flag, one to go.",
    "finished_you_2": "That's the checkered, well done.",
    "personal_best_2": "That's a new PB.",
    "lapping_traffic_2": "Traffic ahead.",
}

# Dynamic cues: phrase template (subtitle + TTS fallback) and the mix recipe
# the in-plugin mixer stitches. Tuples name a segment wav + its spoken text.
DYNAMIC_CUES = {
    "fastest_lap_other": ("Fastest lap, {event_rider}, {event_time}.",
                          [("seg_fastlap", "Fastest lap,"), "{event_rider}",
                           "{event_time}"]),
    "fastest_lap_you": ("Fastest lap, nice work, {event_time}.",
                        [("seg_fastlap_you", "Fastest lap, nice work,"),
                         "{event_time}"]),
    "penalty_other": ("Penalty for {event_rider}, {penalty_seconds}.",
                      [("seg_penalty", "Penalty for,"), "{event_rider}",
                       "{penalty_seconds}"]),
    "leader_other": ("New leader, {event_rider}.",
                     [("seg_leader", "New leader,"), "{event_rider}"]),
    "finished_other": ("{event_rider} has finished.",
                       ["{event_rider}", ("seg_finished", "has finished.")]),
    "retired_other": ("{event_rider} is out.", ["{event_rider}", ("seg_out", "is out.")]),
    "disqualified_other": ("{event_rider} disqualified.",
                  ["{event_rider}", ("seg_dsq", "disqualified.")]),
    "pit_entry_other": ("{event_rider} is pitting.",
                     ["{event_rider}", ("seg_pitin", "is pitting.")]),
    "pit_exit_other": ("{event_rider} leaving pit.",
                      ["{event_rider}", ("seg_pitout", "leaving pit.")]),
    "lap_completed": ("P {position}.", [("seg_p", "P,"), "{position}"]),
    # The BEHIND gap is the only pace report that is still a cue of its own -
    # the ahead one became {gap_to_ahead} on the crossing cues, since it fires
    # at the same instant as those and marks no moment they do not.
    #
    # TWO NAMESPACES, and they are not the same one. The PHRASE expands
    # SpotterVars, where the gap is {gap_to_behind}. The MIX recipe is resolved
    # by SpotterMix::resolveTokens, which has never known that name - it takes
    # {event_time} for "the composed number and tenths this cue carries", and
    # emitGapCue passes the gap's seconds/tenths in exactly those arguments.
    #
    # Naming the variable in the recipe made every gap cue silent on a recorded
    # pack: the token fell through as a wav FILENAME, the file did not exist,
    # the whole recipe was dropped and the cue fell back to TTS - which under
    # Wine is nothing at all. parse() now rejects an unknown placeholder at
    # load and says so, so this cannot be silent again.
    "gap_behind": ("Behind, {gap_to_behind}.",
                   [("seg_behind", "Behind,"), "{event_time}"]),
    "gap_behind_closing": ("Behind, {gap_to_behind}, closing.",
                           [("seg_behind", "Behind,"), "{event_time}",
                            ("seg_closing", "closing.")]),
    "gap_behind_dropping": ("Behind, {gap_to_behind}, dropping back.",
                            [("seg_behind", "Behind,"), "{event_time}",
                             ("seg_dropping", "dropping back.")]),
}

# Cues whose ini rows ship COMMENTED OUT (and whose built-in phrase is empty
# in spotter_phrase.h): other riders' pit traffic is the noisiest event
# stream on a full grid. The segment wavs are still baked, so a pack user
# re-enables one by just uncommenting its rows.
QUIET_DEFAULT = {"pit_entry_other", "pit_exit_other"}

# WAV_CUES entries that ALSO stitch an optional {penalty_seconds} suffix (penalty
# seconds) at playback: the ini phrase gains the placeholder and a _mix row
# joins the _wav fallback. The mix leads with a chunk-FX segment (not the
# radio-FX full cue) so its seams match the number chunks; with no seconds
# value the {penalty_seconds} token is simply omitted (spotter_mix.h), so the segment
# doubles as the standalone call.
ADDON_MIXES = {
    "penalty_you": ("Penalty, penalty, {penalty_seconds}.",
                    [("seg_penalty_you", "Penalty, penalty."), "{penalty_seconds}"]),
    "overtime_started": ("Overtime, {overtime_laps}.",
                 [("seg_overtime", "Overtime,"), "{overtime_laps}"]),
}

# ---------------------------------------------------------------------------
# Racing-style number words - MUST match SpotterPhrase::numberWords verbatim.
# ---------------------------------------------------------------------------
ONES = ["zero", "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
        "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = ["", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
        "eighty", "ninety"]


def two_digit(n):
    if n < 20:
        return ONES[n]
    return TENS[n // 10] + ((" " + ONES[n % 10]) if n % 10 else "")


def num_words(n):
    if n < 100:
        return two_digit(n)
    h, r = n // 100, n % 100
    if r == 0:
        return ONES[h] + " hundred"
    if r < 10:
        return f"{ONES[h]} oh {ONES[r]}"
    return f"{ONES[h]} {two_digit(r)}"


# ---------------------------------------------------------------------------
# Audio chain
# ---------------------------------------------------------------------------
def trim(x, sr, pad_ms=8.0, thresh=0.02):
    x = np.asarray(x, dtype=np.float32)
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return x
    above = np.flatnonzero(np.abs(x) >= thresh * peak)
    pad = int(pad_ms / 1000.0 * sr)
    return x[max(0, int(above[0]) - pad):
             min(x.size, int(above[-1]) + pad)]


def rms_match(x, target_rms=0.12, peak_ceiling=0.95):
    rms = float(np.sqrt(np.mean(x ** 2)))
    if rms <= 0.0:
        return x
    x = x * (target_rms / rms)
    peak = float(np.max(np.abs(x)))
    return x * (peak_ceiling / peak) if peak > peak_ceiling else x


def bandpass(x, sr, lo=280.0, hi=3400.0, edge=0.25):
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(x.size, 1.0 / sr)
    gain = np.zeros_like(freqs)
    lo0, lo1 = lo * (1 - edge), lo
    hi0, hi1 = hi, hi * (1 + edge)
    rise = (freqs >= lo0) & (freqs < lo1)
    gain[rise] = 0.5 - 0.5 * np.cos(np.pi * (freqs[rise] - lo0) / (lo1 - lo0))
    gain[(freqs >= lo1) & (freqs <= hi0)] = 1.0
    fall = (freqs > hi0) & (freqs <= hi1)
    gain[fall] = 0.5 + 0.5 * np.cos(np.pi * (freqs[fall] - hi0) / (hi1 - hi0))
    return np.fft.irfft(spec * gain, n=x.size).astype(np.float32)


def noise_bed(n, sr, rng, level):
    return bandpass(rng.normal(0.0, 1.0, n).astype(np.float32), sr) * level


def squelch_click(sr, rng, ms=45.0, level=0.5):
    n = int(ms / 1000.0 * sr)
    burst = bandpass(rng.normal(0.0, 1.0, n).astype(np.float32), sr,
                     lo=600, hi=3800)
    env = np.exp(-np.linspace(0.0, 9.0, n, dtype=np.float32))
    burst *= env * level / max(1e-9, float(np.max(np.abs(burst))))
    return burst


def radio_fx(x, sr, drive=3.2, noise_level=0.016, seed=1):
    """Whole-transmission treatment: bandpass, drive, static bed, clicks."""
    rng = np.random.default_rng(seed)
    x = np.asarray(x, dtype=np.float32)
    x = x / max(1e-9, float(np.max(np.abs(x))))
    x = bandpass(x, sr)
    x = np.tanh(drive * x) / np.tanh(drive)
    x = x + noise_bed(x.size, sr, rng, noise_level)
    lead = int(0.10 * sr)
    pad = noise_bed(lead, sr, rng, noise_level)
    out = np.concatenate([squelch_click(sr, rng), pad, x,
                          pad, squelch_click(sr, rng, ms=35, level=0.4)])
    return out * (0.89 / max(1e-9, float(np.max(np.abs(out)))))


def chunk_fx(x, sr, drive=3.2):
    """Stitchable-chunk treatment: bandpass + drive only (see header)."""
    x = np.asarray(x, dtype=np.float32)
    x = x / max(1e-9, float(np.max(np.abs(x))))
    x = bandpass(x, sr)
    x = np.tanh(drive * x) / np.tanh(drive)
    return (x * 0.89).astype(np.float32)


def write_12k(path, x, sr):
    """Lowpass (the drive's harmonics exceed the new Nyquist) + decimate."""
    assert sr == 24000, "generator assumes Kokoro's 24kHz output"
    x = bandpass(x, sr)
    sf.write(path, x[::2], sr // 2, subtype="PCM_16")


# ---------------------------------------------------------------------------
def build_pack(kokoro, voice, out_root, speed, num_scheme="full"):
    pack_dir = os.path.join(out_root, voice)
    os.makedirs(pack_dir, exist_ok=True)

    def render(text):
        samples, sr = kokoro.create(text, voice=voice, speed=speed,
                                    lang="en-us")
        return rms_match(trim(samples, sr)), sr

    def exists(name):
        return os.path.exists(os.path.join(pack_dir, name))

    lines = [
        f"; mxbmrp3 spotter voice pack: {voice}",
        f"; Generated by tools/spottergen (Kokoro-82M, speed {speed}).",
        f"; Install: extract to <game>\\plugins\\mxbmrp3_data\\spotters\\{voice}\\",
        f"; Select:  Settings -> Spotter -> Voice pack -> {voice}",
        "[Cues]",
    ]
    seg_texts = {}

    def collect_tokens(parts):
        tokens = []
        for part in parts:
            if isinstance(part, tuple):
                seg_texts[part[0]] = part[1]
                tokens.append(f"{part[0]}.wav")
            else:
                tokens.append(part)
        return tokens

    for key, text in WAV_CUES.items():
        if key in ADDON_MIXES:
            template, parts = ADDON_MIXES[key]
            lines.append(f"{key} = {template}")
            lines.append(f"{key}_wav = {key}.wav")
            lines.append(f"{key}_mix = {' '.join(collect_tokens(parts))}")
        else:
            lines.append(f"{key} = {text}")
            lines.append(f"{key}_wav = {key}.wav")
        if exists(f"{key}.wav"):
            continue
        samples, sr = render(text)
        seed = abs(hash((voice, key))) % (2 ** 31)
        write_12k(os.path.join(pack_dir, f"{key}.wav"),
                  radio_fx(samples, sr, seed=seed), sr)

    lines.append("; --- dynamic cues (stitched; template doubles as subtitle"
                 " + TTS fallback) ---")
    for key, (template, parts) in DYNAMIC_CUES.items():
        tokens = collect_tokens(parts)
        if key in QUIET_DEFAULT:
            lines.append("; off by default (noisy on a full grid) -"
                         " uncomment to enable:")
            lines.append(f";{key} = {template}")
            lines.append(f";{key}_mix = {' '.join(tokens)}")
        else:
            lines.append(f"{key} = {template}")
            lines.append(f"{key}_mix = {' '.join(tokens)}")

    for name, text in seg_texts.items():
        if exists(f"{name}.wav"):
            continue
        samples, sr = render(text)
        write_12k(os.path.join(pack_dir, f"{name}.wav"),
                  chunk_fx(samples, sr), sr)

    for word, fname in [("rider,", "rider.wav"), ("point,", "point.wav"),
                        ("hundred,", "hundred.wav"), ("oh,", "oh.wav"),
                        ("seconds.", "seconds.wav"),
                        ("second.", "second.wav"),
                        ("laps,", "laps.wav"),
                        ("lap,", "lap.wav")]:
        if exists(fname):
            continue
        samples, sr = render(word)
        write_12k(os.path.join(pack_dir, fname), chunk_fx(samples, sr), sr)
    # split scheme: num_0..99 + hundred/oh only; the plugin stitches
    # three-digit numbers from the hundreds split (spotter_mix.h).
    top = 100 if num_scheme == "split" else 1000
    for n in range(top):
        if exists(f"num_{n}.wav"):
            continue
        samples, sr = render(num_words(n) + ",")
        write_12k(os.path.join(pack_dir, f"num_{n}.wav"),
                  chunk_fx(samples, sr), sr)
        if n % 100 == 0:
            print(f"    {voice}: num chunks {n}/{top}")

    # spotter.ini, not "<voice>.ini": a generated pack must land in the shape the
    # plugin ships and the docs describe, or "copy a pack and rename the
    # folder" -- the whole point of the fixed name -- silently does not hold for
    # the packs this tool produces. (The legacy name is still READ, so an already
    # generated pack keeps working.)
    with open(os.path.join(pack_dir, "spotter.ini"), "w",
              encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    zip_path = os.path.join(out_root, f"spotter_pack_{voice}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(os.listdir(pack_dir)):
            z.write(os.path.join(pack_dir, name), f"{voice}/{name}")
    print(f"  {voice}: {len(os.listdir(pack_dir))} files -> {zip_path} "
          f"({os.path.getsize(zip_path) / 1e6:.1f} MB)")


def selftest():
    """Wording parity with SpotterPhrase::numberWords, via the fixture both
    sides assert. A deliberate wording change regenerates the fixture AND
    every shipped pack (the baked num_*.wav say the old words)."""
    here = os.path.dirname(os.path.abspath(__file__))
    fixture = os.path.join(here, "..", "..", "tests", "fixtures",
                           "spotter_number_words.txt")
    bad = checked = 0
    with open(fixture, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            n_str, _, words = line.partition("\t")
            checked += 1
            if num_words(int(n_str)) != words:
                print(f"MISMATCH at {n_str}: fixture '{words}' != "
                      f"generator '{num_words(int(n_str))}'")
                bad += 1
    if bad or checked != 1000:
        print(f"SELFTEST FAIL: {bad} mismatches, {checked}/1000 checked")
        return 1
    print("spottergen selftest: 1000/1000 number wordings match the fixture")

    # ...and that every key baked here is a key the plugin still emits.
    #
    # These tables are a THIRD list of cue keys, after the registry and the
    # shipped ini, and nothing else checks them. A pack baked from a stale key
    # carries rows the plugin logs as "will never be spoken", so that cue has no
    # audio at all - silently, because the census test reads the shipped ini,
    # which is correct.
    header = os.path.join(here, "..", "..", "mxbmrp3", "core",
                          "spotter_cue_pack.h")
    with open(header, encoding="utf-8") as f:
        # A registry row is `{ "key", "what", SpotterPhrase::Category::X }`;
        # the count guard below is what catches this scan when the row shape
        # changes.
        known = set(re.findall(r'\{\s*"([a-z_0-9]+)",\s*"',
                               f.read()))
    if len(known) < 20:
        print(f"SELFTEST FAIL: only {len(known)} cue keys parsed from "
              f"{header} - the registry's shape changed, fix this scan")
        return 1
    baked = list(WAV_CUES) + list(DYNAMIC_CUES) + list(ADDON_MIXES)
    stale = sorted(k for k in baked
                   if re.sub(r"_[2-9]$", "", k) not in known)
    if stale:
        print("SELFTEST FAIL: cue keys baked here that the plugin does not "
              "emit - a pack built from these plays nothing for them:")
        for k in stale:
            print(f"    {k}")
        return 1
    print(f"spottergen selftest: {len(baked)} cue keys all exist in the "
          f"plugin's registry")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--voices", default="am_michael,bm_george,af_heart")
    ap.add_argument("--model", default="kokoro-v1.0.onnx")
    ap.add_argument("--voices-bin", default="voices-v1.0.bin")
    ap.add_argument("--out", default="packs")
    ap.add_argument("--speed", type=float, default=1.15)
    ap.add_argument("--selftest", action="store_true",
                    help="verify num_words against the shared fixture "
                         "(tests/fixtures/spotter_number_words.txt) and exit; "
                         "needs no model. The C++ side of the same handshake "
                         "is test_spotter_phrase.cpp.")
    ap.add_argument("--num-scheme", choices=["full", "split"], default="split",
                    help="split (default) = num_0..99 + hundred/oh, 3-digit "
                         "numbers stitch at the hundreds boundary; full = "
                         "num_0..999 baked (zero seams, 10x the size)")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    _import_audio_deps()
    from kokoro_onnx import Kokoro  # deferred: --help works without the model
    kokoro = Kokoro(args.model, args.voices_bin)
    os.makedirs(args.out, exist_ok=True)
    for voice in args.voices.split(","):
        build_pack(kokoro, voice.strip(), args.out, args.speed,
                   args.num_scheme)
    return 0


if __name__ == "__main__":
    sys.exit(main())
