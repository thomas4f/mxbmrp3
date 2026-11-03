# Language packs

A language pack translates what the plugin shows: menus, tooltips, HUD labels
and messages. Pick one in **Settings > Appearance > Language**. Anything a pack
does not translate is shown in English, string by string, so a half-finished
pack is still usable.

The default, **System**, uses the pack for Windows' display language: the
folder named after the full code (`fr-ca`) if there is one, else its language
(`fr`), else English. A pack installed later is picked up on its own.

A pack is a folder named after its language code (`fr`, `sv`, ...) holding:

- `language.ini` - the pack's name as shown in the Language list, written in
  its own language:

  ```ini
  [pack]
  name = Français
  ```

  An optional `base = fr` makes the pack a variant of another one (`fr-ca` over
  `fr`): what it leaves out is answered by its base.
- `strings.po` - the translations, in the standard gettext format.

Shipped packs, all Beta (not yet reviewed line by line by a native speaker):
`de` (Deutsch), `es` (Español), `fr` (Français, translated by cross6200),
`pt` (Português (Brasil)) and `sv` (Svenska).

## Translating

1. Copy `template.pot` from this folder into your own
   `Documents\PiBoSo\<Game>\mxbmrp3\languages\<code>\strings.po`, and add a
   `language.ini` beside it. That copy survives plugin updates.
2. Open `strings.po` in [Poedit](https://poedit.net) (free). It shows each
   English line next to yours, and any note for translators, such as the most
   characters a box can hold.
3. In game, pick your language and press the **Reload** hotkey (Settings > Hotkeys) after
   saving: the plugin re-reads the pack without restarting.
4. Send the finished `strings.po` and `language.ini` in an issue at
   https://github.com/thomas4f/mxbmrp3/issues.

A note such as `max 12 characters` above a line is the most it may have: the
room before the control after it, or before the edge of its column. A longer
translation is shown in English, and the log counts how many were.

Lines marked with the context `tooltip` are the descriptions shown under the
settings panel. They must fit its box of two lines of about 50 characters;
the plugin cuts anything longer.

Keep `%d`, `%s` and `{names}` exactly as they are: the plugin puts numbers and
names there. Only Western European letters can be drawn by the in-game fonts
(Windows-1252: French, Swedish, German, Spanish and similar). A line using any
other character is shown in English, and the plugin's log says how many lines
that happened to.

When the English of a line changes, Poedit marks your translation as needing
review ("fuzzy"); until you confirm it, the plugin shows the new English.

The `pseudo` folder is a test language for developers, listed only with
developer mode on.
