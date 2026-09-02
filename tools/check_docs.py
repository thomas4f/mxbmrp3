#!/usr/bin/env python3
"""
tools/check_docs.py - keep the project docs honest, mechanically.

The docs carry rules that the code cannot state about itself, and they are read
before every task. That only works while they are TRUE: a stale rule is worse
than a missing one, because it is followed anyway. These failure modes are
mechanical, so they are checked here instead of by review (plus the CI/gate
cross-checks at the bottom of the file, which keep `ctest` and the workflow
describing the same suite in BOTH directions):

  1. DANGLING PATH - a doc names a file that was renamed, split or deleted.
     Every path-shaped token in every tracked .md must resolve to something on
     disk (globs and `{h,cpp}` / `.h/.cpp` shorthands are expanded). The same
     holds for a repo-rooted path named in a first-party source COMMENT, which
     is the costlier half: this project asks readers to start at the header
     comment, so a header pointing at a deleted file misdirects every one of
     them. See check_comment_paths for why the comment rule is stricter.

  2. UNBACKED ENFORCEMENT CLAIM - CLAUDE.md's Maintenance Invariants label each
     rule **Enforced** / **Pinned** / **Convention**. An "Enforced" bullet whose
     named check no longer exists silently downgrades to an unchecked rule that
     merely LOOKS checked. Every bullet must carry one of the three labels, and
     an Enforced/Pinned bullet must name a file that exists.

  3. STALE CATALOGUE - TESTING.md's per-test catalogue exists to answer "which
     test covers X?", so a test that isn't in it is invisible. Every test file on
     disk must appear.

  4. REGROWTH - CLAUDE.md and ARCHITECTURE.md each have a byte budget; exceeding
     one means compressing something or moving it next to the code it describes,
     not raising the cap reflexively. (Raise it deliberately when the project
     genuinely grows.) See DOC_BUDGETS for why the two budgets exist for
     different reasons.

Pure stdlib, no network, runs in about a second. Usage:

    python3 tools/check_docs.py [--list-paths]
"""
import glob
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Byte budgets, per doc. Neither is a quality score; both are RATCHETS, and the
# way back under one is to move mechanism detail next to the mechanism (a header
# comment) or bug lore into the regression test — both better homes anyway.
# Raise a budget only as a deliberate decision, never to make a red build green.
#
# The two exist for different reasons, and the difference is the point:
#
#   CLAUDE.md is read IN FULL before every task, so its size is a recurring cost
#   paid on work that never touches the thing being described. That is what
#   justifies a tight ceiling.
#
#   ARCHITECTURE.md is read on demand, so it costs nothing until someone opens
#   it. Its budget is not about context cost — it is about REGROWTH. This file
#   is where prose that lost its argument elsewhere tends to settle, and a 100 KB
#   document nobody can hold in their head is one where a stale claim survives
#   indefinitely (`check_paths` catches a renamed file; nothing catches a
#   sentence that quietly stopped being true). The ceiling makes the next
#   addition a choice rather than an accretion: something goes in, something
#   comes out or moves next to its code.
DOC_BUDGETS = {
    "CLAUDE.md": 48_000,
    "ARCHITECTURE.md": 103_000,
}

# EVERY tracked .md is checked -- the top-level docs and every sub-README -- minus
# the exclusions below. Opt-OUT rather than a hand-kept opt-in list: a new doc is
# covered on the day it lands, and a doc that should NOT be covered has to say why
# here. A sub-README outside the check drifts like any other doc (a build engine
# the migration deleted, a "cannot be built on Linux" that CLAUDE.md contradicts)
# and nothing catches it.
DOCS_EXCLUDED = {
    # Historical by definition: old entries name files that were later renamed
    # or deleted, and rewriting history to satisfy a path check would be a lie.
    "CHANGELOG.md",
    # Upstream licence text, verbatim. Not ours to edit, no repo paths in it.
    "THIRD_PARTY_LICENSES.md",
    # GENERATED wholesale from data the repo does not keep (Aptabase exports,
    # known_game_crashes.json). Their generators own what goes in them.
    "analytics/REPORT.md", "crash_analysis/KNOWN_GAME_CRASHES.md",
}


def tracked_docs():
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=REPO,
                         capture_output=True, text=True).stdout.split()
    return [f for f in out
            if not f.startswith("mxbmrp3/vendor/") and f not in DOCS_EXCLUDED]


DOCS = tracked_docs()

# Directories a bare filename (`map_hud.h`) may be resolved against, so docs can
# name a file without repeating its full path.
SEARCH_ROOTS = ["mxbmrp3", "tests", "tools", "packaging", "mxbmrp3_data", ".github", "analytics"]

# Tokens that are path-SHAPED but aren't paths. Checked as exact matches.
NOT_PATHS = {
    # Wildcards standing in for a family, with no single file behind them.
    "*.md", "*.cpp", "*.h", "*.js", "*.dlo", "*.dli", "*.ttf", "*.fnt", "*.png",
    "*.dmp", "*.log", "*.tape", "*.pdb", "*.zip", "*.json", "*.csv", "*.svg",
    # Game-side / user-machine files that don't live in this repo.
    "proxy64.dlo", "proxy_udp64.dlo", "xinput64.dli", "telemetry64.dlo",
    "mxbmrp3.dlo", "mxbmrp3_gpb.dlo", "mxbmrp3_krp.dlo", "wrsmrp3.dlo",
    "mxbmrp3_log.txt", "mxbmrp3_analytics.json", "settings.ini", "custom.css",
    "mxbmrp3-Setup.exe", "mxbmrp3.zip", "Setup.exe", "callback_fuzzer.exe",
    "version_build.g.h", "mxbmrp3/version_build.g.h", "mxbmrp3_record.dlo", "app.js", "known_game_crashes.json",
    "KNOWN_GAME_CRASHES.md", "REPORT.md",
    # Toolchain-provided, not ours: mingw ships a lowercase xinput.h, which is
    # the whole point of the tests/unit/shim/Xinput.h case shim.
    "xinput.h",
    # Names in tests/unit/README.md's PROPOSED refactor (split the pure
    # formatters out of plugin_utils.cpp). Deliberately do-not-exist-yet: the
    # day they land, drop them from here and the check starts guarding them.
    "format_utils.cpp", "format_utils.h", "test_format_utils.cpp",
    # Deleted ON PURPOSE and named as HISTORY, not as a pointer: the post-mortem
    # for a bespoke tool that a standard one replaced is worth more with the
    # casualty's name in it (CLAUDE.md's "say what you evaluated and why it fell
    # short" cuts both ways). Only the ones a comment cites this way belong here
    # -- a reference that still reads as "go look at this" is stale, not history.
    "tools/coverage_report.py",
}

# A token must look like a path AND not like code to be checked. Deliberately
# CONSERVATIVE: a false positive here trains people to ignore the check, which is
# worse than missing a stale reference. So a token qualifies only if it ends in a
# known source extension (with a real stem before the dot) or in a slash.
CODEY = re.compile(r"[()<>\[\]=;:!?\"'`\s]|::|->|\.\.\.")
# Source-controlled file types: always checked, wherever they're mentioned.
CODE_EXTS = (r"cpp|h|hpp|inc|js|py|sh|md|yml|yaml|css|html|nsi|ps1|sln|vcxproj|filters|\*")
# Data/artifact types: also produced at RUNTIME on a player's machine
# (mxbmrp3_settings.ini, a user's LCD.fnt), so a bare mention isn't a repo
# reference. Only checked when the token spells out a path.
DATA_EXTS = "json|ini|cfg|fnt|ttf|txt|gz|dlo|dli|png|svg|tape"
CODE_PATH = re.compile(rf".*[\w*?\]-]\.({CODE_EXTS})$")
DATA_PATH = re.compile(rf".*/.*[\w*?\]-]\.({DATA_EXTS})$")
# A directory reference needs at least two components: `tests/integration/` is a
# repo path, `plugins/` (game install) and `claude/` (branch prefix) are not.
DIR_PATH = re.compile(r"^[^/]+(/[^/]+)+/$")

# Prefixes that are never repo files: build outputs, URLs, absolute/URL routes.
SKIP_PREFIX = ("build/", "plugins/", "http://", "https://", "mailto:", "#", "/")


def brace_expand(tok):
    """`a/b.{h,cpp}` -> [a/b.h, a/b.cpp]; `a/b.h/.cpp` -> [a/b.h, a/b.cpp]."""
    m = re.match(r"^(.*)\{([^}]*)\}(.*)$", tok)
    if m:
        head, body, tail = m.groups()
        return [f"{head}{part.strip()}{tail}" for part in body.split(",")]
    m = re.match(r"^(.*?)(\.\w+)((?:/\.\w+)+)$", tok)   # foo.h/.cpp
    if m:
        head, first, rest = m.groups()
        return [head + first] + [head + ext for ext in rest.split("/") if ext]
    return [tok]


def resolve(tok, base=""):
    """True if the token names something that exists (glob-aware).

    Docs routinely name a file by a PARTIAL path (`core/http_server.cpp`) or by
    bare filename (`map_hud.h`), which is good for readability, so both are
    resolved by suffix-matching under the known roots as well as from the root.

    `base` is the directory of the doc being checked, so a sub-README's relative
    links (`../CMakeLists.txt`, `../../TESTING.md`) resolve the way a reader
    clicking them would.
    """
    tok = tok.strip("/")
    if not tok:
        return True
    candidates = [os.path.join(REPO, tok)]
    if base:
        candidates.append(os.path.normpath(os.path.join(REPO, base, tok)))
    for root in SEARCH_ROOTS:                        # partial path or bare name
        candidates.append(os.path.join(REPO, root, "**", tok))
    if "/" not in tok:                               # `_widget.h` = a name suffix
        candidates += [os.path.join(REPO, root, "**", "*" + tok) for root in SEARCH_ROOTS]
    for cand in candidates:
        if any(ch in cand for ch in "*?["):
            if glob.glob(cand, recursive=True):
                return True
        elif os.path.exists(cand):
            return True
    return False


def doc_paths(text):
    """Path-shaped backticked tokens, plus relative markdown link targets."""
    out = []
    for tok in re.findall(r"`([^`\n]+)`", text):
        tok = tok.strip().rstrip(".,;")
        if tok in NOT_PATHS or tok.startswith(SKIP_PREFIX) or CODEY.search(tok):
            continue
        if re.search(r"\{[^},]+\}", tok):
            continue                     # `{save_path}/...` placeholder, not a path
        if not (CODE_PATH.match(tok) or DATA_PATH.match(tok) or DIR_PATH.match(tok)):
            continue
        out.append(tok)
    for tok in re.findall(r"\]\(([^)#\s]+)\)", text):
        if not tok.startswith(SKIP_PREFIX):
            out.append(tok)
    return out


def check_paths(failures, list_only=False):
    for doc in DOCS:
        full = os.path.join(REPO, doc)
        if not os.path.exists(full):
            failures.append(f"{doc}: listed in DOCS but missing from the repo")
            continue
        base = os.path.dirname(doc)
        text = open(full, encoding="utf-8").read()
        for tok in sorted(set(doc_paths(text))):
            if list_only:
                print(f"{doc}: {tok}")
            elif not any(resolve(part, base) for part in brace_expand(tok)):
                failures.append(
                    f"{doc}: `{tok}` does not exist "
                    "(renamed, split or deleted — update the doc or drop the reference)")


# Source files whose comments are scanned by check_comment_paths. Shell/Python
# use `#`; everything else uses C-style `//` and `/* */`.
COMMENT_SOURCES = ("*.cpp", "*.h", "*.sh", "*.py", "*.js")
COMMENT_EXCLUDED = ("mxbmrp3/vendor/", "tests/integration/harness/doctest.h")

# A comment reference is only checked when it is ROOTED at a top-level directory
# and names a source-controlled file type. Both halves are load-bearing:
#
#   Rooted, because that is the form a reader can follow. A bare `map_hud.h` in
#   prose is a name, not a pointer. Note this check uses plain existence, NOT
#   check_paths' suffix resolution: that resolution is right for docs, which name
#   files partially on purpose, but it would accept a rooted `tests/<name>.cpp`
#   as the real tests/integration/tests/<name>.cpp -- silently passing the exact
#   wrong-path case this check was written to catch.
#
#   Source types only, because data/asset paths in comments are overwhelmingly
#   patterns and runtime files -- `mxbmrp3_data/textures/standings_hud_1.tga` is
#   an "e.g." for a naming rule, `<savePath>/mxbmrp3/mxbmrp3_analytics.json` is
#   written on a player's machine. Including them was measured: it turned a check
#   with no false positives into one that was mostly false positives, which is
#   the failure mode that teaches people to ignore a gate.
COMMENT_ROOTS = r"(?:mxbmrp3|mxbmrp3_data|tools|tests|packaging|cmake|analytics|docs|\.github)"
# CODE_EXTS minus its `\*` alternative: a glob is a family, not a file to check.
COMMENT_EXTS = "|".join(e for e in CODE_EXTS.split("|") if e != r"\*")
COMMENT_PATH = re.compile(
    rf"\b{COMMENT_ROOTS}/[A-Za-z0-9_./-]*\.(?:{COMMENT_EXTS})\b")


def comment_lines(path):
    """(lineno, text) for every line that is inside a comment."""
    hashy = path.endswith((".sh", ".py"))
    out, in_block = [], False
    with open(os.path.join(REPO, path), encoding="utf-8", errors="replace") as fh:
        for n, raw in enumerate(fh, 1):
            line = raw.strip()
            if hashy:
                if line.startswith("#"):
                    out.append((n, line))
                continue
            if in_block:
                out.append((n, line))
                if "*/" in line:
                    in_block = False
            elif line.startswith("//"):
                out.append((n, line))
            elif line.startswith("/*"):
                out.append((n, line))
                in_block = "*/" not in line
    return out


def check_comment_paths(failures, list_only=False):
    """A repo-rooted path named in a source COMMENT still exists.

    Same failure as a dangling path in a doc, and the more expensive one: the
    reading order this project asks for starts at the header comment, so a
    header pointing at a deleted file sends every new reader somewhere that is
    not there. Found four real ones the day it was written, among them four test
    headers still citing tests/unit/run_tests.sh -- the hand-written unit runner
    the CTest migration deleted.

    WHY BESPOKE (CLAUDE.md: prefer the off-the-shelf tool). Validating references
    inside comments is thoroughly conventional -- rustdoc's broken_intra_doc_links,
    javadoc -Xdoclint:reference, Sphinx's nitpicky mode, Doxygen's unresolved-ref
    warnings -- but every one of them validates references written in ITS OWN
    markup, pointing at entities IT documents. Doxygen is the near-miss: adopting
    it means a Doxyfile, \file blocks across ~340 sources and comments rewritten
    into API-doc form, and it still would not read "See tests/unit/run_tests.sh"
    in a paragraph of prose. Link checkers (lychee, markdown-link-check) extract
    URLs and Markdown link syntax, so a bare relative path in a .cpp is invisible
    to them; Vale lints prose style; semgrep can match a comment but has no
    "exists on disk" predicate; clang-tidy, cppcheck and cpplint have no
    comment-reference checks at all.

    So the honest framing is not "write a tool" but "widen one we already run":
    check_paths above does exactly this for tracked .md and owns the resolver,
    the NOT_PATHS allowlist and the gate registration. This adds the source-file
    input and the stricter rooted-path rule. A standalone script would have been
    the violation.
    """
    tracked = subprocess.run(["git", "ls-files", *COMMENT_SOURCES],
                             cwd=REPO, capture_output=True, text=True).stdout.split()
    scanned = 0
    for src in tracked:
        if src.startswith(COMMENT_EXCLUDED):
            continue
        scanned += 1
        for lineno, text in comment_lines(src):
            for tok in COMMENT_PATH.findall(text):
                if tok in NOT_PATHS:
                    continue
                if list_only:
                    print(f"{src}:{lineno}: {tok}")
                elif not os.path.exists(os.path.join(REPO, tok)):
                    failures.append(
                        f"{src}:{lineno}: comment names `{tok}`, which does not exist "
                        "(renamed, split or deleted - fix the reference, or add it to "
                        "NOT_PATHS if the comment cites it as history)")
    # A scan that matched nothing passes vacuously - the failure mode of every
    # source-scanning gate here (see check_file_budgets.sh's MIN_SCANNED).
    if scanned < 300:
        failures.append(
            f"check_comment_paths scanned only {scanned} sources; the tree has ~670. "
            "The git ls-files patterns are looking at the wrong place.")


def check_invariant_labels(failures):
    """Every Maintenance Invariant bullet is labelled, and a claimed check exists."""
    text = open(os.path.join(REPO, "CLAUDE.md"), encoding="utf-8").read()
    m = re.search(r"^## Maintenance Invariants.*?(?=^## )", text, re.S | re.M)
    if not m:
        failures.append("CLAUDE.md: the Maintenance Invariants section is gone — "
                        "check_docs.py enforces its labelling contract")
        return
    section = m.group(0)
    bullets = [b for b in re.findall(r"^- \*\*.*?(?=^- \*\*|\Z)", section, re.S | re.M)]
    if not bullets:
        failures.append("CLAUDE.md: no invariant bullets found (format changed?)")
        return
    for bullet in bullets:
        title = re.match(r"^- \*\*(.{0,60})", bullet).group(1)
        labels = [lab for lab in ("Enforced", "Pinned", "Convention")
                  if re.search(rf"\*\*{lab}\.?\*\*", bullet)]
        if not labels:
            failures.append(
                f"CLAUDE.md invariant '{title}…': no **Enforced**/**Pinned**/**Convention** "
                "label — say what catches a violation, or admit that nothing does")
            continue
        if "Convention" in labels and len(labels) == 1:
            continue
        # An Enforced/Pinned bullet must name a real file somewhere in it.
        named = [t for t in doc_paths(bullet)
                 if any(resolve(p) for p in brace_expand(t))]
        if not named:
            failures.append(
                f"CLAUDE.md invariant '{title}…': labelled {'/'.join(labels)} but names no "
                "existing check or test file — the claim is unbacked")


def check_test_catalogue(failures):
    """TESTING.md's catalogue is a census, not a sample.

    Its whole job is answering "which test covers X?", which only works if a new
    test lands in it. Both directions matter and both are already checked: a
    listed-but-deleted test trips the dangling-path check above; a written-but-
    unlisted test trips this one.
    """
    doc = open(os.path.join(REPO, "TESTING.md"), encoding="utf-8").read()
    # BOTH unit spellings: a glob for `test_*.cpp` alone leaves every `*_test.cpp`
    # silently outside the census the docstring above claims.
    for pattern in ("tests/integration/tests/*.cpp",
                    "tests/unit/test_*.cpp", "tests/unit/*_test.cpp"):
        for path in sorted(glob.glob(os.path.join(REPO, pattern))):
            name = os.path.basename(path)
            if name not in doc:
                failures.append(
                    f"TESTING.md: `{name}` exists but is not in the catalogue — "
                    "add a row saying what it pins")


def check_gate_catalogue(failures):
    """Every CTest gate must be findable in the docs.

    The catalogue check above censuses TESTING.md against *test files*, so a new
    GATE could land fully undocumented and nothing complained — which is exactly
    what happened with `codeql`: registered in CMakeLists.txt, described in
    DEVELOPMENT.md, absent from TESTING.md (the file CLAUDE.md calls the guide),
    and green the whole time.

    Matching is deliberately loose — the script's basename stem, or the gate
    name, in EITHER TESTING.md or DEVELOPMENT.md. Prose refers to these as
    "run_fuzz / run_perf" as often as by full path, and a check that forces one
    spelling would be a check people work around rather than a check that keeps
    the docs honest. The bar is "a reader can find it", not "cited canonically".
    """
    cml = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    docs = ""
    for name in ("TESTING.md", "DEVELOPMENT.md"):
        docs += open(os.path.join(REPO, name), encoding="utf-8").read()
    pattern = r'^mxb_gate\((\S+)\s+\S+\s+\S+\s+"[^"]*"\s+"(.*?)"\)'
    for match in re.finditer(pattern, cml, re.M | re.S):
        gate, command = match.group(1), match.group(2)
        script = re.search(r'((?:tests|tools)/[\w/.-]+\.(?:sh|py))', command)
        stem = os.path.splitext(os.path.basename(script.group(1)))[0] if script else None
        if (stem and stem in docs) or gate in docs:
            continue
        failures.append(
            f"CTest gate `{gate}` is registered in CMakeLists.txt but appears in "
            "neither TESTING.md nor DEVELOPMENT.md — say what it checks and when "
            "to run it")


def check_budget(failures):
    for doc, budget in DOC_BUDGETS.items():
        size = os.path.getsize(os.path.join(REPO, doc))
        if size > budget:
            failures.append(
                f"{doc} is {size:,} bytes, over its {budget:,}-byte budget by "
                f"{size - budget:,}. Move mechanism detail into a header comment next "
                "to the code, or bug lore into the regression test that pins it — both "
                "are better homes. Raise the budget only as a deliberate call, and "
                "never just to make this check pass.")


def check_named_singletons_exist(failures):
    """Every singleton CLAUDE.md names must still be one.

    The list is deliberately NOT exhaustive -- `grep -l getInstance` is, and a
    hand-kept copy of a thing the code already states is the "don't index the
    tree" mistake CLAUDE.md itself warns about. What a reader cannot grep for is
    which ones matter and why, so that is all the list carries.

    The failure mode worth catching is therefore not incompleteness but a name
    that has been renamed or deleted out from under the prose: a list that is
    short is honest, a list that is WRONG sends the next reader looking for a
    class that is not there. Adding a singleton needs no edit here; renaming or
    removing one does.
    """
    claude = open(os.path.join(REPO, "CLAUDE.md"), encoding="utf-8").read()
    start = claude.find("**Key Singletons**")
    if start < 0:
        failures.append(
            "CLAUDE.md has no '**Key Singletons**' block. If it was renamed, update "
            "check_named_singletons_exist so the names stay checked.")
        return
    block = claude[start:claude.find("\n\n", start)]
    named = set(re.findall(r"^- `([A-Za-z_][A-Za-z0-9_]*)`", block, re.M))

    real = set()
    for root, _dirs, files in os.walk(os.path.join(REPO, "mxbmrp3")):
        if "vendor" in root.split(os.sep):
            continue
        for f in files:
            if not f.endswith((".h", ".cpp")):
                continue
            for m in re.finditer(r"static\s+([A-Za-z_][A-Za-z0-9_]*)\s*&\s*getInstance",
                                 open(os.path.join(root, f), encoding="utf-8",
                                      errors="replace").read()):
                real.add(m.group(1))

    for name in sorted(named - real):
        failures.append(
            f"CLAUDE.md's Key Singletons names `{name}`, which is no longer a "
            "singleton in mxbmrp3/ (no `static X& getInstance`). Renamed, removed, or "
            "demoted -- update the entry or drop it. The list may be incomplete; it "
            "may not be wrong.")


def check_symbol_homes(failures):
    """A doc that says `sym()` lives in `file.cpp` must still be right.

    check_paths already proves the FILE exists, which is why this one was
    needed: `finiteOrZero()` moved to stats_manager_persistence.cpp in a file
    split, both CLAUDE.md and ARCHITECTURE.md kept pointing at
    stats_manager.cpp, and every existing check stayed green because that file
    is still there. A reader following the pointer finds the wrong file and no
    symbol, which is worse than no pointer at all.

    Deliberately narrow: only the "`sym()` ... in `path`" phrasing, which is a
    doc making a checkable claim. Prose that merely mentions a function near a
    filename is not a claim and is left alone.
    """
    # Two phrasings, both a checkable claim:
    #   `sym()` ... in `file.cpp`      -- a function, named as a call
    #   `sym` (file.cpp)               -- a type, table or constant
    # The second exists because a symbol written without parens is invisible
    # to the first pattern.
    patterns = (
        re.compile(
            r"`((?:[A-Za-z_][A-Za-z0-9_]*::)*[A-Za-z_][A-Za-z0-9_]*)\(\)`"
            r"[^.\n]{0,40}?\bin `([a-z_0-9/]+\.(?:h|cpp))`"),
        re.compile(
            r"`([A-Za-z_][A-Za-z0-9_]*)`\s+\(([a-z_0-9/]+\.(?:h|cpp))\)"),
    )
    for doc in ("CLAUDE.md", "ARCHITECTURE.md", "TESTING.md", "DEVELOPMENT.md"):
        full = os.path.join(REPO, doc)
        if not os.path.exists(full):
            continue
        text = open(full, encoding="utf-8").read()
        claims = [(sym, rel, is_call)
                  for is_call, pat in zip((True, False), patterns)
                  for sym, rel in pat.findall(text)]
        for sym, rel, is_call in claims:
            # resolve() answers "does this exist", not "where"; docs name files
            # by partial path, so suffix-match under the repo the way a reader
            # would. No hit is check_paths' problem, not this check's.
            hits = [q for q in glob.glob(os.path.join(REPO, "**", os.path.basename(rel)),
                                         recursive=True)
                    if q.replace(os.sep, "/").endswith("/" + rel) and "/build/" not in
                    q.replace(os.sep, "/")]
            if not hits:
                continue
            body = "".join(open(q, encoding="utf-8", errors="replace").read() for q in hits)
            # A call must appear as one; a table or constant only has to appear.
            bare = re.escape(sym.split("::")[-1])
            probe = r"\b%s\s*\(" % bare if is_call else r"\b%s\b" % bare
            if not re.search(probe, body):
                shown = f"{sym}()" if is_call else sym
                failures.append(
                    f"{doc} says `{shown}` is in `{rel}`, but that file defines no such "
                    "symbol. Moved in a file split? Point at its new home -- a pointer "
                    "to the wrong file is worse than none.")


def check_build_sharing_gates_are_locked(failures):
    """Any gate that drives build.sh must hold the RESOURCE_LOCK.

    They all relink and then load the SAME artifact,
    tests/integration/build/mxbmrp3_test.dlo, so two of them running under
    `ctest -j` corrupt each other. CMakeLists.txt already said "a new gate that
    runs the cross-built DLL belongs in this list" -- and then codeql was added
    and was not, which is what makes this a check rather than a comment.

    It cost a full suite run to find, and the failure does not look like a race:
    85 integration tests failed on host.loaded() with exit=1/reached_init=0,
    which reads like a fuzz finding, while codeql's database came back two
    thirds complete. Comment lines are stripped before matching so a script that
    merely MENTIONS build.sh in its rationale (check_game_configs.sh does) is
    not dragged into the lock.
    """
    cml = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    locked = set()
    m = re.search(r"set_tests_properties\(([^)]*?)PROPERTIES\s+RESOURCE_LOCK\s+\w+\)",
                  cml, re.S)
    if m:
        locked = set(m.group(1).split())

    # Chunk on the call rather than regexing the whole invocation: a gate body is
    # shell, and codeql's contains a ")" inside its own SKIP message, at which a
    # whole-invocation regex stops -- matching no codeql gate and reporting
    # all-clear while the bug this exists for is present.
    chunks = cml.split("mxb_gate(")[1:]
    for chunk in chunks:
        body = chunk.split("\nmxb_gate(")[0].split("\nset_tests_properties")[0]
        name = body.split()[0]
        # Only tests/integration/ scripts share the artifact. tools/fontgen/test.sh
        # also says "build.sh", meaning its OWN, which is why this is scoped by
        # location rather than by the word alone.
        script = re.search(r"\./(tests/integration/[A-Za-z0-9_/.-]+\.sh)", body)
        if not script:
            continue
        full = os.path.join(REPO, script.group(1))
        if not os.path.exists(full):
            continue
        code = "\n".join(line for line in open(full, encoding="utf-8", errors="replace")
                          if not line.lstrip().startswith("#"))
        if "build.sh" in code and name not in locked:
            failures.append(
                f"CMakeLists.txt: gate `{name}` drives build.sh but is not in the "
                "RESOURCE_LOCK list. It shares tests/integration/build/mxbmrp3_test.dlo "
                "with the other cross-build gates, so under `ctest -j` it will relink "
                "that DLL while another gate is loading it.")


def check_readme_toc(failures):
    """README.md's Contents block must list every section, and only real ones.

    It is hand-maintained, which is the same shape as the two lists that had
    already rotted by the time anyone looked (the singleton roster and the docs
    allowlist). It is kept rather than deleted because the README is the
    user-facing front door and is read on mirrors and forums where GitHub's
    generated outline does not exist -- so it earns a check instead.

    The two sections ABOVE the block (Features, Get Started) are deliberately
    not in it: a reader has already passed them.
    """
    readme = open(os.path.join(REPO, "README.md"), encoding="utf-8").read()
    start = readme.find("## Contents")
    if start < 0:
        return
    block = readme[start:readme.index("\n## ", start + 5)]
    listed = re.findall(r"^- \[([^\]]+)\]\(#", block, re.M)
    sections = [h for h in re.findall(r"^## (.+)$", readme, re.M)]
    above = sections[:sections.index("Contents")]

    for name in listed:
        if name not in sections:
            failures.append(
                f"README.md Contents links to \"{name}\", which is not a section. "
                "Renamed or removed -- fix the entry.")
    for name in sections:
        if name in above or name == "Contents" or name in listed:
            continue
        failures.append(
            f"README.md has a \"{name}\" section that its Contents block does not "
            "list. Add it, or move the section above Contents if it is meant to be "
            "read before the list.")


def check_gate_tools_installable(failures):
    """Every binary a CTest gate requires must be installable via install_deps.sh.

    Two lists describing one subject: CMakeLists.txt's mxb_gate TOOLS names the
    BINARIES a gate checks for, tools/install_deps.sh names the PACKAGES that
    provide them (and they are not the same strings — the `wine64` package ships
    no `wine64` binary). Without this check, adding a gate that needs a new tool
    leaves a contributor who ran install_deps.sh with a silent SKIP and no clue
    which package to add.
    """
    cml = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    deps = open(os.path.join(REPO, "tools", "install_deps.sh"),
                encoding="utf-8").read()

    required = set()
    # ^\s* — NOT ^. Three gates (unit / unit-coverage / unit-asan) are indented
    # inside an if(), so a column-0 anchor skips them silently and gcovr, which
    # only unit-coverage requires, is never checked as installable at all.
    for m in re.finditer(r'^\s*mxb_gate\([^\s]+\s+\w+\s+\d+\s+"([^"]*)"', cml, re.M):
        if m.group(1) != "-":
            required.update(m.group(1).split())

    provided = set()
    for row in re.finditer(r'^\s+"[a-z]+\|[^"]*"', deps, re.M):
        provided.update(row.group(0).strip().strip('"').split("|")[3].split())

    if not required or not provided:
        failures.append("check_gate_tools_installable: parsed nothing — did the "
                        "mxb_gate() or install_deps.sh table format change?")
        return

    for tool in sorted(required - provided):
        failures.append(
            f"CMakeLists.txt: a gate requires '{tool}', but no group in "
            f"tools/install_deps.sh lists it under `provides`. Add it there so "
            f"`./tools/install_deps.sh` actually unblocks that gate.")


def check_tooltip_ids_resolve(failures):
    """Every tooltip id the settings UI asks for must exist in the table.

    A control names its tooltip by STRING, and a miss is silent: the row
    renders, the hover does nothing, and no test reads tooltips. That is how
    hotkeys.crash_reset shipped -- the Crashes widget added a hotkey action
    with a tooltip id and no entry beside the thirty other hotkeys that have
    one.

    Both sides are read from the source, so this needs no list of its own:
    the ids come from the settings UI (the only place they are passed) and the
    table from tooltip_manager.h. Tokens that merely look dotted -- headers,
    .lib names -- are excluded by extension rather than by an allowlist, so a
    NEW tooltip namespace is covered the day it appears.
    """
    table = os.path.join(REPO, "mxbmrp3/core/tooltip_manager.h")
    if not os.path.exists(table):
        return
    defined = set(re.findall(r'\{"([a-z0-9_.]+)",\s*"', open(table, encoding="utf-8").read()))
    if not defined:
        failures.append("check_tooltip_ids_resolve: parsed no tooltips from "
                        "tooltip_manager.h. Table reshaped? This check is now blind.")
        return

    NOT_A_TOOLTIP = {"h", "cpp", "hpp", "inc", "lib", "dll", "dlo", "exe", "js",
                     "css", "html", "ini", "json", "tga", "fnt", "wav", "txt",
                     "png", "md", "py", "sh"}
    ui = []
    for root, _dirs, files in os.walk(os.path.join(REPO, "mxbmrp3/hud")):
        for f in files:
            if f.endswith(".cpp") and (f.startswith("settings") or "settings" in root):
                ui.append(os.path.join(root, f))
    missing = {}
    for path in sorted(ui):
        text = open(path, encoding="utf-8").read()
        for tok in re.findall(r'"([a-z][a-z0-9_]*\.[a-z][a-z0-9_]*)"', text):
            if tok.rsplit(".", 1)[1] in NOT_A_TOOLTIP or tok in defined:
                continue
            missing.setdefault(tok, os.path.relpath(path, REPO))
    for tok, where in sorted(missing.items()):
        failures.append(
            f"{where}: tooltip id '{tok}' has no entry in tooltip_manager.h "
            "(the control renders, the hover shows nothing)")


def check_no_legacy_data_filenames(failures):
    """No user-visible text may name a data file the plugin migrated away from.

    The Records tab told users their records live in
    mxbmrp3_personal_bests.json long after StatsManager had folded that file
    into mxbmrp3_stats.json. Nothing caught it: the string is a valid literal,
    the old file still exists on upgraded installs, and no test reads tooltips.

    The migration source is the authority -- it declares the old names as
    OLD_*_FILENAME -- so this needs no list of its own. Naming one anywhere
    except that file, or the changelog recording the migration, is the bug.
    """
    mig = os.path.join(REPO, "mxbmrp3/core/stats_manager_persistence.cpp")
    if not os.path.exists(mig):
        return
    legacy = re.findall(r'OLD_\w*FILENAME\s*=\s*"([^"]+)"',
                        open(mig, encoding="utf-8").read())
    if not legacy:
        failures.append(
            "check_no_legacy_data_filenames: found no OLD_*FILENAME constants in "
            "stats_manager_persistence.cpp. Renamed? This check is now blind.")
        return

    allowed = {"mxbmrp3/core/stats_manager_persistence.cpp", "CHANGELOG.md"}
    tracked = subprocess.run(["git", "ls-files", "*.cpp", "*.h", "*.md", "*.ini"],
                             cwd=REPO, capture_output=True, text=True).stdout.split()
    for rel in tracked:
        if rel in allowed or rel.startswith("mxbmrp3/vendor/"):
            continue
        try:
            text = open(os.path.join(REPO, rel), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for name in legacy:
            for i, line in enumerate(text.splitlines(), 1):
                if name not in line:
                    continue
                # A line that says it is describing the migration is the one
                # legitimate mention -- ARCHITECTURE.md documents that the
                # migration exists. Anything else is telling a user to go look
                # in a file the plugin no longer writes.
                if re.search(r"\b(legacy|migrat)", line, re.I):
                    continue
                failures.append(
                    f"{rel}:{i} names `{name}`, which the plugin migrated away "
                    "from. Point at the current file -- users follow these. If the "
                    "line is describing the migration, say \"legacy\" in it.")


def read_tab_registry(failures, check_name):
    """Parse s_tabRegistry into (global tabs, profile tabs) name lists.

    Returns None -- after appending ONE named failure -- when the anchors are
    gone, rather than letting str.index() raise. A registry rename used to take
    the whole gate down with a ValueError traceback, hiding every other check;
    the file's other checks (check_named_singletons_exist) already degrade to a
    this-check-is-blind message for exactly this case, and the registry has
    moved once already.

    One reader rather than two: check_documented_settings_paths and
    check_readme_menu_tables both need these names, and the duplicated anchor
    literals had to be fixed in both places.
    """
    path = os.path.join(REPO, "mxbmrp3/hud/settings_hud_render.cpp")
    if not os.path.exists(path):
        failures.append(f"{check_name}: settings_hud_render.cpp is gone -- this "
                        "check is now blind. Point it at the registry's new home.")
        return None
    render = open(path, encoding="utf-8").read()
    start = render.find("s_tabRegistry[] = {")
    end = render.find("const SettingsHud::TabDescriptor* SettingsHud::findTabDescriptor")
    if start < 0 or end < 0 or end <= start:
        failures.append(
            f"{check_name}: could not find s_tabRegistry[] (or the "
            "findTabDescriptor definition that bounds it) in "
            "settings_hud_render.cpp -- moved or renamed? This check is now "
            "blind; update its anchors.")
        return None

    rows = re.findall(r'\{\s*(TAB_[A-Z_]+),\s*(?:"([^"]*)"|nullptr)', render[start:end])
    glob, prof, in_profile = [], [], False
    for tab, name in rows:
        if tab == "TAB_SECTION_PROFILE":
            in_profile = True
        elif tab == "TAB_SECTION_GLOBAL":
            pass
        elif in_profile:
            prof.append(name)
        else:
            glob.append(name)
    if not glob or not prof:
        failures.append(
            f"{check_name}: s_tabRegistry parsed to {len(glob)} global and "
            f"{len(prof)} profile tabs -- the row shape changed, so this check "
            "is matching nothing. Update the row regex.")
        return None
    return glob, prof


def check_documented_settings_paths(failures):
    """A doc saying "Settings > X" must name a tab that exists.

    Tab names are what a reader matches against the menu in front of them, and
    a renamed tab leaves every doc that named it quietly wrong.

    Windows' own Settings app shares the phrasing ("Settings > Apps > Installed
    apps"), so those two prefixes are allowed through by name rather than by
    guessing from context.
    """
    registry = read_tab_registry(failures, "check_documented_settings_paths")
    if registry is None:
        return
    glob, prof = registry
    tabs = set(glob) | set(prof)
    windows = {"Apps", "Time"}          # Windows Settings, not ours

    for doc in tracked_docs_all():
        for i, line in enumerate(
                open(os.path.join(REPO, doc), encoding="utf-8").read().splitlines(), 1):
            for m in re.finditer(r"Settings > ([A-Z][a-zA-Z]*(?: [A-Z][a-zA-Z]*)?)", line):
                claimed = m.group(1)
                # docs write "Settings > General and the plugin ..." -- try the
                # longest match first, then its first word.
                if claimed in tabs or claimed.split()[0] in tabs:
                    continue
                if claimed.split()[0] in windows:
                    continue
                failures.append(
                    f"{doc}:{i} says \"Settings > {claimed}\", but there is no such "
                    "tab. Renamed, or Windows' own Settings? Tabs are in s_tabRegistry.")


def tracked_docs_all():
    """Every tracked .md outside vendor -- house-style and link checks want the
    changelog and licence file too, which DOCS deliberately excludes."""
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=REPO,
                         capture_output=True, text=True).stdout.split()
    return [f for f in out if not f.startswith("mxbmrp3/vendor/")]


def check_anchor_links(failures):
    """Every `](#heading)` and `](other.md#heading)` must resolve.

    check_paths proves the FILE exists; nothing proved the fragment did. A
    heading rename breaks every inbound deep link silently -- GitHub renders a
    dead anchor as an ordinary link that just does not move the page, so it is
    invisible until a reader clicks it.

    Slugging follows GitHub's rule closely enough for these docs: strip inline
    HTML, links and backticks, drop punctuation, lowercase, spaces to hyphens.
    """
    docs = tracked_docs_all()
    heads = {}
    for doc in docs:
        found = set()
        for h in re.findall(r"^#{1,6}\s+(.+?)\s*$",
                            open(os.path.join(REPO, doc), encoding="utf-8").read(), re.M):
            h = re.sub(r"<[^>]+>", "", h)
            h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", h).replace("`", "")
            found.add(re.sub(r"[^\w\s-]", "", h).strip().lower().replace(" ", "-"))
        heads[doc] = found

    for doc in docs:
        body = open(os.path.join(REPO, doc), encoding="utf-8").read()
        body = re.sub(r"```.*?```", "", body, flags=re.S)
        for target in re.findall(r"\]\(([^)\s]+)\)", body):
            if target.startswith(("http", "mailto")):
                continue
            path, _, frag = target.partition("#")
            if not frag:
                continue
            ref = doc if not path else os.path.normpath(
                os.path.join(os.path.dirname(doc), path))
            if ref not in heads:
                continue          # not a tracked doc; check_paths owns the file
            if frag.lower() not in heads[ref]:
                failures.append(
                    f"{doc} links to `{target}`, but {ref} has no such heading. "
                    "Renamed? A dead anchor renders as a link that does nothing.")


def check_readme_menu_tables(failures):
    """README's three settings tables must match the code, names and order.

    They are what a user compares against the menu in front of them, so a
    stale one is worse than none: it sends them looking for a row that moved
    or was never there. All three had rotted by the time this was written --
    the Widgets table was missing Pointer, Settings and Version entirely, and
    both it and the HUDs table listed rows in an order the menu does not use.

    Order is checked, not just membership, because the tables exist to be read
    alongside the menu.
    """
    readme = open(os.path.join(REPO, "README.md"), encoding="utf-8").read()

    # find(), not index(): a renamed README heading must report which section
    # went missing, not raise out of the gate. Same rule as read_tab_registry.
    def documented(start, end):
        a, b = readme.find(start), readme.find(end)
        if a < 0 or b < 0 or b <= a:
            failures.append(
                f"check_readme_menu_tables: could not slice README.md between "
                f"{start!r} and {end!r} -- a heading was renamed or removed, so "
                "this table is no longer being checked. Update the anchors.")
            return None
        return re.findall(r"\|\s*\*\*([^*]+)\*\*\s*\|", readme[a:b])

    registry = read_tab_registry(failures, "check_readme_menu_tables")
    if registry is None:
        return
    glob, prof = registry
    prof = [t for t in prof if t != "Widgets"]   # its own table, below
    # The About screen is deliberately NOT in the tabs table: it is absent from
    # the menu's tab list (it opens from the About button, bottom right), so a
    # row for it sends readers hunting for a tab that does not exist. Filtered
    # from the EXPECTED list, the exact-match comparison below turns a re-added
    # About row into an "extra: About" failure -- the row cannot quietly return.
    glob = [t for t in glob if t != "About"]

    widgets_src = open(os.path.join(
        REPO, "mxbmrp3/hud/settings/settings_tab_widgets.cpp"), encoding="utf-8").read()
    widgets = re.findall(r'addWidgetRow\("([^"]+)"', widgets_src)
    if not widgets:
        failures.append(
            "check_readme_menu_tables: found no addWidgetRow calls in "
            "settings_tab_widgets.cpp -- renamed? The Widgets table is no "
            "longer being checked.")
        return

    for label, code, doc, where in (
            ("global settings tabs", glob,
             documented("| Icon | Tab | Description |", "### Profiles"),
             "s_tabRegistry (settings_hud_render.cpp)"),
            ("HUDs", prof, documented("### HUDs", "### Widgets"),
             "s_tabRegistry (settings_hud_render.cpp)"),
            ("Widgets", widgets, documented("### Widgets", "## More Features"),
             "addWidgetRow calls (settings_tab_widgets.cpp)")):
        if doc is None:
            continue          # documented() already reported the bad anchor
        if code == doc:
            continue
        missing = [c for c in code if c not in doc]
        extra = [d for d in doc if d not in code]
        detail = []
        if missing:
            detail.append("missing " + ", ".join(missing))
        if extra:
            detail.append("lists " + ", ".join(extra) + ", which the menu does not")
        if not detail:
            detail.append(f"order differs; the menu is: {', '.join(code)}")
        failures.append(
            f"README.md's {label} table disagrees with {where}: "
            + "; ".join(detail) + ".")


def check_no_em_dashes(failures):
    """No U+2014 in any tracked doc.

    A house style rule, and the kind that rots on its own: it reads as prose
    advice, so it gets followed until someone (or some model) writes a
    paragraph without thinking about it, and then it is quietly false. A
    grep costs nothing and is never out of date.

    En dashes (U+2013) are deliberately NOT flagged. They carry meaning here:
    numeric ranges ("20-200%", "0x80-0x9F") and the Arrange-Act-Assert triad.

    Generated docs are checked like any other, which is what catches an em
    dash added to a generator's source -- it lands in the .md, and the
    census gate keeps the .md in step with the source.

    Runs over every tracked .md, not DOCS: the changelog and the third-party
    licence file are excluded from the other checks for reasons that do not
    apply to house style.
    """
    for doc in tracked_docs_all():
        text = open(os.path.join(REPO, doc), encoding="utf-8").read()
        for i, line in enumerate(text.splitlines(), 1):
            if "\u2014" in line:
                failures.append(
                    f"{doc}:{i} has an em dash. Use a spaced hyphen: "
                    f"{line.strip()[:70]}")


def check_tables_end_at_a_blank_line(failures):
    """A markdown table must be followed by a blank line, not by more text.

    GitHub renders a table that runs straight into an HTML comment; the
    GitHub PAGES site does not, and that is where the public README is read.
    Jekyll parses with kramdown, whose table needs the whole block to be rows -
    the comment is part of that block with no blank line before it, so the
    block is not a table, and it comes out as one paragraph of pipes with the
    separator row's dashes typographed into em dashes. The settings-tab table
    shipped that way in the public README (v1.29.3), correct on github.com and
    broken on the docs site, which is why nobody caught it here.

    A blank line is the whole fix, and it costs nothing on any renderer. This
    is a text rule rather than a render check on purpose: kramdown is a Ruby
    gem, and a gate nobody can run locally is a gate that gets bypassed.
    """
    for doc in tracked_docs_all():
        lines = open(os.path.join(REPO, doc), encoding="utf-8").read().split("\n")
        for i in range(len(lines) - 1):
            row, after = lines[i], lines[i + 1]
            if not row.startswith("|"):
                continue
            if after.strip() and not after.startswith("|"):
                failures.append(
                    f"{doc}:{i + 2} follows a table row with a non-row line, so "
                    f"kramdown (GitHub Pages) renders the whole table as a "
                    f"paragraph. Put a blank line after the last row: "
                    f"{after.strip()[:60]}")


def check_ci_runs_every_gate(failures):
    """Every gate script registered with CTest must also be invoked by CI.

    CI does not run bare `ctest` — it invokes the gate scripts directly, one job
    per area, so each job installs only what it needs and a failure is legible on
    its own. The cost of that structure is that registering a gate in
    CMakeLists.txt does NOT make CI run it, and nothing said so. A gate CI never
    runs is a gate that only fails for whoever happens to run the suite locally.

    A deliberate divergence is declared inline on the mxb_gate() line with
    `# ci-covers-differently: <reason>` (same shape as the `// vis-gate:` and
    `// mt-plain:` annotations elsewhere).
    """
    cml = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    ci_raw = open(os.path.join(REPO, ".github", "workflows", "tests.yml"),
                  encoding="utf-8").read()
    # COMMENT LINES DON'T COUNT. Matching anywhere in the file lets a comment
    # merely *naming* a script satisfy the check while CI still never runs that
    # gate. Only executable YAML.
    ci = "\n".join(ln for ln in ci_raw.splitlines()
                   if not ln.lstrip().startswith("#"))

    seen = 0
    lines = cml.splitlines()
    for i, line in enumerate(lines):
        # lstrip for the same reason as the regex above: the indented unit gates
        # would otherwise never be checked as CI-invoked.
        if not line.lstrip().startswith("mxb_gate("):
            continue
        # The annotation may sit on the gate line or anywhere in the comment
        # block directly above it — a real reason needs more than one line.
        block = [line]
        j = i - 1
        while j >= 0 and lines[j].lstrip().startswith("#"):
            block.append(lines[j])
            j -= 1
        if any("ci-covers-differently:" in b for b in block):
            continue
        for script in re.findall(r"\./[\w/.\-]+\.(?:sh|py)|\btools/[\w.\-]+\.py",
                                 line):
            seen += 1
            bare = script[2:] if script.startswith("./") else script
            if bare not in ci:
                failures.append(
                    f"CMakeLists.txt: gate script {script} is registered with "
                    f"CTest but never invoked in .github/workflows/tests.yml — "
                    f"CI would not run it. Add a step, or annotate the gate line "
                    f"with `# ci-covers-differently: <reason>`.")
    if seen < 10:
        failures.append("check_ci_runs_every_gate: found only %d gate scripts — "
                        "did the mxb_gate() line format change?" % seen)


def check_every_ci_script_is_a_gate(failures):
    """...and the other direction: a script CI runs must be a registered gate.

    check_ci_runs_every_gate above walks gate -> CI. Nothing walked CI -> gate,
    and the gap is not hypothetical: `tools/fontgen/test.sh` and
    `tools/analytics_report.py --selftest` ran in CI for months with no gate, so
    a developer's `ctest` came back green while CI ran two more things. That is
    how 19 -Wunused-function warnings per fontgen build survived — the only
    place they were printed was a CI log nobody had reason to open.

    Scope is tests.yml, matching the forward check. release.yml is deliberately
    out: packaging, SBOM and the MSVC smoke-load have no headless Linux
    equivalent to be a gate of. install_deps.sh is out because provisioning is
    not a check. Anything else declares itself with `# not-a-gate: <reason>` on
    the line or in the comment block above it.
    """
    cml = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    path = os.path.join(REPO, ".github", "workflows", "tests.yml")
    lines = open(path, encoding="utf-8").read().splitlines()

    seen = 0
    for i, line in enumerate(lines):
        if line.lstrip().startswith("#"):
            continue
        for script in re.findall(r"(?:\./)?(?:tests|tools)/[\w/.\-]+\.(?:sh|py)",
                                 line):
            bare = script[2:] if script.startswith("./") else script
            if bare == "tools/install_deps.sh":
                continue
            seen += 1
            block = [line]
            j = i - 1
            while j >= 0 and lines[j].lstrip().startswith("#"):
                block.append(lines[j])
                j -= 1
            if any("not-a-gate:" in b for b in block):
                continue
            if bare in cml:
                continue
            failures.append(
                f"tests.yml runs {bare}, but no mxb_gate() in CMakeLists.txt "
                f"registers it — `ctest` would be green while CI runs more than "
                f"it. Register a gate, or annotate the step with "
                f"`# not-a-gate: <reason>`.")
    if seen < 10:
        failures.append("check_every_ci_script_is_a_gate: found only %d scripts "
                        "in tests.yml — did the workflow's shape change?" % seen)



def check_shipped_theme_keys(failures):
    """Every key a shipped theme ini names -- commented example or not -- must be one
    the applier accepts.

    A theme ini is EDITED BY USERS, and its commented block advertises itself as the
    values in effect. A stale key there is worse than a missing one: uncommenting it
    logs "unknown key" and changes nothing, so the file teaches the wrong vocabulary.
    That is not hypothetical -- `[title] padding-x` survived in bracket.ini after the
    hugging band it configured was removed.

    Checks NAMES, not values: the accepted set is mechanically readable from
    readThemeIni, while the values would need the C++ to answer honestly.

    The set used to include the layout vocabulary too, read from layout_metrics.h's
    key applier. There is no applier any more -- a theme states slices, colours and
    fonts, and nothing else -- so this now enforces that smaller surface, which is
    the point: paste a `[panel] padding-x` into a theme and it fails here rather
    than being logged and ignored at runtime.
    """
    # Read from readThemeIni, not copied here. A hardcoded list would drift exactly
    # the way this check exists to prevent, one level up: rename card.content to
    # card.body there and bracket.ini's live `content = 1` would still be accepted
    # here while the body card silently stopped working.
    # readThemeIni lives in the themes TU.
    assets = os.path.join(REPO, "mxbmrp3", "core", "asset_manager_themes.cpp")
    with open(assets, encoding="utf-8") as f:
        src = f.read()
    accepted = set(re.findall(r'std::strcmp\(key, "([^"]+)"\)', src))
    # The per-family overrides ([card] widget-content and friends) are a TABLE rather
    # than a run of strcmps, because six near-identical branches is what a table is
    # for. Read them too: this check exists so a documented key is a real one, and it
    # would otherwise reject exactly the keys it should be protecting.
    accepted |= set(re.findall(r'\{\s*"([^"]+)",\s*\d+,\s*(?:true|false)\s*\}', src))
    # ...and the box-model key table (kBoxKeys): { "panel.border", &ThemeAsset::...,
    # then that row's FLAGS } -- same table-over-strcmps reasoning as the
    # per-family one. The flag count is deliberately open: the table gains one
    # whenever a per-key property does (border, cardArt, scalar, fracBorder so
    # far), and a pattern pinned to a fixed count silently matches NOTHING the
    # next time -- which reads as "every documented key is unknown", 147 of them
    # at once, and looks like the theme files broke rather than this regex.
    accepted |= set(re.findall(
        r'\{\s*"([^"]+)",\s*&ThemeAsset::\w+'
        r'(?:,\s*(?:true|false))+\s*\}', src))
    # ...plus the two sparse sections whose keys are slot/category names.
    prefixes = ("colors.", "fonts.")

    ini_paths = []
    root = os.path.join(REPO, "mxbmrp3_data", "themes")
    for theme in sorted(os.listdir(root)):
        d = os.path.join(root, theme)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".ini"):
                ini_paths.append(os.path.join(d, name))
    # assets/themes too. That is where debug's ini lives (its slices are not built,
    # so it has no folder under mxbmrp3_data), and the walk covers it.
    masters = os.path.join(REPO, "assets", "themes")
    for name in sorted(os.listdir(masters)):
        if name.endswith(".ini"):
            ini_paths.append(os.path.join(masters, name))
    for path in ini_paths:
        section = ""
        with open(path, encoding="utf-8") as f:
            for lineno, line in enumerate(f, 1):
                s = line.strip()
                # A commented-out SETTING (";key = value"), not prose.
                if s.startswith(";"):
                    s = s[1:].strip()
                    # A commented SECTION HEADER still changes scope: the whole
                    # [colors]/[fonts] block ships commented out, and reading its
                    # keys under whatever real section came last reports ten
                    # colour slots as bad `settings.` keys.
                    # The WHOLE line must be the header. Prose routinely starts
                    # with a bracketed reference ("; [content]; off by default")
                    # and treating that as a scope change silently rescopes every
                    # key after it (bracket.ini's `[card] content` blamed on a
                    # `[content]` section).
                    if not re.fullmatch(r"\[[a-z-]+\]", s):
                        if "=" not in s or " " in s.split("=", 1)[0].strip():
                            continue
                if s.startswith("[") and "]" in s:
                    section = s[1:s.index("]")].strip()
                    continue
                if "=" not in s:
                    continue
                key = s.split("=", 1)[0].strip()
                # An ini key, not prose that happens to contain '=' -- the files
                # are full of "; 0 = sprites carry their own colours".
                if not re.fullmatch(r"[a-z][a-z0-9-]*", key) or not section:
                    continue
                scoped = f"{section}.{key}"
                if scoped in accepted or scoped.startswith(prefixes):
                    continue
                failures.append(
                    f"{os.path.relpath(path, REPO)}:{lineno}: '{scoped}' is not a key the "
                    f"theme applier accepts, so uncommenting it would log 'unknown key' "
                    f"and change nothing. Fix the name or delete the line.")


# The GEOMETRY sections, in the order a theme ini states them. Colours and fonts are
# each theme's own; these are not.
GEOMETRY_SECTIONS = ("panel", "title", "content", "button", "frame", "card")

# The one theme that is allowed to differ, and why: debug is a measuring instrument
# rather than a look. Its values are deliberately larger than a real theme's so each
# band is thick enough to see, and it turns every [card] switch ON -- the shared
# geometry turns the bands off, which would leave it unable to show a band at all.
GEOMETRY_EXEMPT = {"debug"}


def check_shipped_pack_skins(failures):
    """Every shipped pack that declares `base` must actually resolve.

    A skin states only what it changes; discovery answers the rest from its
    base and REJECTS THE PACK WHOLE if the base is missing, is itself a skin,
    or the resolved file set has a hole. That rejection is a log line the user
    never reads -- so a typo here means the shipped Midnight pads or the Carbon
    board silently never appear, and nothing else notices.

    The integration test (pack_skin_test) proves the RULE against staged packs;
    this proves the shipped packs obey it, which is the half a synthetic
    fixture cannot cover.

    Stems come from asset_manager.h, so adding one to a kStems table makes this
    check demand it of every shipped pack rather than going quietly out of date.
    """
    header = open(os.path.join(REPO, "mxbmrp3/core/asset_manager.h"),
                  encoding="utf-8").read()

    def stems_of(namespace):
        m = re.search(namespace + r"\b.*?kStems\[\] = \{(.*?)\};", header, re.S)
        return re.findall(r'"([^"]+)"', m.group(1)) if m else []

    kinds = (("gamepads", stems_of("namespace GamepadSprite")),
             ("pitboards", stems_of("namespace PitboardSprite")))

    for subdir, stems in kinds:
        root = os.path.join(REPO, "mxbmrp3_data", subdir)
        if not os.path.isdir(root):
            continue
        if not stems:
            failures.append(f"check_shipped_pack_skins: could not read the {subdir} "
                            "kStems table from asset_manager.h -- this check is blind.")
            continue

        bases = {}          # pack -> base name ("" = standalone)
        for pack in sorted(os.listdir(root)):
            ini = os.path.join(root, pack, pack + ".ini")
            if not os.path.isdir(os.path.join(root, pack)):
                continue
            if not os.path.exists(ini):
                bases[pack] = ""
                continue
            text = open(ini, encoding="utf-8", errors="replace").read()
            m = re.search(r"^\s*base\s*=\s*(\S+)", text, re.M)
            bases[pack] = m.group(1) if m else ""

        for pack, base in bases.items():
            if not base:
                continue
            if base not in bases:
                failures.append(
                    f"mxbmrp3_data/{subdir}/{pack}: base = {base}, which is not a "
                    "pack here. Discovery skips this pack whole -- it would never "
                    "appear in game.")
                continue
            if bases[base]:
                failures.append(
                    f"mxbmrp3_data/{subdir}/{pack}: base = {base}, but {base} is "
                    "itself a skin. Bases must be baseless (one level only), so "
                    "discovery skips this pack whole.")
                continue
            missing = [st for st in stems
                       if not os.path.exists(os.path.join(root, pack, st + ".tga"))
                       and not os.path.exists(os.path.join(root, base, st + ".tga"))]
            if missing:
                failures.append(
                    f"mxbmrp3_data/{subdir}/{pack}: neither it nor its base {base} "
                    f"provides {', '.join(missing)}.tga -- the resolved set has a "
                    "hole, so discovery skips the pack whole.")


def check_shipped_theme_geometry(failures):
    """Every shipped theme states the SAME box geometry.

    The ten design-language themes differ in art, colour and typeface -- that is what
    they are for -- and share one set of box terms, so a panel measures the same
    whichever is picked and a retune is one decision rather than ten.

    COMPARED AGAINST EACH OTHER, not against a copy kept here. A canonical block in
    this file would be an eleventh place the numbers live, and the first one to go
    stale: it is not the file anybody edits when they retune a theme. The first
    theme in sorted order is the reference purely because something has to be, and a
    disagreement is reported as a pair so the message never implies which is right.
    """
    root = os.path.join(REPO, "mxbmrp3_data", "themes")
    geoms = {}
    for theme in sorted(os.listdir(root)):
        d = os.path.join(root, theme)
        if not os.path.isdir(d) or theme in GEOMETRY_EXEMPT:
            continue
        # theme.ini, or the pre-1.29.2 <name>.ini a third-party theme may still
        # carry (core/pack_ini_path.h): the geometry comparison below cares about
        # the numbers, not which of the two spellings holds them.
        ini = os.path.join(d, "theme.ini")
        if not os.path.isfile(ini):
            ini = os.path.join(d, theme + ".ini")
        if not os.path.isfile(ini):
            failures.append(f"mxbmrp3_data/themes/{theme}/: no theme.ini "
                            f"(see core/pack_ini_path.h)")
            continue
        section, keys = "", {}
        with open(ini, encoding="utf-8") as f:
            for line in f:
                t = line.split(";", 1)[0].strip()
                if t.startswith("[") and "]" in t:
                    section = t[1:t.index("]")].strip()
                elif "=" in t and section in GEOMETRY_SECTIONS:
                    k, v = t.split("=", 1)
                    keys[f"{section}.{k.strip()}"] = " ".join(v.split())
        geoms[theme] = keys

    if len(geoms) < 2:
        return
    ref_name = sorted(geoms)[0]
    ref = geoms[ref_name]
    for theme in sorted(geoms):
        if theme == ref_name:
            continue
        for key in sorted(set(ref) | set(geoms[theme])):
            a, b = ref.get(key), geoms[theme].get(key)
            if a == b:
                continue
            failures.append(
                f"theme geometry differs: [{key}] is "
                f"{'absent' if a is None else repr(a)} in {ref_name} and "
                f"{'absent' if b is None else repr(b)} in {theme}. Shipped themes "
                f"share one geometry -- change both, or say why this one is exempt "
                f"in check_docs.py's GEOMETRY_EXEMPT.")


def main():
    if "--list-paths" in sys.argv:
        check_paths([], list_only=True)
        return 0

    failures = []
    check_paths(failures)
    check_comment_paths(failures)
    check_invariant_labels(failures)
    check_test_catalogue(failures)
    check_gate_catalogue(failures)
    check_shipped_theme_keys(failures)
    check_shipped_theme_geometry(failures)
    check_shipped_pack_skins(failures)
    check_budget(failures)
    check_gate_tools_installable(failures)
    check_named_singletons_exist(failures)
    check_symbol_homes(failures)
    check_build_sharing_gates_are_locked(failures)
    check_readme_toc(failures)
    check_no_legacy_data_filenames(failures)
    check_tooltip_ids_resolve(failures)
    check_documented_settings_paths(failures)
    check_anchor_links(failures)
    check_readme_menu_tables(failures)
    check_no_em_dashes(failures)
    check_tables_end_at_a_blank_line(failures)
    check_ci_runs_every_gate(failures)
    check_every_ci_script_is_a_gate(failures)

    if failures:
        print("Documentation check FAILED:\n", file=sys.stderr)
        for f in failures:
            print(f"  * {f}", file=sys.stderr)
        print(f"\n{len(failures)} problem(s).", file=sys.stderr)
        return 1

    sizes = ", ".join(
        f"{doc} {os.path.getsize(os.path.join(REPO, doc)):,}/{budget:,}"
        for doc, budget in DOC_BUDGETS.items())
    print(f"Docs clean: paths resolve (docs and source comments), invariants labelled, "
          f"named singletons exist, "
          f"symbols where docs say, build-sharing gates locked, gate tools installable, "
          f"shipped themes share one geometry, CI and the gate list agree both "
          f"ways, {sizes} bytes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
