; packaging/lang/English.nsh - every string Setup and the uninstaller show, in
; English. One file per language, included by packaging/mxbmrp3.nsi after its
; MUI_LANGUAGE lines; the other files carry the same keys in the same order.
;
; TRANSLATING: keep each string about as long as the English. The pages are
; laid out in fixed boxes that clip rather than wrap past their height (the
; Privacy page has no spare line at all), so a longer translation silently
; loses its last line. $gameLabel is a game's name, $R1 a list of them, and
; ${PLUGIN_NAME} stays as it is. Menu paths (Settings > General > Integrations)
; use the names the plugin's own language pack shows, so the player can find
; them. MXB_PLUGIN_LANG is the plugin's language folder for this language
; (mxbmrp3_data\languages\<folder>, "en" for English): Setup hands it to the
; plugin, which starts in it on its first launch (core/install_prefs.h).

LangString MXB_WELCOME ${LANG_ENGLISH} "Setup will guide you through the installation of ${PLUGIN_NAME} for PiBoSo racing games.$\n$\n${PLUGIN_NAME} is free and open source.$\n$\nSupported games:$\n  • MX Bikes$\n  • GP Bikes$\n  • Kart Racing Pro$\n$\nSetup will try to find your game installations automatically.$\n$\nClick Next to continue."
LangString MXB_FINISH ${LANG_ENGLISH} "${PLUGIN_NAME} has been installed on your computer.$\n$\nYour settings and data are stored per-game in:$\n  Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
LangString MXB_UN_FINISH ${LANG_ENGLISH} "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nTo remove all settings and data, manually delete:$\n  Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
LangString MXB_UN_FINISH_WIPED ${LANG_ENGLISH} "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nSettings and data for the games you uninstalled have also been removed.$\n$\nClick Finish to close Setup."
LangString MXB_EXIST_TITLE ${LANG_ENGLISH} "Existing ${PLUGIN_NAME} Installation Detected"
LangString MXB_EXIST_SUB ${LANG_ENGLISH} "An existing version was found. Choose how you'd like to proceed."
LangString MXB_EXIST_FOUND ${LANG_ENGLISH} "Existing ${PLUGIN_NAME} installation found for: $R1"
LangString MXB_EXIST_CHOOSE ${LANG_ENGLISH} "Choose your action."
LangString MXB_EXIST_ACTION ${LANG_ENGLISH} "Action"
LangString MXB_EXIST_UPGRADE ${LANG_ENGLISH} "Upgrade — overwrite plugin files, keep settings and data"
LangString MXB_EXIST_FRESH ${LANG_ENGLISH} "Fresh install — overwrite plugin files and reset settings and data"
LangString MXB_EXIST_REMOVE ${LANG_ENGLISH} "Remove (uninstall) ${PLUGIN_NAME}"
LangString MXB_NOTE_KEEP ${LANG_ENGLISH} "Your settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}) will be preserved."
LangString MXB_NOTE_WIPE ${LANG_ENGLISH} "Warning: existing settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}) will be deleted for each selected game."
LangString MXB_NOTE_UNINSTALL ${LANG_ENGLISH} "The uninstaller will let you choose whether to also remove settings and data."
LangString MXB_UNKNOWN ${LANG_ENGLISH} "Unknown"
LangString MXB_PRIV_TITLE ${LANG_ENGLISH} "Privacy"
LangString MXB_PRIV_SUB ${LANG_ENGLISH} "Choose whether to take part in the anonymous usage survey."
LangString MXB_PRIV_BODY ${LANG_ENGLISH} "${PLUGIN_NAME} can send a small anonymous ping each game launch, so the developer can see how many people use it, which features are worth keeping, and what needs fixing.$\n$\nIt sends a random install ID, the plugin version and which game, your enabled features, which achievements you have unlocked, operating system and language, session length, and where the game faulted if it crashed."
LangString MXB_PRIV_NEVER ${LANG_ENGLISH} "It never sends your name, lap times, online activity, server or rider data, or any crash dump. Content you added yourself is never reported by name - at most it counts as $\"custom$\"."
LangString MXB_PRIV_CHECK ${LANG_ENGLISH} "Participate in the anonymous usage survey"
LangString MXB_PRIV_FOOT ${LANG_ENGLISH} "Turning this off doesn't disable anything else; the plugin works exactly the same. You can change it any time in Settings > General > Integrations."
LangString MXB_PRIV_POLICY ${LANG_ENGLISH} "Privacy policy"
LangString MXB_PRIV_REPORT ${LANG_ENGLISH} "See the published usage report"
LangString MXB_GAMES_TITLE ${LANG_ENGLISH} "Select Games"
LangString MXB_GAMES_SUB ${LANG_ENGLISH} "Choose which games to install ${PLUGIN_NAME} for."
LangString MXB_DETECTED ${LANG_ENGLISH} "(Detected)"
LangString MXB_NOT_DETECTED ${LANG_ENGLISH} "(Not detected)"
LangString MXB_SELECT_ONE ${LANG_ENGLISH} "Select at least one game to install."
LangString MXB_SELECT_FOLDER ${LANG_ENGLISH} "Select $gameLabel installation folder"
LangString MXB_SPECIFY_FOLDER ${LANG_ENGLISH} "Please specify the $gameLabel plugins folder."
LangString MXB_NOT_PLUGINS_FOLDER ${LANG_ENGLISH} "This does not appear to be the $gameLabel plugins folder. Continue anyway?"
LangString MXB_FRESH_CONFIRM ${LANG_ENGLISH} "Fresh install will permanently delete all existing ${PLUGIN_NAME} settings and data (profiles, stats, benchmarks, logs, crash dumps) for the selected games before reinstalling.$\n$\nThis cannot be undone. Continue?"
LangString MXB_ADMIN_INSTALL ${LANG_ENGLISH} "Administrator rights are required to install into the selected folder.$\n$\nChoose a folder you can write to, or close Setup and re-run it as administrator."
LangString MXB_DP_INSTALLING ${LANG_ENGLISH} "Installing ${PLUGIN_NAME} for $gameLabel..."
LangString MXB_DP_GAME_DONE ${LANG_ENGLISH} "$gameLabel installation complete."
LangString MXB_DP_FRESH ${LANG_ENGLISH} "Fresh install requested - clearing existing settings and data..."
LangString MXB_DP_REMOVING_DATA ${LANG_ENGLISH} "Removing settings and data:"
LangString MXB_DP_INSTALL_DONE ${LANG_ENGLISH} "Installation complete."
LangString MXB_UN_TITLE ${LANG_ENGLISH} "Select Components to Remove"
LangString MXB_UN_SUB ${LANG_ENGLISH} "Choose which game installations to remove."
LangString MXB_CHANGE_SUB ${LANG_ENGLISH} "Unticking a game removes the plugin from it."
LangString MXB_CHANGE_SELECT_ONE ${LANG_ENGLISH} "To remove ${PLUGIN_NAME} from every game, choose Remove on the previous page."
LangString MXB_UN_DATA_CHECK ${LANG_ENGLISH} "Also delete settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC})"
LangString MXB_UN_SELECT_ONE ${LANG_ENGLISH} "Select at least one game to uninstall."
LangString MXB_UN_SELECT_ONE_MSG ${LANG_ENGLISH} "Please select at least one game to uninstall from."
LangString MXB_UN_DATA_CONFIRM ${LANG_ENGLISH} "This will permanently delete all ${PLUGIN_NAME} settings and data (profiles, stats, benchmarks, logs, crash dumps) for the selected games.$\n$\nThis cannot be undone. Continue?"
LangString MXB_UN_ADMIN ${LANG_ENGLISH} "Administrator rights are required to remove ${PLUGIN_NAME} from the selected folder.$\n$\nClose this and re-run the uninstaller as administrator, or cancel."
LangString MXB_DP_UN_FILES ${LANG_ENGLISH} "Removing ${PLUGIN_NAME} files..."
LangString MXB_DP_UN_FROM ${LANG_ENGLISH} "Removing from $gameLabel..."
LangString MXB_DP_UN_DATA ${LANG_ENGLISH} "Removing ${PLUGIN_NAME} settings and data..."
LangString MXB_DP_UN_DONE ${LANG_ENGLISH} "Uninstallation complete."
LangString MXB_DP_ALL_REMOVED ${LANG_ENGLISH} "All installations removed."
LangString MXB_DP_PARTIAL ${LANG_ENGLISH} "Partial uninstall complete. Some installations remain."
LangString MXB_PLUGIN_LANG ${LANG_ENGLISH} "en"
