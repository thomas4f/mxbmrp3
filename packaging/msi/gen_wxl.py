"""Generate the MSI's WiX localization files from Setup's NSIS string files.

    gen_wxl.py <packaging/lang dir> <out dir>

ONE SOURCE FOR BOTH INSTALLERS. Every string the MSI shows that is ours (not
WixUI's stock dialog text) is a LangString in packaging/lang/<Language>.nsh,
the same file the NSIS Setup is built from. This turns each into
<out>/<culture>.wxl so a translation fixed there reaches both installers, and
an MSI cannot ship a string the NSIS build has never seen.

The conversion is strict rather than best-effort, because an MSI does not show
an unknown escape as an error -- it shows it to the player:
  - NSIS escapes ($\\n, $\\r, $\\", $$) and the two defines become text.
  - $gameLabel and $R1 become [GAMELABEL] / [EXISTGAMES]; any OTHER NSIS
    variable fails the build.
  - '[' and ']' in the TEXT are escaped ([\\[] / [\\]]): MSI dialog text is
    "Formatted", where [Game] is a property reference and would render as
    nothing ("Documents\\PiBoSo\\\\mxbmrp3").
  - Every string must encode in its culture's ANSI codepage. MSI UI is not
    Unicode; a character outside it renders as '?' with no error anywhere.
  - Every language must define exactly English's keys (makensis enforces the
    same with warning 6040; checked again here so the MSI build stands alone).

Run by build_msi.ps1 on the release runner; plain Python 3, no dependencies.
"""
import os
import re
import sys
from xml.sax.saxutils import escape

PLUGIN_NAME = "MXBMRP3"
PLUGIN_NAME_LC = "mxbmrp3"

# NSIS language file -> (WiX culture, LCID, ANSI codepage). The culture is also
# what selects WixUI's own stock dialog text, so it must be one WixUI ships.
LANGUAGES = {
    "English":      ("en-US", 1033, 1252),
    "German":       ("de-DE", 1031, 1252),
    "Spanish":      ("es-ES", 3082, 1252),
    "French":       ("fr-FR", 1036, 1252),
    "Italian":      ("it-IT", 1040, 1252),
    "Dutch":        ("nl-NL", 1043, 1252),
    "Norwegian":    ("nb-NO", 1044, 1252),
    "PortugueseBR": ("pt-BR", 1046, 1252),
    "Swedish":      ("sv-SE", 1053, 1252),
    "Turkish":      ("tr-TR", 1055, 1254),
}

LINE = re.compile(r'^LangString\s+(\w+)\s+\$\{LANG_\w+\}\s+"(.*)"\s*$')
VARIABLES = {"$gameLabel": "[GAMELABEL]", "$R1": "[EXISTGAMES]"}


def convert(key, raw):
    out = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == "$":
            two = raw[i:i + 3]
            if two in ("$\\n", "$\\r"):
                out.append("\n" if two == "$\\n" else "")
                i += 3
                continue
            if two == '$\\"':
                out.append('"')
                i += 3
                continue
            if raw.startswith("$$", i):
                out.append("$")
                i += 2
                continue
            if raw.startswith("${PLUGIN_NAME_LC}", i):
                out.append(PLUGIN_NAME_LC)
                i += len("${PLUGIN_NAME_LC}")
                continue
            if raw.startswith("${PLUGIN_NAME}", i):
                out.append(PLUGIN_NAME)
                i += len("${PLUGIN_NAME}")
                continue
            for var, prop in VARIABLES.items():
                if raw.startswith(var, i) and not raw[i + len(var):i + len(var) + 1].isalnum():
                    out.append("\x00" + prop + "\x00")   # protected from the [ ] escape
                    i += len(var)
                    break
            else:
                sys.exit("%s: unsupported NSIS escape at %r" % (key, raw[i:i + 16]))
            continue
        out.append(c)
        i += 1
    text = "".join(out)
    parts = text.split("\x00")
    for n in range(0, len(parts), 2):   # even parts are literal text
        parts[n] = (parts[n].replace("[", "\x01").replace("]", "[\\]]")
                    .replace("\x01", "[\\[]"))
    return "".join(parts)


def read(path):
    strings = {}
    with open(path, encoding="utf-8-sig") as fh:
        for line in fh:
            m = LINE.match(line.strip())
            if m:
                strings[m.group(1)] = m.group(2)
    return strings


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    english = read(os.path.join(src, "English.nsh"))
    for name, (culture, lcid, codepage) in LANGUAGES.items():
        strings = read(os.path.join(src, name + ".nsh"))
        if set(strings) != set(english):
            sys.exit("%s.nsh: keys differ from English.nsh: %s"
                     % (name, sorted(set(strings) ^ set(english))))
        rows = []
        for key in english:   # English's order, so the files diff cleanly
            value = convert(key, strings[key])
            try:
                value.encode("cp%d" % codepage)
            except UnicodeEncodeError as e:
                sys.exit("%s.nsh %s: %r is not in codepage %d"
                         % (name, key, value[e.start:e.end], codepage))
            if key == "MXB_NOT_PLUGINS_FOLDER":
                # Read raw by ca/check_folders.cpp (a Property, not formatted
                # text): it splits at the LAST ". " into the per-game statement
                # and the question asked once, and fills [GAMELABEL] itself.
                cut = value.rfind(". ")
                if cut < 0 or "[GAMELABEL]" not in value[:cut]:
                    sys.exit("%s.nsh %s: needs '<statement naming $gameLabel>. <question>'"
                             % (name, key))
                if value.replace("[GAMELABEL]", "").count("[") or "]" in value.replace("[GAMELABEL]", ""):
                    sys.exit("%s.nsh %s: no brackets besides $gameLabel (shown raw)" % (name, key))
            value = escape(value, {'"': "&quot;", "\n": "&#xD;&#xA;"})
            rows.append('  <String Id="%s" Value="%s" />' % (key, value))
        # The package's own language and codepage, read by mxbmrp3.wxs.
        rows.append('  <String Id="MXB_LCID" Value="%d" />' % lcid)
        rows.append('  <String Id="MXB_CODEPAGE" Value="%d" />' % codepage)
        path = os.path.join(out, culture + ".wxl")
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write('<?xml version="1.0" encoding="utf-8"?>\n')
            fh.write("<!-- GENERATED by packaging/msi/gen_wxl.py from packaging/lang/%s.nsh."
                     " Do not edit. -->\n" % name)
            fh.write('<WixLocalization Culture="%s" Codepage="%d" Language="%d"'
                     ' xmlns="http://wixtoolset.org/schemas/v4/wxl">\n' % (culture, codepage, lcid))
            fh.write("\n".join(rows) + "\n</WixLocalization>\n")
        print("%s: %d strings" % (path, len(rows)))


if __name__ == "__main__":
    main()
