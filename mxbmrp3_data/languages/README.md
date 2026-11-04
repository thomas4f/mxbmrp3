# Language packs

A language pack translates what the plugin shows: menus, tooltips, HUD labels
and messages. Pick one in **Settings > Appearance > Language**. Anything a pack
does not translate is shown in English, string by string, so a half-finished
pack is still usable.

The default, **System**, uses the pack for Windows' display language: the
folder named after the full code (`fr-ca`) if there is one, else its language
(`fr`), else English. A pack installed later is picked up on its own.

Shipped packs, all Beta (not yet reviewed line by line by a native speaker):
`de` (Deutsch), `es` (Español), `fr` (Français, translated by cross6200),
`it` (Italiano), `nb` (Norsk bokmål), `nl` (Nederlands), `pt` (Português
(Brasil)), `sv` (Svenska) and `tr` (Türkçe).

## What a pack is

A folder named after its language code (`fr`, `sv`, ...) holding:

- `language.ini` - the pack's name as shown in the Language list, written in
  its own language:

  ```ini
  [pack]
  name = Français
  ```

  An optional `base = fr` makes the pack a variant of another one (`fr-ca` over
  `fr`): what it leaves out is answered by its base.
- `strings.po` - the translations, in the standard gettext format.

Your own packs go in `Documents\PiBoSo\<Game>\mxbmrp3\languages\<code>\`,
never in the game's `plugins\mxbmrp3_data\languages\`: the plugin copies them
across at startup, and the Documents copy survives plugin updates.

## Translating

1. Copy `template.pot` from this folder into your own
   `Documents\PiBoSo\<Game>\mxbmrp3\languages\<code>\strings.po`, and add a
   `language.ini` beside it.
2. Open `strings.po` in [Poedit](https://poedit.net) (free). It shows each
   English line next to yours, and any note for translators, such as the most
   characters a box can hold. Any text editor that saves UTF-8 works too: fill
   in each `msgstr ""` under its `msgid`.
3. In game, pick your language and press the **Reload** hotkey (Settings >
   Hotkeys) after saving: the plugin re-reads the pack without restarting.
4. Send the finished `strings.po` and `language.ini` in an issue at
   https://github.com/thomas4f/mxbmrp3/issues.

You don't have to finish before you send it, or before you play with it:
every line left empty stays English.

## What a line must keep to

A line that breaks one of these rules is shown in English instead (a tooltip
that is too long is cut), so a mistake never garbles the screen:

- **Length.** A note such as `max 12 characters` above a line is the most it
  may have: the room before the control after it, or before the edge of its
  column.
- **Tooltips.** Lines marked with the context `tooltip` are the descriptions
  shown under the settings panel. They must fit its box of two lines of about
  55 characters; the plugin cuts anything longer.
- **Codes.** Keep `%d`, `%s` and `{names}` exactly as they are: the plugin
  puts numbers and names there.
- **Letters.** The in-game fonts draw Western European letters
  (Windows-1252: French, Swedish, German, Spanish and similar) plus the
  Turkish letters Ğ ğ İ ı Ş ş.
- **Fuzzy lines.** When the English of a line changes, Poedit marks your
  translation as needing review ("fuzzy"); until you confirm it, the plugin
  shows the new English.

Each time it loads a pack, the plugin's log (`mxbmrp3_log.txt`) has one line
counting how many lines fell back for each of these reasons:

```
I18n: language 'fr': 1520 strings (3 fuzzy, 0 with characters the font cannot draw, 0 whose % codes differ from the English, 2 longer than their room - all shown in English)
```

## Changing a shipped pack

To reword a few lines without taking over the whole pack, make a variant:
a folder with a new code, say `fr-perso`, holding a `language.ini` with its
own `name` and `base = fr`, and a `strings.po` with only the lines you changed. Everything
else comes from the shipped `fr`, including its fixes in later updates. Pick
it by name in the Language list (System only picks a folder named after the
Windows language).

## After a plugin update

An update can add lines or change the English of old ones, and ships a fresh
`template.pot` here. Bring your `strings.po` up to date with Poedit's
**Translation > Update from POT File**, or `msgmerge --update strings.po
template.pot` on the command line: new lines arrive empty, changed ones come
back fuzzy, and your other translations are kept.

## What a pack does not cover

- The web overlay, which is in English.
- The spotter's words, spoken and subtitled: they come from its voice pack,
  which you can reword - see the [Spotter voice](../../docs/spotter.md)
  guide.
- Names that come from the game, such as riders, tracks and bikes.

The `pseudo` folder is a test language for developers, listed only with
developer mode on.
