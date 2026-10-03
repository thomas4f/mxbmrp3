#!/usr/bin/env python3
# ============================================================================
# tools/spottergen/generate.py
# Bakes the spotter's RECORDED voice packs - the folders users drop into
# mxbmrp3_data/spotters/ and pick in Settings -> Spotter -> Voice pack.
#
# The packs are MINIMAL by design: one short line per cue (no _2.._9
# variants), no lap times, rider numbers or gaps. The only number is your
# position after a lap ("P four"), stitched by the plugin's mixer from
# seg_p.wav + num_<N>.wav. Every cue the shipped default pack has switched on
# gets a row, so nothing falls through to the default's longer TTS wording;
# cues that are nothing but a number (gap_behind) are muted instead.
#
# The wavs are GENERATED artifacts and deliberately NOT committed - pick the
# voices, run this, and hand out the zips. Never hand-edit a clip; it silently
# stops matching its siblings' voice, level and pacing.
#
# Why bespoke (CLAUDE.md rule): synthesis is off the shelf (sherpa-onnx,
# chatterbox-tts, orpheus-cpp). This script holds what no standard tool knows:
# the cue table, the racing-style number wording that MUST match
# SpotterPhrase::numberWords (--selftest), the chunk-name convention shared
# with spotter_mix.h, the radio chain, and picking the clearest of N takes.
#
# ENGINES (all CPU; everything downloads itself on first use into
# ~/.cache/mxbmrp3_spottergen and the Hugging Face cache):
#   kokoro, kitten  sherpa-onnx; models from the k2-fsa/sherpa-onnx GitHub
#                   "tts-models" release. Fast (~1 s/line), deterministic.
#   chatterbox      Resemble AI Chatterbox Turbo (Hugging Face). Its only
#                   built-in voice is female, so every voice here is CLONED
#                   from a short reference recording: openly licensed speakers
#                   from Kyutai's tts-voices repo (VCTK is CC BY 4.0). ~10 s/line.
#   orpheus         Canopy Labs Orpheus 3B (Q4 GGUF via llama.cpp), built-in
#                   voices leo/dan/zac. ~15 s/line.
# The neural engines occasionally slur or repeat a word, so they render
# --takes candidates per line and keep the one a speech recogniser (Whisper
# base.en via sherpa-onnx) transcribes closest to the intended text.
#
# Setup (one venv covers every engine):
#   pip install sherpa-onnx soundfile scipy chatterbox-tts orpheus-cpp
#   pip install llama-cpp-python \
#       --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cpu
#   pip install "numpy<2"
# The last line is not optional: orpheus-cpp pulls numpy 2, which breaks
# chatterbox-tts; orpheus-cpp itself runs fine on 1.26 despite its metadata.
#
# Usage:
#   python3 generate.py --list                    # the voice presets
#   python3 generate.py --voices all              # every preset, radio FX
#   python3 generate.py --voices orpheus_zac,chatterbox_p232 --clean
#   python3 generate.py --selftest                # CI gate, needs no model
#
# Re-runs are incremental: rendered clips are cached per voice under
# <out>/.raw/ (delete a voice's folder there to re-render it); the radio/clean
# processing, ini, sample.wav and zips are always rebuilt from that cache.
# ============================================================================
import argparse
import difflib
import os
import re
import sys
import tarfile
import urllib.request
import zipfile

# numpy/soundfile are needed only to BAKE. Deferred so --selftest and --help
# run without them - the spottergen-selftest gate lists python3 as its only
# tool, and a top-level import would turn a missing pip module into a gate
# FAILURE on unrelated changes.
np = None
sf = None


def _import_audio_deps():
    global np, sf
    import numpy
    import soundfile
    np = numpy
    sf = soundfile


# ---------------------------------------------------------------------------
# Cue table - the editable part. Keys are the plugin's frozen cue-key API
# (see core/spotter_cue_pack.h); --selftest checks every one still exists.
# ---------------------------------------------------------------------------
CUES = {
    # general
    "session_started": "Race underway.",
    "practice_started": "Practice underway.",
    "quali_started": "Qualifying underway.",
    "warmup_started": "Warm up underway.",
    "gate_drop": "Go, go, go.",
    "session_prestart": "Get ready.",
    "session_ended": "Session over.",
    "session_state": "Session update.",
    "leader_you": "You're leading.",
    "finished_you": "Checkered flag.",
    "penalty_you": "Penalty.",
    "penalty_clear_you": "Penalty cleared.",
    "penalty_change": "Penalty changed.",
    "disqualified_you": "Disqualified.",
    "fuel_low": "Fuel low.",
    "pit_entry_you": "Pit lane.",
    "pit_exit_you": "Pit exit.",
    "voice_preview": "Spotter ready.",
    # timing
    "position_gained": "Position gained.",
    "position_lost": "Position lost.",
    "sector_best": "Best sector.",
    "on_pace_session_best": "Session best pace.",
    "on_pace_personal_best": "Personal best pace.",
    "on_pace_record": "Record pace.",
    "lap_invalidated": "Lap invalid.",
    "fastest_lap_you": "Fastest lap.",
    "personal_best": "Personal best.",
    "record_beaten": "Track record.",
    "session_best": "Session best.",
    "final_lap_you": "Last lap.",
    "overtime_started": "Overtime.",
    "session_time_expired": "Time's up.",
    "ten_minutes_remaining": "Ten minutes.",
    "five_minutes_remaining": "Five minutes.",
    "halfway_point": "Halfway.",
    # opponents
    "fastest_lap_other": "Fastest lap, rival.",
    "leader_other": "Lead change.",
    "finished_leader": "Leader's finished.",
    # proximity - "Behind." not "Rider behind.": speech recognition heard
    # "rider" as "right or" in several voices, next to a cue that IS "Right."
    "rider_behind": "Behind.",
    "rider_behind_clear": "Clear.",
    "rider_left": "Left.",
    "rider_right": "Right.",
    "riders_both_sides": "Both sides.",
    "lapping_traffic": "Lapper ahead.",
    # hazards
    "blue_flag": "Blue flag.",
    "hazard_ahead": "Crash ahead.",
    "wrong_way_ahead": "Wrong way ahead.",
}
# Cues that are nothing but a number we no longer speak: muted, not reworded.
MUTED = ["gap_behind"]
# "P {position}" cues, stitched by the plugin from seg_p.wav + num_<N>.wav.
POSITION_CUES = ["lap_completed", "hotkey_triggered"]
MAX_POS = 40

# sample.wav: a demo of each pack stitched from its own clips (the plugin
# ignores the file). One inner list = one call; a gap follows each.
SAMPLE = [["gate_drop"], ["rider_left"], ["rider_behind"],
          ["rider_behind_clear"], ["blue_flag"], ["seg_p", "num_4"],
          ["final_lap_you"], ["personal_best"], ["finished_you"]]

# ---------------------------------------------------------------------------
# Voice presets: name -> (engine, voice, picker title). For chatterbox the
# voice is a reference recording in huggingface.co/kyutai/tts-voices.
# ---------------------------------------------------------------------------
_VCTK = "vctk/{}_023_enhanced.wav"
VOICES = {
    "chatterbox_p226": ("chatterbox", _VCTK.format("p226"), "Chatterbox p226"),
    "chatterbox_p232": ("chatterbox", _VCTK.format("p232"), "Chatterbox p232"),
    "chatterbox_p237": ("chatterbox", _VCTK.format("p237"), "Chatterbox p237"),
    "chatterbox_p254": ("chatterbox", _VCTK.format("p254"), "Chatterbox p254"),
    "chatterbox_p259": ("chatterbox", _VCTK.format("p259"), "Chatterbox p259"),
    "chatterbox_p315": ("chatterbox", _VCTK.format("p315"), "Chatterbox p315"),
    "chatterbox_p360": ("chatterbox", _VCTK.format("p360"), "Chatterbox p360"),
    "chatterbox_bill_boerst": ("chatterbox", "voice-zero/bill_boerst.wav",
                               "Chatterbox Bill"),
    "chatterbox_stuart_bell": ("chatterbox", "voice-zero/stuart_bell.wav",
                               "Chatterbox Stuart"),
    "chatterbox_peter_yearsley": ("chatterbox",
                                  "voice-zero/peter_yearsley.wav",
                                  "Chatterbox Peter"),
    "orpheus_leo": ("orpheus", "leo", "Orpheus Leo"),
    "orpheus_dan": ("orpheus", "dan", "Orpheus Dan"),
    "orpheus_zac": ("orpheus", "zac", "Orpheus Zac"),
    "kokoro_adam": ("kokoro", "am_adam", "Kokoro Adam"),
    "kokoro_michael": ("kokoro", "am_michael", "Kokoro Michael"),
    "kokoro_lewis_uk": ("kokoro", "bm_lewis", "Kokoro Lewis (UK)"),
    "kokoro_george_uk": ("kokoro", "bm_george", "Kokoro George (UK)"),
    "kitten_m3": ("kitten", "expr-voice-3-m", "Kitten M3"),
}

CACHE = os.path.join(os.path.expanduser("~"), ".cache", "mxbmrp3_spottergen")
SHERPA_TTS = ("https://github.com/k2-fsa/sherpa-onnx/releases/download/"
              "tts-models/{}.tar.bz2")
SHERPA_ASR = ("https://github.com/k2-fsa/sherpa-onnx/releases/download/"
              "asr-models/{}.tar.bz2")
KYUTAI_VOICES = "https://huggingface.co/kyutai/tts-voices/resolve/main/{}"
SR = 24000  # every engine is resampled to this before processing

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
# Downloads
# ---------------------------------------------------------------------------
def fetch_tarball(url, name):
    """Download + unpack a sherpa-onnx model tarball once; return its dir."""
    d = os.path.join(CACHE, name)
    if not os.path.isdir(d):
        os.makedirs(CACHE, exist_ok=True)
        print(f"  downloading {name} ...", flush=True)
        tmp = d + ".tar.bz2"
        urllib.request.urlretrieve(url.format(name), tmp)
        with tarfile.open(tmp) as t:
            t.extractall(CACHE)
        os.remove(tmp)
    return d


def fetch_file(url, rel):
    path = os.path.join(CACHE, "refs", rel.replace("/", "_"))
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        urllib.request.urlretrieve(url.format(rel), path)
    return path


# ---------------------------------------------------------------------------
# Engines: each returns gen(text) -> float32 mono at SR.
# ---------------------------------------------------------------------------
def _resample(x, sr):
    if sr == SR:
        return x
    from scipy.signal import resample_poly
    g = np.gcd(SR, sr)
    return resample_poly(x, SR // g, sr // g).astype(np.float32)


def make_engine(engine, voice):
    if engine in ("kokoro", "kitten"):
        import sherpa_onnx
        m = sherpa_onnx.OfflineTtsModelConfig(num_threads=4, provider="cpu")
        if engine == "kokoro":
            d = fetch_tarball(SHERPA_TTS, "kokoro-multi-lang-v1_0")
            m.kokoro = sherpa_onnx.OfflineTtsKokoroModelConfig(
                model=f"{d}/model.onnx", voices=f"{d}/voices.bin",
                tokens=f"{d}/tokens.txt", data_dir=f"{d}/espeak-ng-data",
                dict_dir=f"{d}/dict",
                lexicon=f"{d}/lexicon-us-en.txt,{d}/lexicon-gb-en.txt")
        else:
            d = fetch_tarball(SHERPA_TTS, "kitten-mini-en-v0_8")
            m.kitten = sherpa_onnx.OfflineTtsKittenModelConfig(
                model=f"{d}/model.onnx", voices=f"{d}/voices.bin",
                tokens=f"{d}/tokens.txt", data_dir=f"{d}/espeak-ng-data")
        tts = sherpa_onnx.OfflineTts(sherpa_onnx.OfflineTtsConfig(model=m))
        names = tts_speaker_names(f"{d}/model.onnx" if engine == "kokoro"
                                  else f"{d}/model.onnx")
        sid = names.index(voice)

        def gen(text):
            a = tts.generate(text, sid=sid, speed=1.0)
            return _resample(np.asarray(a.samples, np.float32), a.sample_rate)
        return gen

    if engine == "chatterbox":
        import torch
        from chatterbox.tts_turbo import ChatterboxTurboTTS
        torch.set_num_threads(os.cpu_count() or 4)
        m = ChatterboxTurboTTS.from_pretrained("cpu")
        m.prepare_conditionals(fetch_file(KYUTAI_VOICES, voice))
        return lambda text: _resample(
            m.generate(text).squeeze().numpy().astype(np.float32), m.sr)

    if engine == "orpheus":
        # orpheus-cpp opens the GGUF with n_ctx=0 (the model's full 128k
        # context), whose KV cache gets the process OOM-killed on a 16 GB
        # machine. One line of speech needs a few hundred tokens.
        import llama_cpp
        real = llama_cpp.Llama
        llama_cpp.Llama = lambda *a, **k: real(*a, **{**k, "n_ctx": 4096})
        from orpheus_cpp import OrpheusCpp
        m = OrpheusCpp(lang="en", n_threads=os.cpu_count() or 4,
                       verbose=False)
        llama_cpp.Llama = real

        def gen(text):
            sr, a = m.tts(text, options={"voice_id": voice})
            return _resample(a.squeeze().astype(np.float32) / 32768.0, sr)
        return gen

    raise SystemExit(f"unknown engine {engine}")


def tts_speaker_names(onnx_path):
    import onnxruntime
    meta = onnxruntime.InferenceSession(
        onnx_path, providers=["CPUExecutionProvider"]
    ).get_modelmeta().custom_metadata_map
    return meta["speaker_names"].split(",")


# ---------------------------------------------------------------------------
# Picking the clearest take: Whisper base.en transcribes each candidate and
# the closest match to the intended words wins. Overlong takes are penalised
# - a repeated word or a trailing breath transcribes fine but sounds wrong.
# ---------------------------------------------------------------------------
_asr = None


def _words(t):
    t = t.lower().replace("-", " ").replace("p4", "pee four")
    t = t.replace("p four", "pee four").replace("p,", "pee")
    t = re.sub(r"\d+", lambda m: f" {num_words(int(m.group()))} "
               if int(m.group()) < 1000 else m.group(), t)
    return re.sub(r"[^a-z ]", "", t).split()


def clarity(x, text):
    global _asr
    if _asr is None:
        import sherpa_onnx
        d = fetch_tarball(SHERPA_ASR, "sherpa-onnx-whisper-base.en")
        _asr = sherpa_onnx.OfflineRecognizer.from_whisper(
            encoder=f"{d}/base.en-encoder.int8.onnx",
            decoder=f"{d}/base.en-decoder.int8.onnx",
            tokens=f"{d}/base.en-tokens.txt", num_threads=2)
    s = _asr.create_stream()
    s.accept_waveform(SR, x)
    _asr.decode_stream(s)
    want = _words(text)
    score = difflib.SequenceMatcher(None, _words(s.result.text), want).ratio()
    overlong = max(0.0, x.size / SR - 0.4 * len(want) - 0.5)
    return score - 0.3 * overlong


# ---------------------------------------------------------------------------
# Audio chain
# ---------------------------------------------------------------------------
def trim(x, pad_ms=5.0, thresh=0.02):
    peak = float(np.max(np.abs(x)))
    if peak <= 0.0:
        return x
    above = np.flatnonzero(np.abs(x) >= thresh * peak)
    pad = int(pad_ms / 1000.0 * SR)
    return x[max(0, int(above[0]) - pad):min(x.size, int(above[-1]) + pad)]


def edge_clean(x, frame_ms=20, gap_ms=120, min_ms=140, rel=0.25):
    """Drop breath/click islands left at the start and end of a clip."""
    n = int(SR * frame_ms / 1000)
    f = x.size // n
    if f < 3:
        return x
    e = np.sqrt(np.mean(x[:f * n].reshape(f, n) ** 2, axis=1))
    on = e > 0.05 * e.max()
    segs, i = [], 0
    while i < f:
        if on[i]:
            j = i
            while j < f and on[j]:
                j += 1
            if segs and (i - segs[-1][1]) * frame_ms < gap_ms:
                segs[-1][1] = j
            else:
                segs.append([i, j])
            i = j
        else:
            i += 1
    if not segs:
        return x

    def weak(s):
        return ((s[1] - s[0]) * frame_ms < min_ms
                or e[s[0]:s[1]].max() < rel * e.max())
    while len(segs) > 1 and weak(segs[0]):
        segs.pop(0)
    while len(segs) > 1 and weak(segs[-1]):
        segs.pop()
    pad = int(0.01 * SR)
    return x[max(0, segs[0][0] * n - pad):min(x.size, segs[-1][1] * n + pad)]


def level(x, target_rms=0.12, peak=0.89):
    x = np.asarray(x, np.float32)
    rms = float(np.sqrt(np.mean(x ** 2)))
    if rms > 0.0:
        x = x * (target_rms / rms)
    return (x * (peak / max(1e-9, float(np.max(np.abs(x)))))).astype(np.float32)


def bandpass(x, lo=280.0, hi=3400.0, edge=0.25):
    spec = np.fft.rfft(x)
    freqs = np.fft.rfftfreq(x.size, 1.0 / SR)
    gain = np.zeros_like(freqs)
    lo0, lo1 = lo * (1 - edge), lo
    hi0, hi1 = hi, hi * (1 + edge)
    rise = (freqs >= lo0) & (freqs < lo1)
    gain[rise] = 0.5 - 0.5 * np.cos(np.pi * (freqs[rise] - lo0) / (lo1 - lo0))
    gain[(freqs >= lo1) & (freqs <= hi0)] = 1.0
    fall = (freqs > hi0) & (freqs <= hi1)
    gain[fall] = 0.5 + 0.5 * np.cos(np.pi * (freqs[fall] - hi0) / (hi1 - hi0))
    return np.fft.irfft(spec * gain, n=x.size).astype(np.float32)


def noise_bed(n, rng, lvl):
    return bandpass(rng.normal(0.0, 1.0, n).astype(np.float32)) * lvl


def squelch_click(rng, ms=45.0, lvl=0.5):
    n = int(ms / 1000.0 * SR)
    burst = bandpass(rng.normal(0.0, 1.0, n).astype(np.float32), lo=600,
                     hi=3800)
    env = np.exp(-np.linspace(0.0, 9.0, n, dtype=np.float32))
    return burst * env * lvl / max(1e-9, float(np.max(np.abs(burst))))


def drive(x, amount=3.2):
    x = bandpass(x / max(1e-9, float(np.max(np.abs(x)))))
    return np.tanh(amount * x) / np.tanh(amount)


def radio_full(x, seed, noise=0.016):
    """Whole-transmission treatment: bandpass, drive, static bed, clicks."""
    rng = np.random.default_rng(seed)
    x = drive(x)
    x = x + noise_bed(x.size, rng, noise)
    pad = noise_bed(int(0.10 * SR), rng, noise)
    return np.concatenate([squelch_click(rng), pad, x, pad,
                           squelch_click(rng, ms=35, lvl=0.4)])


def write_wav(path, x, radio):
    """Clean: 24 kHz. Radio: lowpass (the drive's harmonics exceed the new
    Nyquist, so skipping it aliases audibly), decimate to 12 kHz - the band
    stops at ~4.3 kHz, so more would be bytes for nothing - and re-peak, since
    that lowpass overshoots."""
    if radio:
        x = bandpass(x)[::2]
    x = x * (0.89 / max(1e-9, float(np.max(np.abs(x)))))
    sf.write(path, x, SR // 2 if radio else SR, subtype="PCM_16")


# ---------------------------------------------------------------------------
def clip_texts():
    items = dict(CUES)
    items["seg_p"] = "P,"
    for n in range(1, MAX_POS + 1):
        items[f"num_{n}"] = num_words(n) + "."
    return items


def render_raw(name, out, takes):
    """Render (or reuse) every clip for one voice into <out>/.raw/<name>/."""
    engine, voice, _ = VOICES[name]
    raw = os.path.join(out, ".raw", name)
    os.makedirs(raw, exist_ok=True)
    todo = {k: t for k, t in clip_texts().items()
            if not os.path.exists(os.path.join(raw, f"{k}.wav"))}
    if not todo:
        return raw
    print(f"{name}: rendering {len(todo)} clips ({engine})", flush=True)
    gen = make_engine(engine, voice)
    tries = 1 if engine in ("kokoro", "kitten") else takes
    for i, (key, text) in enumerate(todo.items()):
        best = None
        for _ in range(tries):
            x = level(edge_clean(trim(gen(text))))
            score = clarity(x, text) if tries > 1 else 1.0
            if best is None or score > best[0]:
                best = (score, x)
            if score >= 0.95:
                break
        sf.write(os.path.join(raw, f"{key}.wav"), best[1], SR,
                 subtype="FLOAT")
        if i % 20 == 19:
            print(f"  {name}: {i + 1}/{len(todo)}", flush=True)
    return raw


def build_pack(name, out, radio, takes):
    raw = render_raw(name, out, takes)

    def load(k):
        return sf.read(os.path.join(raw, f"{k}.wav"), dtype="float32")[0]

    title = VOICES[name][2]
    folder = name + ("_radio" if radio else "")
    pack = os.path.join(out, folder)
    os.makedirs(pack, exist_ok=True)
    lines = [f"; mxbmrp3 spotter voice pack: {title}",
             "; Minimal: one line per cue, no variants, the only number is "
             "your position.",
             "; Generated by tools/spottergen/generate.py - do not hand-edit "
             "the clips.",
             "; Install: extract this folder into "
             "<game>\\plugins\\mxbmrp3_data\\spotters\\",
             "[pack]", f"name = {title}{' (radio)' if radio else ''}",
             "[Cues]"]
    for i, (key, text) in enumerate(CUES.items()):
        x = load(key)
        write_wav(os.path.join(pack, f"{key}.wav"),
                  radio_full(x, seed=i + 1) if radio else x, radio)
        lines += [f"{key} = {text}", f"{key}_wav = {key}.wav"]
    lines += [f"{key} =" for key in MUTED]
    # Stitched chunks get the drive but no static bed or clicks: a per-chunk
    # bed drops out in the stitch gaps, and clicks mark whole transmissions.
    for key in ["seg_p"] + [f"num_{n}" for n in range(1, MAX_POS + 1)]:
        x = load(key)
        write_wav(os.path.join(pack, f"{key}.wav"),
                  drive(x) if radio else x, radio)
    for key in POSITION_CUES:
        lines += [f"{key} = P {{position}}.",
                  f"{key}_mix = seg_p.wav {{position}}"]
    with open(os.path.join(pack, "spotter.ini"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    rate = SR // 2 if radio else SR
    parts = []
    for call in SAMPLE:
        for key in call:
            parts.append(sf.read(os.path.join(pack, f"{key}.wav"),
                                 dtype="float32")[0])
        parts.append(np.zeros(int(0.5 * rate), np.float32))
    sf.write(os.path.join(pack, "sample.wav"), np.concatenate(parts), rate,
             subtype="PCM_16")

    zip_path = os.path.join(out, f"{folder}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for fn in sorted(os.listdir(pack)):
            z.write(os.path.join(pack, fn), f"{folder}/{fn}")
    print(f"{folder}: {zip_path} ({os.path.getsize(zip_path) / 1e6:.1f} MB)",
          flush=True)
    return folder


def selftest():
    """Wording parity with SpotterPhrase::numberWords, via the fixture both
    sides assert, and every cue key here still exists in the plugin."""
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

    # The cue table is a third list of keys, after the registry and the
    # shipped ini, and nothing else checks it. A pack baked from a stale key
    # carries a row the plugin logs as "will never be spoken" - that cue then
    # has no audio at all, silently.
    header = os.path.join(here, "..", "..", "mxbmrp3", "core",
                          "spotter_cue_pack.h")
    with open(header, encoding="utf-8") as f:
        # A registry row is `{ "key", "what", SpotterPhrase::Category::X }`;
        # the count guard below catches this scan when the row shape changes.
        known = set(re.findall(r'\{\s*"([a-z_0-9]+)",\s*"', f.read()))
    if len(known) < 20:
        print(f"SELFTEST FAIL: only {len(known)} cue keys parsed from "
              f"{header} - the registry's shape changed, fix this scan")
        return 1
    baked = list(CUES) + MUTED + POSITION_CUES
    stale = sorted(k for k in baked if k not in known)
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
    ap = argparse.ArgumentParser(
        description="Bake minimal spotter voice packs (see the file header).")
    ap.add_argument("--voices", default="all",
                    help="comma-separated preset names, or 'all' (--list)")
    ap.add_argument("--clean", action="store_true",
                    help="no radio FX (24 kHz, no static or clicks)")
    ap.add_argument("--both", action="store_true",
                    help="build the radio AND the clean pack of each voice")
    ap.add_argument("--takes", type=int, default=3,
                    help="candidates per line for the neural engines; the "
                         "clearest to speech recognition is kept")
    ap.add_argument("--out", default="packs")
    ap.add_argument("--list", action="store_true", help="list voice presets")
    ap.add_argument("--selftest", action="store_true",
                    help="verify num_words against the shared fixture "
                         "(tests/fixtures/spotter_number_words.txt) and the "
                         "cue keys against the plugin's registry, then exit; "
                         "needs no model. The C++ side of the number handshake "
                         "is test_spotter_phrase.cpp.")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if args.list:
        for name, (engine, _voice, title) in VOICES.items():
            print(f"{name:28} {engine:11} {title}")
        return 0

    _import_audio_deps()
    names = list(VOICES) if args.voices == "all" else \
        [v.strip() for v in args.voices.split(",")]
    unknown = [n for n in names if n not in VOICES]
    if unknown:
        raise SystemExit(f"unknown voice(s) {unknown}; see --list")
    variants = [True, False] if args.both else [not args.clean]
    os.makedirs(args.out, exist_ok=True)
    built = []
    for name in names:
        for radio in variants:
            built.append(build_pack(name, args.out, radio, args.takes))
    if len(built) > 1:
        all_zip = os.path.join(args.out, "all_packs.zip")
        with zipfile.ZipFile(all_zip, "w", zipfile.ZIP_DEFLATED) as z:
            for folder in built:
                for fn in sorted(os.listdir(os.path.join(args.out, folder))):
                    z.write(os.path.join(args.out, folder, fn),
                            f"{folder}/{fn}")
        print(f"all: {all_zip} ({os.path.getsize(all_zip) / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
