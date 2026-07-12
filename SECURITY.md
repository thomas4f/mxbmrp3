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

MXBMRP3 is a client-side game plugin. Its most relevant attack surface is the
optional embedded **web-overlay HTTP server** (off by default; when on it binds
locally for OBS) and the files it reads (settings INI, JSON state, user-supplied
assets). Bugs in those areas are in scope. Faults originating in the host game
engine itself are generally out of scope — the plugin's crash handler is
process-global and will capture game-engine crashes that are unrelated to the
plugin.
