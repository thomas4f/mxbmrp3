#!/usr/bin/env python3
"""
tools/check_docs.py - keep the project docs honest, mechanically.

The docs carry rules that the code cannot state about itself, and they are read
before every task. That only works while they are TRUE: a stale rule is worse
than a missing one, because it is followed anyway. Three failure modes are
mechanical, so they are checked here instead of by review:

  1. DANGLING PATH - a doc names a file that was renamed, split or deleted.
     Every path-shaped token in every tracked .md must resolve to something on
     disk (globs and `{h,cpp}` / `.h/.cpp` shorthands are expanded).

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

# The top-level docs, plus every sub-README. The sub-READMEs were originally
# left out, and all of them drifted: tests/integration/README.md still described
# a Makefile build engine two weeks after the CMake migration deleted it, and
# tests/unit/README.md still told readers the plugin "cannot be built on
# Linux/CI" while citing a CLAUDE.md that says the opposite. Nothing caught
# either, because path checking stopped at the root. It no longer does.
DOCS = ["CLAUDE.md", "ARCHITECTURE.md", "TESTING.md", "DEVELOPMENT.md",
        "CONTRIBUTING.md", "README.md", "SECURITY.md",
        "tests/unit/README.md", "tests/integration/README.md",
        "tests/integration/API_COVERAGE.md", "tests/web/README.md",
        "tests/asan/README.md", "analytics/README.md",
        "crash_analysis/README.md", "tools/mxbmrp3_replay/README.md",
        "tools/mxbmrp3_fontgen/README.md", "tools/mxbmrp3_hud_window/README.md"]

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
    for pattern in ("tests/integration/tests/*.cpp", "tests/unit/test_*.cpp"):
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
    # inside an if(), so a column-0 anchor skipped them silently: gcovr, which
    # only unit-coverage requires, was never checked as installable at all. Same
    # failure mode as the tool this check exists to catch, inside the check.
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
    # COMMENT LINES DON'T COUNT. The first cut of this check matched anywhere in
    # the file, so a comment merely *naming* tests/web/run.sh satisfied it — the
    # check passed while CI still never ran that gate. Only executable YAML.
    ci = "\n".join(ln for ln in ci_raw.splitlines()
                   if not ln.lstrip().startswith("#"))

    seen = 0
    lines = cml.splitlines()
    for i, line in enumerate(lines):
        # lstrip for the same reason as the regex above: the indented unit gates
        # were never checked as CI-invoked.
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


def main():
    if "--list-paths" in sys.argv:
        check_paths([], list_only=True)
        return 0

    failures = []
    check_paths(failures)
    check_invariant_labels(failures)
    check_test_catalogue(failures)
    check_gate_catalogue(failures)
    check_budget(failures)
    check_gate_tools_installable(failures)
    check_ci_runs_every_gate(failures)

    if failures:
        print("Documentation check FAILED:\n", file=sys.stderr)
        for f in failures:
            print(f"  * {f}", file=sys.stderr)
        print(f"\n{len(failures)} problem(s).", file=sys.stderr)
        return 1

    sizes = ", ".join(
        f"{doc} {os.path.getsize(os.path.join(REPO, doc)):,}/{budget:,}"
        for doc, budget in DOC_BUDGETS.items())
    print(f"Docs clean: paths resolve, invariants labelled, gate tools installable, "
          f"CI runs every gate, {sizes} bytes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
