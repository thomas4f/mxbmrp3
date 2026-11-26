# MSI installer

`mxbmrp3-Setup.msi`, built with WiX 5 from the same staging tree as the NSIS
`mxbmrp3-Setup.exe` (`packaging/mxbmrp3.nsi`). It exists for its antivirus
record: an unsigned NSIS Setup draws machine-learning detections that an MSI
did not (VirusTotal, Nov 2026). It does what the NSIS Setup does, with the
differences listed below, using standard Windows Installer tables plus one
small read-only DLL of ours, `ca/check_folders.cpp`, with two actions: the
Games page's "not the plugins folder?" check (NSIS's guardrail, which plain
MSI cannot do), and reading an earlier Setup's analytics opt-out so an
upgrade keeps it. The
only other compiled code in the package is WiX's own `RemoveFolderEx` action,
which deletes a folder tree.

| File | What it is |
|---|---|
| `mxbmrp3.wxs` | The package: properties, upgrade rules, the NSIS takeover |
| `game.wxi` | Everything per game, included once for each of the three |
| `ui.wxs`, `game_row.wxi`, `game_next.wxi` | The wizard pages |
| `gen_payload.py` | Staging tree → `payload.wxs` (whole `mxbmrp3_data` per game) |
| `gen_wxl.py` | `packaging/lang/*.nsh` → one `.wxl` per language: one set of strings for both installers |
| `build_msi.ps1` | Builds the ten language variants and folds them into one `.msi` |
| `ca/` | The folder check, a native DLL (MSVC, static CRT), built by `build_msi.ps1` |
| `test_msi.ps1` | Installs, takes over NSIS, uninstalls on real Windows Installer |

It ships beside Setup.exe for a few releases. `release.yml`'s `msi` job
builds and tests it on every run, and the `package` job attaches it to the
Release.

## Same as NSIS Setup

- Finds MX Bikes, GP Bikes and Kart Racing Pro (Steam's uninstall key, then
  Program Files), one box and plugins folder with Browse per game; a picked
  game folder gets `\plugins` appended.
- Privacy page, same text and default; `install_prefs.ini` written with the
  same contract (`core/install_prefs.h`): language always, `analyticsOptOut=1`
  only on opt-out, the stamp, and the file rewritten whole.
- Existing installation page: Upgrade (keep data) or Fresh install (delete
  `Documents\PiBoSo\<game>\mxbmrp3` first, after a confirmation page).
- Uninstall removes the plugin and `mxbmrp3_data` whole; deleting the
  player's settings and data is a separate, confirmed choice, offered only
  when that folder exists. One game can be removed and the others kept.
- The ten languages, from the same string files. The uninstaller speaks the
  language Setup ran in.
- Never downloads anything, never starts the game, never touches player
  files unless asked.

## Takes over an NSIS install

An earlier NSIS install's folders are picked up (they win over detection).
Installing over it replaces the files, removes the NSIS uninstaller from each
plugins folder and removes the NSIS Add/Remove Programs entry, unless that
entry still covers a game this MSI was told to skip. Then the entry stops
listing the games taken over and points at a game NSIS keeps, as NSIS does
after uninstalling one game. Every file is overwritten whatever its version,
so a newer plugin left by a later release cannot stay beside this one's data.

NSIS Setup does not look for an MSI install (it reads only its own entry),
so going back from the MSI to Setup.exe means uninstalling the MSI first
(README → Installation).

## Different from NSIS Setup

- **No language picker.** The MSI opens in the Windows display language
  (English when it is none of the ten) and hands that language to the plugin.
  A picker needs an `.exe` bootstrapper, which is what the MSI avoids.
- **UAC always.** It asks once, at Install. NSIS asks only when the game
  folder needs admin.
- **Add/Remove Programs "Uninstall" shows no pages**, so it always keeps the
  player's data. "Modify" opens the full maintenance wizard (change games,
  remove, delete data).
- **No "Remove" choice on the existing-installation page.** An install
  cannot run another product's uninstaller without a custom action.
- **The "not a plugins folder?" question is a page, not a pop-up**, listing
  every game whose picked folder has no game exe beside it (NSIS asked once
  per game). A folder not named `plugins` gets `\plugins` appended first.
- **Per-user NSIS entries** (HKCU, an NSIS install that needed no admin) are
  removed from the hive of whoever runs the MSI, as NSIS itself does.
- **Also installs `mxbmrp3_data\README.txt`**, the folder's "do not edit"
  note, which the zip ships and NSIS leaves out.
- WixUI's standard artwork, and WixUI's own maintenance welcome and
  Change/Remove pages.

## Command line

Every wizard choice is a public property, so silent installs can make it:

```
msiexec /i mxbmrp3-Setup.msi /qn ANALYTICS=0 MXB_DIR="D:\Games\MX Bikes\plugins\" GPB_SEL=0
msiexec /x mxbmrp3-Setup.msi /qn WIPEDATA=1
```

`ANALYTICS=0`, `FRESH=1`, `WIPEDATA=1`, `PLUGINLANG=<pack folder>`,
`MXB_DIR`/`GPB_DIR`/`KRP_DIR`, `MXB_SEL`/`GPB_SEL`/`KRP_SEL` (1 or 0),
`TRANSFORMS=:<LCID>` to force a language.
