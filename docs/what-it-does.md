# How MXBMRP3 works

MXBMRP3 is a plugin: the game loads it from its `plugins` folder and calls it through PiBoSo's plugin interface, handing it the session, timing, standings and telemetry. The plugin hands back the quads and text that the game draws. Everything else on this page is about how it stays inside that line, and about the few places where it reaches beyond the game, all of them listed below.

## What it never does

- **It never reads race data out of the game's memory and never writes to it.** Race data comes only from what the game hands to plugins.
- **It never patches or hooks the game's code**, and it never loads itself into any other program.
- **It never changes the game's files.** The installer puts the plugin in the game's `plugins` folder. Apart from its own files there, which an update replaces when you click **Install**, everything the plugin writes goes under `Documents\PiBoSo\[Game]\mxbmrp3\`.
- **It never sends input to the game** and has no effect on physics, timing or anything else about how the game plays.
- **It never runs outside the game.** There's no launcher, background service, startup entry or tray app: you start the game as usual, and when you close it the plugin is gone.
- **It never needs an account or a login.** That includes the stream chat, which reads anonymously.
- **It never asks you to pay to use it.** There's no paid tier, no locked features and no ads: every copy has every feature, and the MIT license leaves it free to use, change and share. The thanks links on the About page are optional and unlock nothing.

The source is public and every release is built from it, so all of this can be checked.

The name is older than the plugin: it comes from the very first version, **MXBMRP - MX Bikes Memory Reader Project** (see [About](../README.md#about)). MXBMRP3 is built on the plugin interface, and the name stayed.

## What each part uses

The plugin interface covers race data and drawing. Some features need more than that, such as a mouse to drive the menus, a controller to rumble, or a connection to fetch records. Every one of them is listed here. Most are **off until you turn them on**.

| Feature | What it uses | Goes online | Default |
|---|---|---|---|
| HUDs and widgets | The game's plugin interface: race data in, quads and text out | No | On |
| Settings menu, dragging, hotkeys | Reads mouse and keyboard state from Windows, because the plugin interface has no input. Reads the clipboard when you paste, and opens links in your browser when you click one. It never sends input. | No | On |
| Settings, stats, profiles, logs | Files in `Documents\PiBoSo\[Game]\mxbmrp3\` | No | On |
| Diagnostics in the log | At launch, at the first frame and at shutdown, writes what a graphics problem is diagnosed from: Windows version, CPU, memory, monitors, the graphics card and driver as the game's OpenGL reports them, and the file names of the other DLLs loaded in the game, such as overlays or a ReShade-style `opengl32.dll`. File names only, never paths, and no other program is looked at | No | On |
| Crash files | A Windows crash handler that writes a standard Windows crash dump (`.dmp`) to `crashes\`, then lets the crash continue exactly as it would have | Only a summary, with the usage survey | On |
| Usage survey | An anonymous ping per launch; see [Privacy](../README.md#privacy) for exactly what | Yes | On; the installer asks first, a ZIP install doesn't |
| Update check | Asks GitHub for the latest release. **Install** downloads it, backs up the current version, and the new one loads the next time you start the game. | Yes | Notify; installs only when you click |
| Steam friends | The game's own Steam connection: shows your Steam friends your track, server and session, and reads theirs for the Friends HUD. **Steam** (Settings > General) turns it off. | Through Steam | On (Steam build) |
| Discord | The Discord app on your PC, through its local connection | Through Discord | Off (MX Bikes) |
| Track records | Fetches lap records from CBR or MXB-Ranked for the track and bike category | Yes | Off (MX Bikes) |
| Stream chat | Twitch (anonymous, read-only) or YouTube live chat for your channel | Yes | Off |
| Web overlay | A small web server on this PC (`127.0.0.1:8080`) for OBS or a browser | No; local only unless you change its address | Off |
| Now playing | Windows' media controls: shows the track and sends play/pause/next and volume to your media player; looks up only the player's name | No | Off |
| Spotter voice | Windows text-to-speech, or a voice pack's recordings | No | Off |
| Controller (hotkeys, Gamepad widget, rumble) | Reads your controller through Windows (XInput); vibrates it only when Rumble is on | No | Reading on; rumble and widget off |
| Companion window | A separate window the plugin draws itself, for a second monitor | No | Off |
| Direct GL (experimental) | Draws the HUD into the game's frame itself instead of through the plugin interface's draw calls, and puts back every setting it touches | No | Off |
| Achievements | Your own stats, on your PC | Only tiers reached, with the usage survey | On (MX Bikes) |

For the files the plugin keeps, see [Data Files](../README.md#data-files).

## Common questions

**Windows or my browser warns about the download.** The installer isn't code-signed, so Windows shows "unknown publisher". Every release is built by GitHub Actions from the public source. [Why the warning, and how to proceed](../README.md#windows-warns-me-when-downloading-or-installing).

**Will it change my controls?** No. It never touches the game's control bindings or any of the game's files. It reads your controller only for its own hotkeys, the Gamepad widget and rumble.

**Will it delete anything, and how do I remove it?** It deletes only its own files. The uninstaller (Windows' **Installed apps**) removes the plugin, its `mxbmrp3_data` folder and its registry entry, and leaves your settings unless you tick the option to remove them. A ZIP install comes out by deleting those same files. An update from inside the plugin moves the current version to `plugins\mxbmrp3_update_backup\` before unpacking the new one, and moves it back if unpacking fails; the backup stays there until the next update. If you've ever updated from inside the plugin, also delete `plugins\mxbmrp3_update_backup\`; the uninstaller leaves it. Step by step: [Uninstallation](../README.md#uninstallation).

**Will an update install itself?** No. It tells you a new version exists and installs only when you click **Install**. When GitHub publishes a SHA-256 checksum for the download, it's checked first.

**Is it the cause of my crash?** Every point where the game calls the plugin is guarded, so a plugin error is logged instead of being thrown back at the game. If the game does crash, the crash file names the module that faulted, the plugin included, and the [known-crash list](../crash_analysis/KNOWN_GAME_CRASHES.md) collects the ones that turned out to be in the game itself.

**What does it cost in frame rate?** It does its work inside the game's frame, about the same every frame, and while you ride it writes no files and makes no network calls on the game's thread. The **Performance** HUD shows its own time per frame, and [Getting the most FPS](../README.md#getting-the-most-fps) lists what costs the most.

**Can I make it do something else?** Yes, you're free to change it. Changes that keep to the limits on this page are welcome in MXBMRP3; anything else belongs in a fork under a name of its own. [Building on MXBMRP3](https://github.com/thomas4f/mxbmrp3/blob/main/CONTRIBUTING.md#building-on-mxbmrp3) shows how.
