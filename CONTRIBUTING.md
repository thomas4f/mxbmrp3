# Contributing to MXBMRP3

Thanks for your interest! Bug reports, feature requests, and code contributions
are all welcome.

## Reporting bugs & requesting features

Open an issue on [GitHub](https://github.com/thomas4f/mxbmrp3/issues) (the
templates walk you through what to include) or leave a comment on
[MXB-Mods](https://mxb-mods.com/mxbmrp3/). For crashes, attach the `.log`
from `Documents\PiBoSo\[Game]\mxbmrp3\crashes\` and send its matching `.dmp`
privately when asked: a dump can contain your Windows user name, computer
name and recent chat lines. Check
[Known MX Bikes Crashes](crash_analysis/KNOWN_GAME_CRASHES.md) first; your
crash may be a known game-engine bug with no plugin fix possible.

**Security issues**: please do NOT open a public issue - see
[SECURITY.md](SECURITY.md) for private reporting.

## How this repository works

Development happens in a private repository; this public repo receives **one
squashed commit per release** (see `DEVELOPMENT.md` → "Publishing to the public
mirror"). Two practical consequences:

- The public history won't show individual commits or PR merges - a merged
  change lands here as part of the next release's squashed commit.
- Pull requests are still welcome: the maintainer reviews them here and applies
  accepted changes in the private repo. Small, focused PRs are much easier to
  carry across than large ones.

If you're planning something bigger than a small fix, **open an issue first**
to discuss the approach before investing the time.

## Working on the code

- **Start with [AGENTS.md](AGENTS.md)** - the quick-start context (architecture
  sketch, do/don't lists, and the Maintenance Invariants that CI enforces).
  [ARCHITECTURE.md](ARCHITECTURE.md) has the deep version.
- **Building**: the shipping DLL needs Visual Studio 2022 (x64) via CMake
  (`cmake --preset msvc`), but you can build and test everything on Linux - see
  [DEVELOPMENT.md](DEVELOPMENT.md).
- **Tests are expected**: this project has a real, CI-gated suite that runs
  headless (no game needed). When you change behavior, add or extend a test -
  [TESTING.md](TESTING.md) shows which layer it goes in and how to add one.
  Before submitting, run at least:

  ```
  cmake -S . -B build/tests && ctest --test-dir build/tests -L fast
  ./tests/integration/build.sh && ./tests/integration/run_tests.sh
  ```

- **Changelog**: a user-visible change gets a line under `## [Unreleased]` in
  [CHANGELOG.md](CHANGELOG.md). Internal refactors, test and tooling work do not.
- **Docs**: prefer putting an explanation where it can't drift out of sight - a
  header comment beside the mechanism, or the regression test that pins the bug.
  The markdown carries the rules that no single file can state. `python3
  tools/check_docs.py` (also in CI) fails on a doc path that no longer exists, on
  a Maintenance Invariant claiming an enforcement it doesn't have, on a test
  missing from TESTING.md's catalogue, and on AGENTS.md, TESTING.md or
  ARCHITECTURE.md outgrowing its budget.
- **Style**: match the surrounding code - layout (wrapping, comment alignment)
  is a review concern, not a formatter's, and
  `tests/integration/check_style.sh`'s header records why clang-format was
  measured and rejected. What *is* mechanical - no tabs, no trailing
  whitespace, LF endings, a final newline - lives in `.editorconfig` and is
  enforced by that script in CI. Commit messages use imperative verbs
  and say what actually changed ("Fix stale gap cache on raceNum reuse", not
  "Fix bug").

## Building on MXBMRP3

MXBMRP3 is MIT licensed: you're free to fork it, change it, ship it and
build on it. You don't need to ask. If you do, there are three good routes:

1. **Contribute here**, if you agree with the project's direction and its
   limits below. Open an issue first for anything bigger than a small fix.
2. **Make your own fork under its own name.** Do whatever you want with it.
3. **Build something new** and use this code as a reference.

What we ask, so players can tell the projects apart:

- **Keep the license.** Leave `LICENSE` and the copyright notice in place
  (MIT requires this) and say your project is based on MXBMRP3.
- **Use a different name.** The license covers the code, not the name. Don't
  ship under "MXBMRP3" or a name close to it, and don't present your build as
  official or as an MXBMRP3 release.
- **Give it its own identity on disk**, so it never overwrites or updates a
  real install: your own DLL names (`mxbmrp3/CMakeLists.txt`), `Documents`
  folders (`mxbmrp3/core/plugin_names.h`), installer name, publisher,
  registry and uninstall keys (`packaging/mxbmrp3.nsi`), and update check and
  analytics (`mxbmrp3/core/plugin_constants.h`).

Why the name matters: it comes with a few limits. It works only through the
game's plugin interface; it never reads race data out of the game's memory
and never writes to it, never patches or hooks the game, never changes game
files, and has no effect on physics ([how it works](docs/what-it-does.md)).
It is free, with no paid tier, no locked features and no ads. A fork may
choose differently, and that is fine, under its own name.

If you use an AI assistant to work on a fork, point it at this section.

## License

By contributing you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
