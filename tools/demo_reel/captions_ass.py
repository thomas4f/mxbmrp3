#!/usr/bin/env python3
"""tools/demo_reel/captions_ass.py - the demo reel's captions as ASS + SRT.

Reads the captions.tsv hover_reel writes (seconds, beat, detail, box; one line
per caption change, a last line with only the end time) and writes:

  * an ASS file make_demo.sh burns in with ffmpeg's `ass` filter (libass), in
    one of two layouts:
      toc      a contents column left of the game frame: every beat listed,
               done ones dimmed with a tick, the current one on a card with an
               accent bar, its detail line and a bar filling as the beat runs.
      caption  one line in the frame itself, in the box the reel kept clear
               (hover_reel's kCaptionBoxes, checked there every frame and
               written into captions.tsv).
  * an SRT with the same text, for players that take subtitles.

Why ASS and not ffmpeg drawtext: drawtext needs one filter per text and time
range (hundreds here) and draws no shapes; ASS carries text, boxes and an
animated bar in one file that libass renders. Nothing off the shelf turns a
beat list into a table of contents, hence this script.

  captions_ass.py captions.tsv out.ass out.srt --layout toc|caption
                  [--frame X,Y,W,H] [--version 1.35]
"""
import argparse
import textwrap

W, H = 1920, 1080
# Colours as ASS &HBBGGRR.
WHITE = "&HFFFFFF&"
TEXT = "&HF5EEE8&"     # rows to come
DIM = "&HB0957F&"      # rows done
DETAIL = "&HEADCD0&"
CARD = "&H422C1C&"
TRACK = "&H6E503A&"
ACCENT = "&H2EA5FF&"   # orange, the reel's cursor colour family
PILL = "&H2A1A10&"


def ts(t):
    t = max(0.0, t)
    cs = int(round(t * 100))
    return f"{cs // 360000}:{cs // 6000 % 60:02d}:{cs // 100 % 60:02d}.{cs % 100:02d}"


def srt_ts(t):
    ms = int(round(max(0.0, t) * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def rect(w, h):
    return f"m 0 0 l {w} 0 {w} {h} 0 {h}"


def read(path):
    rows = []
    # CP1252: the plugin's strings (a language's own name) come through as drawn.
    with open(path, encoding="cp1252") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4:
                continue
            # The box hover_reel checked, as fractions of the frame: x0,y0,x1,y1.
            box = tuple(float(v) for v in parts[3].split(",")) if parts[3] else None
            rows.append((float(parts[0]), parts[1], parts[2], box))
    end = rows[-1][0] if rows and not rows[-1][1] else (rows[-1][0] + 3 if rows else 0)
    segs = [r for r in rows if r[1]]
    out = []
    for i, (t, beat, detail, box) in enumerate(segs):
        t1 = segs[i + 1][0] if i + 1 < len(segs) else end
        if t1 > t:
            out.append((t, t1, beat, detail, box))
    return out, end


def header():
    return f"""[Script Info]
ScriptType: v4.00+
PlayResX: {W}
PlayResY: {H}
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Bold,Tomorrow,26,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,1,1,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Semi,Tomorrow SemiBold,26,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,1,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def ev(t0, t1, style, text, layer=1):
    return f"Dialogue: {layer},{ts(t0)},{ts(t1)},{style},,0,0,0,,{text}\n"


def toc(segs, end, version):
    beats = []
    for s in segs:
        if s[2] not in beats:
            beats.append(s[2])
    span = {b: [min(s[0] for s in segs if s[2] == b), max(s[1] for s in segs if s[2] == b)] for b in beats}
    out = [ev(0, end, "Bold", "{\\an7\\pos(30,92)\\fs44\\c" + WHITE + "}MXBMRP3")]
    if version:
        out.append(ev(0, end, "Semi", "{\\an7\\pos(32,142)\\fs22\\c" + DIM + "}v" + version))
    x0, x1, top, step = 14, 304, 204, 48
    for t0, t1, beat, detail, _ in segs:
        cur = beats.index(beat)
        lines = textwrap.wrap(detail, 22)[:2] or [""]
        y = top
        for i, b in enumerate(beats):
            if i < cur:
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(38,{y + 13})\\p1\\bord0\\c{DIM}}}m 0 6 l 3 3 7 7 15 -1 18 2 7 13 {{\\p0}}"))
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(66,{y})\\c{DIM}}}{b}"))
                y += step
            elif i == cur:
                h = 50 + 27 * len(lines) + 16
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos({x0},{y - 10})\\p1\\bord0\\c{CARD}}}{rect(x1 - x0, h)}{{\\p0}}", 0))
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos({x0},{y - 10})\\p1\\bord0\\c{ACCENT}}}{rect(5, h)}{{\\p0}}", 1))
                out.append(ev(t0, t1, "Bold", f"{{\\an7\\pos(32,{y - 3})\\fs32\\c{WHITE}}}{b}"))
                for k, ln in enumerate(lines):
                    out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(32,{y + 38 + 27 * k})\\fs23\\c{DETAIL}}}{ln}"))
                by = y - 10 + h - 9
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(32,{by})\\p1\\bord0\\c{TRACK}}}{rect(x1 - 14 - 32, 3)}{{\\p0}}", 1))
                b0, b1 = span[beat]
                a = int(round((t0 - b0) * 1000))
                d = int(round((b1 - b0) * 1000))
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(32,{by})\\p1\\bord0\\c{ACCENT}\\fscx0\\t({-a},{d - a},\\fscx100)}}"
                                              f"{rect(x1 - 14 - 32, 3)}{{\\p0}}", 2))
                y += h + 14
            else:
                out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos(66,{y})\\c{TEXT}}}{b}"))
                y += step
    return out


def caption(segs, frame):
    fx, fy, fw, fh = frame
    out = []
    for t0, t1, beat, detail, box in segs:
        bx0, by0, bx1, by1 = box
        cx = fx + fw * (bx0 + bx1) / 2
        cy = fy + fh * (by0 + by1) / 2
        text = f"{beat}: {detail}" if detail else beat
        pw = min(fw * (bx1 - bx0), 30 + 15.5 * len(text))
        ph = fh * (by1 - by0) * 0.8
        fade = "\\fad(150,150)"
        out.append(ev(t0, t1, "Semi", f"{{\\an7\\pos({cx - pw / 2:.0f},{cy - ph / 2:.0f}){fade}\\p1\\bord0\\c{PILL}\\1a&H40&}}"
                                      f"{rect(round(pw), round(ph))}{{\\p0}}", 0))
        out.append(ev(t0, t1, "Semi", f"{{\\an5\\pos({cx:.0f},{cy:.0f}){fade}\\fs28\\c{WHITE}}}{text}"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tsv")
    ap.add_argument("ass")
    ap.add_argument("srt")
    ap.add_argument("--layout", choices=["toc", "caption"], default="toc")
    ap.add_argument("--frame", default="0,0,1920,1080", help="X,Y,W,H of the game frame in the output")
    ap.add_argument("--version", default="")
    a = ap.parse_args()
    segs, end = read(a.tsv)
    frame = [int(v) for v in a.frame.split(",")]
    body = toc(segs, end, a.version) if a.layout == "toc" else caption(segs, frame)
    with open(a.ass, "w", encoding="utf-8") as f:
        f.write(header())
        f.writelines(body)
    with open(a.srt, "w", encoding="utf-8") as f:
        for i, (t0, t1, beat, detail, _) in enumerate(segs, 1):
            f.write(f"{i}\n{srt_ts(t0)} --> {srt_ts(t1)}\n{beat}: {detail}\n\n" if detail else
                    f"{i}\n{srt_ts(t0)} --> {srt_ts(t1)}\n{beat}\n\n")


if __name__ == "__main__":
    main()
