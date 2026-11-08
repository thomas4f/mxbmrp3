#!/usr/bin/env python3
# ============================================================================
# tools/spottergen/elevenlabs.py
# Bakes a FULL recorded spotter pack with ElevenLabs: every line the shipped
# pack speaks (mxbmrp3_data/spotters/default/spotter.ini), every alternate,
# and the default-quiet rows (still commented out, so they stay off). Where
# generate.py bakes MINIMAL packs from local models, this one reads the
# shipped wording itself, so a new line in a release is one incremental
# rerun away.
#
# ONE CLIP PER LINE, NOTHING STITCHED. Every cue is one whole `_wav`: pieces
# joined at playback never sound like one sentence, so a recorded pack gives
# up the values that change (positions, gaps, trick names, rider numbers).
# A shipped line's optional groups `[...]` are dropped; a line that still
# names a variable is replaced by its row in recorded_lines.ini, which may
# also mute a cue that is nothing but numbers. --selftest fails on a shipped
# line that needs a row and has none, so a release that adds one is caught.
#
# TONE. ElevenLabs has no "this is motocross" setting; tone comes from the
# text. Each line gets a MOOD (mood_for below) that becomes an audio tag
# (`[excited]`, `[disappointed]`, ...) in the VOICE text only - the subtitle
# stays the written line. PRONOUNCE fixes words a model reads wrong out of
# context ("lead" as in leading); VOICE_TEXT overrides single lines (boos),
# and a written "Sigh" is sent as a [sighs] tag.
#
# TAKES. Each clip is rendered up to --takes times and the first take a speech
# recogniser (Whisper base.en, generate.py's clarity()) hears as the intended
# words is kept, so most clips cost one take. --no-asr keeps the first take.
#
# The key is read from ELEVENLABS_API_KEY and is never written anywhere. The
# wavs are generated artifacts and are NOT committed (see README.md).
#
# Why bespoke (AGENTS.md rule): the HTTP calls are two endpoints; what no
# standard tool knows is the pack format and the shipped wording.
#
# Usage:
#   python3 elevenlabs.py plan                       # offline: clips + characters
#   python3 elevenlabs.py voices [--search sports]   # Voice Library candidates
#   python3 elevenlabs.py add PUBLIC_OWNER_ID VOICE_ID NAME
#   python3 elevenlabs.py audition --voices "Name=voice_id,..." --out DIR
#   python3 elevenlabs.py build --voice VOICE_ID --name folder --title "Title" --out DIR
#   python3 elevenlabs.py --selftest                 # CI-safe, no key, no network
# Setup for build/audition: pip install numpy soundfile (+ sherpa-onnx for ASR).
# ============================================================================
import argparse
import concurrent.futures
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import generate as gen  # noqa: E402  (num_words, audio chain, clarity)

SHIPPED_INI = os.path.join(ROOT, "mxbmrp3_data", "spotters", "default",
                           "spotter.ini")
RECORDED_INI = os.path.join(HERE, "recorded_lines.ini")
API = "https://api.elevenlabs.io"
SR = 24000

# ---------------------------------------------------------------------------
# Tone - the editable part.
# ---------------------------------------------------------------------------
MOOD_TAGS = {"neutral": "", "urgent": "", "excited": "[excited] ",
             "disappointed": "[disappointed] ", "playful": "[playful] ",
             "concerned": "[concerned] ",
             "encouraging": "[encouraging] "}
MOOD_SPEED = {"urgent": 1.1}


def mood_for(key):
    base = root_of(key)
    if base in ("rider_behind", "rider_behind_clear", "rider_left",
                "rider_right", "riders_both_sides", "lapping_traffic",
                "blue_flag", "hazard_ahead", "wrong_way_ahead", "gate_drop"):
        return "urgent"
    if base.startswith("achievement_"):
        return "playful"
    if base in ("position_lost", "penalty_you", "lap_invalidated",
                "disqualified_you", "crashed_you", "retired_you",
                "trick_crashed"):
        return "disappointed"
    if base in ("fuel_low", "fuel_critical"):
        return "concerned"
    if base in ("fastest_lap_you", "personal_best", "record_beaten",
                "session_best", "leader_you", "finished_you", "holeshot_you",
                "position_gained", "sector_best", "trick_landed",
                "trick_combo", "trick_started") or base.startswith("on_pace_"):
        return "excited"
    return "neutral"


# Voice-text-only respellings, applied to whole words.
PRONOUNCE = {"lead": "leed", "warmup": "warm up", "prefinal": "pre-final",
             "oppo": "oppo", "endo": "endo"}
# Whole-line voice text for lines that need more than a mood tag.
# Keyed by the LINE, not the cue key, so it follows a line when the
# alternates are renumbered.
VOICE_TEXT = {
    "Booooo!": "[booing] Booooo!",
    "Ohhh no! Booo!": "[gasps] Ohhh no! [booing] Booo!",
    "Denied! Booooo!": "Denied! [booing] Booooo!",
}

# The stumbles an urgent call plays first when it cuts a clip off ([Mix]
# interrupt, core/spotter_cue_pack.h TAKING TURNS): one-word stumbles and
# wordless sounds. A tag alone is refused as empty text, so a sound is sent
# as "[tag] ...", which voices nothing else. Never subtitled, so no
# recogniser check. Never a direction word: a filler leads into "left".
# A filler delays the urgent call behind it by its own length, so a take
# longer than this is left out of the pack (it stays in the cache).
MAX_FILLER_MS = 800

FILLERS = {
    "interrupt_eh": "Eh—",
    "interrupt_whoa": "Whoa—",
    "interrupt_ah": "Ah—",
    "interrupt_wait": "Wait—",
    "interrupt_oh": "Oh—",
    "interrupt_ope": "Ope—",
    "interrupt_hey": "Hey—",
    "interrupt_ahem": "Ahem—",
    "interrupt_pfft": "Pfft—",
    "interrupt_uh": "Uh—",
    "interrupt_er": "Er—",
    "interrupt_coughs": "[coughs] ...",
    "interrupt_clears_throat": "[clears throat] ...",
    "interrupt_sighs": "[sighs] ...",
    "interrupt_gasps": "[gasps] ...",
    "interrupt_laughs": "[laughs] ...",
    "interrupt_exhales": "[exhales] ...",
    "interrupt_exhales_sharply": "[exhales sharply] ...",
    "interrupt_sniffs": "[sniffs] ...",
    "interrupt_snorts": "[snorts] ...",
    "interrupt_chuckles": "[chuckles] ...",
    "interrupt_inhales_sharply": "[inhales sharply] ...",
    "interrupt_scoffs": "[scoffs] ...",
    "interrupt_groans": "[groans] ...",
    "interrupt_tuts": "[tuts] ...",
    "interrupt_whistles": "[whistles] ...",
    "interrupt_gulps": "[gulps] ...",
}

# The soft ones also open an ordinary line now and then ([Mix] lead_in, the
# same TAKING TURNS note): sounds that suit any mood, unlike a gasp before
# "Lap done" or a whistle before a penalty. LEAD_IN_CHANCE is the percent.
LEAD_INS = ("interrupt_uh", "interrupt_er", "interrupt_eh", "interrupt_ahem",
            "interrupt_clears_throat", "interrupt_coughs")
LEAD_IN_CHANCE = 15

AUDITION = [
    "Left.",
    "Blue flag, faster rider closing.",
    "Behind.",
    "Go, go, go!",
    "[excited] New track record! One forty eight point two.",
    "[disappointed] Position lost. Down two.",
    "[excited] You've got the leed now.",
    "Lead change, rider four seventy six out front.",
    "[sighs] And you were doing so well.",
    "Ooh, what's this?",
    "[excited] Double backflip, stuck it!",
    "[playful] Iron Butt, gold. Your backside has filed a complaint.",
]


# ---------------------------------------------------------------------------
# Sources of truth, read from the tree.
# ---------------------------------------------------------------------------
def read_rows(path=SHIPPED_INI):
    """(key, template, active) for every cue row of a pack file: active rows,
    and the commented `;key = ...` rows (default-quiet cues). Indented
    `;   key = ...` lines are documentation examples, not rows."""
    rows, in_cues = [], False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.strip().startswith("[") and not line.startswith(";"):
                in_cues = line.strip().lower() == "[cues]"
                continue
            if not in_cues:
                continue
            m = re.match(r"^(;)?([a-z][a-z_0-9]*)\s*=\s*(.*)$", line)
            if m and not m.group(2).endswith(("_wav", "_mix")):
                rows.append((m.group(2), m.group(3).strip(), not m.group(1)))
    return rows


# ---------------------------------------------------------------------------
# Template -> recorded line.
# ---------------------------------------------------------------------------
def parse_template(t):
    """Nodes: ("text", s) / ("var", name) / ("group", [nodes])."""
    stack = [[]]
    for tok in re.split(r"(\{[a-z_]+\}|\[|\])", t):
        if tok == "[":
            if len(stack) > 1:
                raise ValueError(f"nested group: {t}")
            stack.append([])
        elif tok == "]":
            grp = stack.pop()
            stack[-1].append(("group", grp))
        elif tok.startswith("{") and tok.endswith("}"):
            stack[-1].append(("var", tok[1:-1]))
        elif tok:
            stack[-1].append(("text", tok))
    if len(stack) != 1:
        raise ValueError(f"unclosed group: {t}")
    return stack[0]


def tidy(s):
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\s+([,.!?])", r"\1", s)
    s = re.sub(r",\s*,", ",", s)
    return re.sub(r"^[\s,.;:]+", "", s).strip()


def respell(text):
    def fix(m):
        w = m.group(0)
        r = PRONOUNCE.get(w.lower())
        if r is None:
            return w
        return r.capitalize() if w[0].isupper() else r
    return re.sub(r"[A-Za-z]+", fix, text)


def say_places(text):
    """"P3" is how a subtitle writes a place; the voice is sent "P three"."""
    return re.sub(r"\bP(\d{1,2})\b",
                  lambda m: "P " + gen.num_words(int(m.group(1))), text)


def slug(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")[:40] or "x"


def strip_groups(template):
    """The shipped line with its optional groups dropped, as text."""
    return tidy("".join(v for k, v in parse_template(template) if k != "group"))


def split_mood(line):
    """recorded_lines.ini may open a line with its own [mood] (a MOOD_TAGS
    name), which outranks mood_for and is not part of the subtitle."""
    m = re.match(r"^\[([a-z]+)\]\s*", line)
    if m and m.group(1) in MOOD_TAGS:
        return m.group(1), line[m.end():]
    return None, line


def root_of(key):
    """The cue a row belongs to: its `_2`.. alternate suffix and then its
    `_p1`..`_p99` / `_last` position suffix gone (spotter_position_rows.h)."""
    key = re.sub(r"_(?:[2-9]|[1-9][0-9])$", "", key)
    return re.sub(r"_(?:p[1-9][0-9]?|last)$", "", key)


class Clip:
    """One wav to render: `voice` is what is sent (mood tag, respellings),
    `check` what the recogniser should hear."""
    __slots__ = ("name", "voice", "check", "mood")

    def __init__(self, name, voice, check, mood):
        self.name, self.voice, self.check, self.mood = name, voice, check, mood


def build_plan():
    """-> (ini lines, {clip name: Clip}, problems). Pure: reads the tree only.
    Each shipped cue keeps its place; a cue recorded_lines.ini defines is
    replaced there by that file's rows, alternates and all."""
    shipped = read_rows()
    recorded = read_rows(RECORDED_INI)
    own = {}
    for key, text, active in recorded:
        own.setdefault(root_of(key), []).append((key, text, active))
    shipped_roots = {root_of(k) for k, _, _ in shipped}
    problems = [f"recorded_lines.ini: {r} is not a shipped cue"
                for r in own if r not in shipped_roots]
    problems += [f"recorded_lines.ini: {r} has alternates but no base row"
                 for r, rows in own.items() if rows[0][0] != r]
    problems += [f"recorded_lines.ini: {k} = {t}" for k, t, _ in recorded
                 if "{" in t or "[" in split_mood(t)[1]]

    rows, done = [], set()
    for key, template, active in shipped:
        root = root_of(key)
        if root in own:
            if root not in done:
                rows += own[root]
                done.add(root)
            continue
        line = strip_groups(template)
        if "{" in line:
            problems.append(f"{key} = {template}: needs a row in "
                            "recorded_lines.ini")
            continue
        rows.append((key, line, active))

    lines, clips = [], {}
    for key, line, active in rows:
        c = "" if active else ";"
        if not line:
            lines.append(f"{c}{key} =")
            continue
        own_mood, line = split_mood(line)
        mood = own_mood or mood_for(key)
        voice = VOICE_TEXT.get(line) or MOOD_TAGS[mood] + respell(say_places(
            re.sub(r"\b[Ss]igh\b\.?", "[sighs]", line)))
        clips[key] = Clip(key, voice, say_places(line), mood)
        lines += [f"{c}{key} = {line}", f"{c}{key}_wav = {key}.wav"]
    for name, voice in FILLERS.items():
        clips[name] = Clip(name, voice, "", "urgent")
    return lines, clips, problems


# ---------------------------------------------------------------------------
# ElevenLabs
# ---------------------------------------------------------------------------
def api(method, path, body=None, raw=False, retries=5):
    key = os.environ.get("ELEVENLABS_API_KEY")
    if not key:
        raise SystemExit("ELEVENLABS_API_KEY is not set")
    data = json.dumps(body).encode() if body is not None else None
    for attempt in range(retries):
        req = urllib.request.Request(API + path, data=data, method=method,
                                     headers={"xi-api-key": key,
                                              "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                payload = r.read()
                return payload if raw else json.loads(payload or b"{}")
        except urllib.error.HTTPError as e:
            msg = e.read().decode(errors="replace")[:400]
            if e.code in (429, 500, 502, 503, 504) and attempt + 1 < retries:
                time.sleep(2 ** attempt * 2)
                continue
            raise RuntimeError(f"{method} {path}: HTTP {e.code}: {msg}") from e
        except (urllib.error.URLError, OSError):  # incl. connection resets
            if attempt + 1 < retries:
                time.sleep(2 ** attempt * 2)
                continue
            raise


def credits_used():
    try:
        s = api("GET", "/v1/user/subscription")
        return s.get("character_count"), s.get("character_limit")
    except Exception as e:  # informational only
        print(f"  (subscription read failed: {e})")
        return None, None


def tts(voice_id, text, model, speed=1.0):
    import numpy as np
    body = {"text": text, "model_id": model,
            "voice_settings": {"stability": 0.5, "similarity_boost": 0.75,
                               "speed": speed}}
    pcm = api("POST", f"/v1/text-to-speech/{voice_id}?output_format=pcm_{SR}",
              body, raw=True)
    return np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0


def first_burst(x, gap_s=0.12, floor=0.05):
    """A one-word filler is one sound: keep it up to its first pause, with a
    short fade, so a breath or click after it does not delay the urgent call.
    Not for "Hm, wait": its comma pause would be taken for the end."""
    import numpy as np
    loud = np.abs(x) > floor
    run, end = 0, x.size
    hop = int(gap_s * SR)
    for i in range(0, x.size, hop // 4):
        if loud[i:i + hop // 4].any():
            run = i + hop // 4
        elif run and i - run >= hop:
            end = run
            break
    y = x[:end].copy()
    f = min(int(0.03 * SR), y.size // 3)
    if f:
        y[-f:] *= np.linspace(1.0, 0.0, f, dtype=y.dtype)
    return y


def strip_tags(t):
    return re.sub(r"\[[^\]]*\]\s*", "", t)


def render_clip(clip, voice_id, model, raw_dir, takes, use_asr):
    gen._import_audio_deps()
    path = os.path.join(raw_dir, clip.name + ".wav")
    # The empty slot is where the stitched baker kept a fragment's target, so
    # whole lines it already rendered stay cached.
    sig = json.dumps([clip.voice, "", model, voice_id])
    sig_path = path + ".sig"
    if os.path.exists(path) and os.path.exists(sig_path):
        with open(sig_path, encoding="utf-8") as f:
            if f.read() == sig:
                return clip.name, 0, None
    best, sent = None, 0
    speed = MOOD_SPEED.get(clip.mood, 1.0)
    for _take in range(max(1, takes)):
        sent += len(clip.voice)
        x = tts(voice_id, clip.voice, model, speed=speed)
        if x.size == 0:
            continue
        x = gen.level(gen.trim(x))
        if not clip.check and " " not in clip.voice:
            x = first_burst(x)
        score = gen.clarity(x, strip_tags(clip.check)) \
            if use_asr and clip.check else 1.0
        if best is None or score > best[0]:
            best = (score, x)
        if score >= 0.9:
            break
    if best is None:
        return clip.name, sent, "no audio"
    gen.sf.write(path, best[1], SR, subtype="FLOAT")
    with open(sig_path, "w", encoding="utf-8") as f:
        f.write(sig)
    return clip.name, sent, None if best[0] >= 0.6 else f"unclear ({best[0]:.2f})"


def render_all(clips, voice_id, model, raw_dir, takes, use_asr, jobs):
    os.makedirs(raw_dir, exist_ok=True)
    before, limit = credits_used()
    total, problems, done = 0, [], 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = [ex.submit(render_clip, c, voice_id, model, raw_dir, takes,
                          use_asr) for c in clips]
        for fut in concurrent.futures.as_completed(futs):
            name, sent, problem = fut.result()
            total += sent
            done += 1
            if problem:
                problems.append(f"{name}: {problem}")
            if done % 25 == 0:
                print(f"  {done}/{len(futs)} clips, {total} characters sent",
                      flush=True)
    after, _ = credits_used()
    print(f"rendered {len(clips)} clips, {total} characters sent", flush=True)
    if before is not None and after is not None:
        print(f"credits: {after - before} used this run, {after}/{limit} "
              "this period", flush=True)
    for p in problems:
        print(f"  CHECK BY EAR: {p}")
    return problems


# ---------------------------------------------------------------------------
def write_pack(lines, clips, raw_dir, pack_dir, title):
    gen._import_audio_deps()
    os.makedirs(pack_dir, exist_ok=True)
    fillers = []
    for c in clips.values():
        src = os.path.join(raw_dir, c.name + ".wav")
        if os.path.exists(src):
            x = gen.sf.read(src, dtype="float32")[0]
            if c.name in FILLERS:
                if x.size * 1000 > MAX_FILLER_MS * SR:
                    continue
                fillers.append(c.name + ".wav")
            gen.sf.write(os.path.join(pack_dir, c.name + ".wav"),
                         x * (0.89 / max(1e-9, float(abs(x).max()))), SR,
                         subtype="PCM_16")
    head = [f"; mxbmrp3 spotter voice pack: {title}",
            "; Generated by tools/spottergen/elevenlabs.py from the shipped "
            "wording - do not hand-edit the clips.",
            "; Install: extract this folder into "
            "<game>\\plugins\\mxbmrp3_data\\spotters\\ and pick it in "
            "Settings > Spotter > Voice pack.",
            "; Commented rows are the callouts the shipped pack keeps quiet: "
            "remove the ; from a row AND its _wav row to turn one on.",
            "[pack]", f"name = {title}",
            "[Mix]", "interrupt = " + " ".join(fillers),
            "lead_in = " + " ".join(f for f in fillers
                                    if f[:-4] in LEAD_INS),
            f"lead_in_chance = {LEAD_IN_CHANCE}",
            "[Cues]"]
    with open(os.path.join(pack_dir, "spotter.ini"), "w", encoding="utf-8") as f:
        f.write("\n".join(head + lines) + "\n")


def reel(lines, pack_dir, out_path):
    """A listening reel: every active clip once, in pack order."""
    import numpy as np
    gen._import_audio_deps()
    parts, index = [], []
    for row in lines:
        key, _, wav = row.partition(" = ")
        if row.startswith(";") or not key.endswith("_wav"):
            continue
        p = os.path.join(pack_dir, wav)
        if os.path.exists(p):
            parts += [gen.sf.read(p, dtype="float32")[0],
                      np.zeros(int(0.6 * SR), np.float32)]
            index.append(key[:-4])
    gen.sf.write(out_path, np.concatenate(parts), SR, subtype="PCM_16")
    with open(out_path[:-4] + ".txt", "w", encoding="utf-8") as f:
        f.write("\n".join(index) + "\n")


def zip_dir(folder, zip_path):
    base = os.path.basename(folder.rstrip("/"))
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for fn in sorted(os.listdir(folder)):
            z.write(os.path.join(folder, fn), f"{base}/{fn}")
    print(f"{zip_path} ({os.path.getsize(zip_path) / 1e6:.1f} MB)")


# ---------------------------------------------------------------------------
def cmd_plan(_args):
    lines, clips, problems = build_plan()
    for p in problems:
        print(f"PROBLEM: {p}")
    chars = sum(len(c.voice) for c in clips.values())
    muted = sum(1 for ln in lines if ln.endswith(" ="))
    print(f"{len(lines)} ini rows, {len(clips)} clips, {muted} muted cues, "
          f"{chars} characters per take")
    return 1 if problems else 0


def cmd_voices(args):
    q = "/v1/shared-voices?page_size=30&gender=male&language=en" \
        "&sort=usage_character_count_1y"
    if args.search:
        q += "&search=" + urllib.request.quote(args.search)
    res = api("GET", q)
    for v in res.get("voices", []):
        print(f"{v.get('name', '?')[:28]:28} {v.get('accent', ''):10} "
              f"{v.get('age', ''):12} {v.get('use_case', ''):18} "
              f"rate={v.get('rate')} {v.get('public_owner_id')} "
              f"{v.get('voice_id')} {v.get('preview_url', '')}")
    mine = api("GET", "/v1/voices")
    print("\nYour voices:")
    for v in mine.get("voices", []):
        labels = v.get("labels") or {}
        print(f"  {v.get('name', '?')[:28]:28} {v.get('category', ''):12} "
              f"{labels.get('gender', '')} {labels.get('accent', '')} "
              f"{v.get('voice_id')}")
    return 0


def cmd_add(args):
    res = api("POST", f"/v1/voices/add/{args.owner}/{args.voice_id}",
              {"new_name": args.name})
    print(res)
    return 0


def cmd_audition(args):
    import numpy as np
    gen._import_audio_deps()
    os.makedirs(args.out, exist_ok=True)
    reel_parts, index = [], []
    for item in args.voices.split(","):
        name, _, vid = item.partition("=")
        name, vid = name.strip(), vid.strip()
        clips = [Clip(f"{slug(name)}_{i:02d}", t, strip_tags(t), "neutral")
                 for i, t in enumerate(AUDITION)]
        raw = os.path.join(args.out, ".raw", slug(name))
        render_all(clips, vid, args.model, raw, 1, False, args.jobs)
        parts = []
        for c in clips:
            p = os.path.join(raw, c.name + ".wav")
            if os.path.exists(p):
                parts += [gen.sf.read(p, dtype="float32")[0],
                          np.zeros(int(0.5 * SR), np.float32)]
        one = np.concatenate(parts)
        gen.sf.write(os.path.join(args.out, f"{slug(name)}.wav"), one, SR,
                     subtype="PCM_16")
        reel_parts += [one, np.zeros(int(1.5 * SR), np.float32)]
        index.append(name)
    gen.sf.write(os.path.join(args.out, "audition_reel.wav"),
                 np.concatenate(reel_parts), SR, subtype="PCM_16")
    with open(os.path.join(args.out, "audition_reel.txt"), "w") as f:
        f.write("\n".join(index) + "\n")
    print(f"audition: {os.path.join(args.out, 'audition_reel.wav')}")
    return 0


def cmd_build(args):
    lines, clips, problems = build_plan()
    if problems:
        for p in problems:
            print(f"PROBLEM: {p}")
        return 1
    raw = os.path.join(args.out, ".raw", args.name)
    unclear = render_all(list(clips.values()), args.voice, args.model, raw,
                         args.takes, not args.no_asr, args.jobs)
    pack = os.path.join(args.out, args.name)
    write_pack(lines, clips, raw, pack, args.title)
    reel(lines, pack, os.path.join(args.out, f"{args.name}_reel.wav"))
    zip_dir(pack, os.path.join(args.out, f"{args.name}.zip"))
    if unclear:
        print(f"{len(unclear)} clips to check by ear (listed above)")
    return 0


def selftest():
    lines, clips, problems = build_plan()
    fails = list(problems)
    keys = set()
    for row in lines:
        key, _, val = row.lstrip(";").partition(" =")
        keys.add(key)
        if key.endswith("_mix") or "{" in val or "[" in val:
            fails.append(f"stitched or dynamic row: {row}")
    # Every spoken row has exactly its own clip; the fillers are the only
    # others, and [Mix] names them.
    for name in clips:
        if name not in FILLERS and f"{name}_wav" not in keys:
            fails.append(f"clip {name} has no _wav row")
    fails += [f"lead-in {n} is not a filler" for n in LEAD_INS
              if n not in FILLERS]
    if not 0 < LEAD_IN_CHANCE <= 100:
        fails.append(f"LEAD_IN_CHANCE {LEAD_IN_CHANCE} is not a percent")
    if len(lines) < 300 or len(clips) < 250:
        fails.append(f"only {len(lines)} rows / {len(clips)} clips parsed")
    for f in fails:
        print(f"SELFTEST FAIL: {f}")
    if fails:
        return 1
    print(f"elevenlabs selftest: {len(lines)} rows, {len(clips)} whole clips, "
          "no line needs a value it cannot say")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Bake a full spotter pack with "
                                 "ElevenLabs (see the file header).")
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("plan")
    v = sub.add_parser("voices")
    v.add_argument("--search", default="")
    a = sub.add_parser("add")
    a.add_argument("owner")
    a.add_argument("voice_id")
    a.add_argument("name")
    for name in ("audition", "build"):
        p = sub.add_parser(name)
        p.add_argument("--model", default="eleven_v4")
        p.add_argument("--out", default="packs_elevenlabs")
        p.add_argument("--jobs", type=int, default=3)
    au = sub.choices["audition"]
    au.add_argument("--voices", required=True, help="Name=voice_id,...")
    b = sub.choices["build"]
    b.add_argument("--voice", required=True)
    b.add_argument("--name", required=True, help="pack folder name")
    b.add_argument("--title", required=True, help="name shown in the picker")
    b.add_argument("--takes", type=int, default=3)
    b.add_argument("--no-asr", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    return {"plan": cmd_plan, "voices": cmd_voices, "add": cmd_add,
            "audition": cmd_audition, "build": cmd_build}.get(
        args.cmd, lambda _a: ap.print_help() or 0)(args)


if __name__ == "__main__":
    sys.exit(main())
