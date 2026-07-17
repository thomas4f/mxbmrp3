# Security Policy

## Supported versions

MXBMRP3 ships as a single rolling release. Only the **latest** release
(see the [Releases page](https://github.com/thomas4f/mxbmrp3/releases/latest))
receives fixes. If you hit a security issue, please confirm it against the
latest version first.

## Reporting a vulnerability

Please report security issues **privately** — do not open a public issue for a
suspected vulnerability.

- Preferred: use GitHub's private vulnerability reporting on this repository —
  **Security → Report a vulnerability**
  ([Privately reporting a security vulnerability](https://docs.github.com/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability)).
  This opens a private advisory only the maintainer can see.

When reporting, please include:

- the plugin version (shown in the settings UI / `mxbmrp3_log.txt`) and the game + version,
- what happens and how to reproduce it,
- any relevant log lines from `Documents\PiBoSo\[Game]\mxbmrp3\mxbmrp3_log.txt`, and
- a crash dump from `Documents\PiBoSo\[Game]\mxbmrp3\crashes\` if the issue is a crash.

You'll get an acknowledgement as soon as the maintainer can respond. Please
allow a reasonable window for a fix before any public disclosure.

## Scope notes

MXBMRP3 is a client-side game plugin. Its most relevant attack surfaces are:

- The optional embedded **web-overlay HTTP server** (off by default; when on it
  binds locally for OBS) and the files it reads (settings INI, JSON state,
  user-supplied assets).
- The **auto-updater**. It talks HTTPS-only to `api.github.com` to discover the
  latest release, restricts downloads (including every redirect hop) to
  `github.com` / `*.githubusercontent.com`, and verifies the downloaded zip
  against the SHA256 digest the GitHub release API reports — a digest that is
  present but fails to verify aborts the install. There is no code signing;
  the trust anchor is TLS to GitHub plus the release digest, so a compromise
  of the GitHub account/repository would defeat it. Reports about weakening
  any link in that chain are in scope.
- The **remote analytics sampling config**: a small public JSON
  (`analytics_config.json`) fetched from this repository via
  `raw.githubusercontent.com` — only when analytics is enabled. It can only
  *reduce* the analytics sample rate (reduce-only, fail-open to the built-in
  default) and carries no code or other settings.

Bugs in those areas are in scope. Faults originating in the host game
engine itself are generally out of scope — the plugin's crash handler is
process-global and will capture game-engine crashes that are unrelated to the
plugin.
