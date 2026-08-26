#!/usr/bin/env python3
"""Report prose that says the same thing in two docs.

    python3 tools/docdup.py                 # every tracked .md
    python3 tools/docdup.py -t 0.45         # widen the net
    python3 tools/docdup.py README.md       # only pairs involving one file

WHY THIS IS NOT jscpd. The obvious off-the-shelf answer is a copy-paste
detector (jscpd supports markdown; PMD's CPD and Simian are the same family).
They were tried first and are the wrong instrument: they match token-identical
blocks, which is what an exact-sentence pass does, and an exact-sentence pass
over these docs returned ZERO while the README and docs/modding.md were stating
three of the same facts in different words. What rots a doc set is a fact
restated, not a paragraph pasted. Nothing off the shelf scores that, so this is
twenty lines of Jaccard over content-word sets, stdlib only.

WHY THIS IS NOT A CI GATE. It has no crisp pass/fail. On the sweep it was
written for it surfaced twelve pairs over 0.55 and six of them were correct as
they stood -- a shipped README that must stand alone, and one rule aimed at
humans in CONTRIBUTING.md and at agents in CLAUDE.md. A gate that is half false
positives gets its threshold raised until it says nothing. Run it when you are
about to write documentation, and judge each pair: the question is never "are
these similar" but "which file owns this fact, and does the other one need more
than a link".

Scoring: Jaccard over the set of 4+ letter words in a sentence, after stripping
code blocks, links and inline code. Sentences under 8 content words are skipped
-- headings and list stubs collide on nothing but shared vocabulary.
"""

import itertools
import re
import subprocess
import sys

THRESHOLD = 0.55
MIN_WORDS = 8


def tracked():
    out = subprocess.run(["git", "ls-files", "*.md"],
                         capture_output=True, text=True).stdout.split()
    return [f for f in out if not f.startswith("mxbmrp3/vendor/")]


def sentences(path):
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"```.*?```", " ", text, flags=re.S)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"`[^`]*`", " ", text)
    out = []
    for chunk in re.split(r"(?<=[.!?])\s+|\n\n", text):
        words = re.findall(r"[a-z]{4,}", chunk.lower())
        if len(words) >= MIN_WORDS:
            out.append((frozenset(words), " ".join(chunk.split())))
    return out


def main(argv):
    threshold = THRESHOLD
    if "-t" in argv:
        i = argv.index("-t")
        threshold = float(argv[i + 1])
        del argv[i:i + 2]
    only = set(argv)

    docs = tracked()
    cache = {d: sentences(d) for d in docs}
    pairs = []
    for a, b in itertools.combinations(docs, 2):
        if only and not (only & {a, b}):
            continue
        for wa, sa in cache[a]:
            for wb, sb in cache[b]:
                score = len(wa & wb) / len(wa | wb)
                if score >= threshold:
                    pairs.append((score, a, sa, b, sb))

    for score, a, sa, b, sb in sorted(pairs, reverse=True):
        print(f"{score:.2f}  {a}\n      {sa[:150]}\n      {b}\n      {sb[:150]}\n")
    print(f"{len(pairs)} pair(s) at or above {threshold:.2f}. "
          "Each needs a judgement, not a fix: decide which file owns the fact.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
