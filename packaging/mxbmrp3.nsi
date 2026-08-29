; mxbmrp3.nsi
; Builds a 64-bit installer via `Target AMD64-Unicode`, which official NSIS has
; supported since 3.07 — stock makensis >= 3.07 is all this needs (see release.yml).

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"

!define PLUGIN_NAME "MXBMRP3"
!define PLUGIN_NAME_LC "mxbmrp3"
!define PLUGIN_PUBLISHER "thomas4f"
; Project home page. Also the privacy policy's home (README #privacy anchor).
!define PLUGIN_URL "https://github.com/thomas4f/MXBMRP3"
; The staging tree (built .dlo + mxbmrp3_data) that File commands pull from. NSIS
; resolves relative File paths relative to THIS script's directory, so callers pass
; an ABSOLUTE path (make_release.bat / release.yml) — this default only covers a
; manual `makensis packaging\mxbmrp3.nsi` run from the repo root.
!ifndef PLUGIN_SOURCE_PATH
  !define PLUGIN_SOURCE_PATH "..\dist\staging"
!endif

; Game definitions
!define MXBIKES_STEAM_APPID "655500"
!define MXBIKES_EXE "mxbikes.exe"
!define MXBIKES_DLO "mxbmrp3.dlo"

!define GPBIKES_STEAM_APPID "848050"
!define GPBIKES_EXE "gpbikes.exe"
!define GPBIKES_DLO "mxbmrp3_gpb.dlo"

!define KRP_STEAM_APPID "415600"
!define KRP_EXE "kart.exe"
!define KRP_DLO "mxbmrp3_krp.dlo"

; Per-game save-path folder names under Documents\PiBoSo\<Game>\mxbmrp3.
; This is where the plugin writes settings, stats, logs, crashes and benchmarks.
; The game's save path is user-configurable, so this only covers the default
; location — the installer probes for it and only offers removal when it exists.
!define MXBIKES_DOCS_FOLDER "MX Bikes"
!define GPBIKES_DOCS_FOLDER "GP Bikes"
!define KRP_DOCS_FOLDER "Kart Racing Pro"
!ifndef PLUGIN_VERSION
  !define PLUGIN_VERSION 1.0.0.0
  ;!error "PLUGIN_VERSION is not defined. Please define it before building."
!endif
!ifndef OUTPUT_DIR
  ; Also script-dir-relative; callers pass an absolute path. `..\dist` is the
  ; manual-run default now that this script lives in packaging\.
  !define OUTPUT_DIR "..\dist"
!endif

!define REG_UNINSTALL_KEY_PATH "Software\Microsoft\Windows\CurrentVersion\Uninstall"

; Resolve $userDocuments to the Documents folder of the person who LAUNCHED Setup, not
; the (possibly different) admin account UAC elevated into. When a standard user triggers
; UAC and supplies separate admin credentials, the elevated process runs as that admin, so
; the built-in $DOCUMENTS points at the admin's profile — the wrong place. We instead read
; the interactive user's own (currently loaded) registry hive:
;   HKLM ...\Authentication\LogonUI\LastLoggedOnUserSID  -> the interactive user's SID
;   HKU  <SID>\...\Explorer\Shell Folders\Personal       -> their (already-expanded) Documents
; The Shell Folders value is the fully-resolved absolute path (honoring OneDrive/known-folder
; redirection exactly like the game does), so it needs no %USERPROFILE% expansion — which
; would wrongly expand to the admin under elevation. Falls back to $DOCUMENTS if either read
; fails (e.g. same-account elevation, older Windows, or no interactive session).
!macro RESOLVE_USER_DOCUMENTS
  StrCpy $userDocuments "$DOCUMENTS"
  SetRegView 64
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\LogonUI" "LastLoggedOnUserSID"
  ${If} $0 != ""
    ReadRegStr $1 HKU "$0\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders" "Personal"
    ${If} $1 != ""
      StrCpy $userDocuments "$1"
    ${EndIf}
  ${EndIf}
!macroend

; Remove a game's plugin settings/data folder (<Documents>\PiBoSo\<Game>\mxbmrp3) if it
; exists. Used by the fresh-install path and the "remove all data" uninstall option.
; Relative jumps (not labels) so the macro can be expanded more than once. Works in both
; the installer and uninstaller — $userDocuments, IfFileExists, RMDir and DetailPrint are
; all valid in each. $userDocuments must have been resolved (RESOLVE_USER_DOCUMENTS) first.
!macro REMOVE_USER_DATA DocsFolder
  IfFileExists "$userDocuments\PiBoSo\${DocsFolder}\${PLUGIN_NAME_LC}\*.*" 0 +3
    DetailPrint "Removing settings and data: $userDocuments\PiBoSo\${DocsFolder}\${PLUGIN_NAME_LC}"
    RMDir /r "$userDocuments\PiBoSo\${DocsFolder}\${PLUGIN_NAME_LC}"
!macroend

; Install the plugin DLO + the whole mxbmrp3_data payload into one game's plugins
; folder. ONE definition for all three games — the per-game sections used to carry
; three hand-duplicated copies of this file list, so a new data subfolder (or a new
; file extension in an existing one) silently shipped to fewer than all games.
; Subfolder globs are *.* on purpose: the build workspace only contains files that
; are meant to ship, and extension-globs (e.g. fonts\*.ttf) are exactly the drift
; trap this macro removes. When ADDING a new mxbmrp3_data subfolder: add it here
; (once), plus sw.js PRECACHE_URLS / AssetManager::syncUserAssets per CLAUDE.md.
;
; Setup writes files and never deletes any. It briefly pruned each pack's
; pre-rename <name>.ini here, which meant re-deriving PackIni::resolve's rules in
; NSIS -- and getting them wrong, by deleting the canonical ini of a pack named
; after its own type. The plugin decides instead: it warns about a shadowed ini
; only when the user's own asset tree has a copy, so a leftover Setup wrote is
; silent without anything having to remove it.

!macro INSTALL_GAME_FILES InstallPath DloFile GameName
  DetailPrint "Installing ${PLUGIN_NAME} for ${GameName}..."

  ; Plugin DLO
  SetOutPath "${InstallPath}"
  File "${PLUGIN_SOURCE_PATH}\${DloFile}"

  ; Licence notices. Required, not courtesy: everything below carries assets whose
  ; licences make the notice a CONDITION of redistributing a copy -- the OFL-1.1
  ; font families under fonts\ and web\fonts\ (OFL section 2), and Gamepad Viewer's
  ; MIT-licensed artwork under gamepads\. The zip has always carried both files at
  ; its root; the installer carried neither, so the recommended install path was
  ; the one shipping the fonts and the art with no notice attached.
  ;
  ; Inside mxbmrp3_data\ rather than beside the DLO, for two reasons: the notice
  ; then sits with the payload it covers, and the uninstaller's existing
  ; `RMDir /r mxbmrp3_data` takes it away again -- a file next to the DLO would
  ; need its own Delete in all three per-game blocks, which is exactly the kind of
  ; hand-duplicated list this macro exists to remove.
  ; Setup's analytics choice, for a plugin whose settings file does not exist yet.
  ; WRITTEN ONLY ON OPT-OUT, and deleted otherwise: the file can say "off" or say
  ; nothing, never "on". Setup cannot read the per-game settings (their location
  ; depends on the game's configurable save path), so it must not be able to
  ; overrule an in-game opt-out on upgrade. See core/install_prefs.h.
  SetOutPath "${InstallPath}\mxbmrp3_data"
  ${If} $analyticsOptOut == "1"
    ; The error flag is GLOBAL and STICKY -- a failed RMDir /r in the fresh-install
    ; wipe above leaves it set, and the IfNot below would then read that instead of
    ; this FileOpen and skip the write in silence. Every other ${Errors} site in
    ; this script clears first; this one did not.
    ClearErrors
    FileOpen $9 "${InstallPath}\mxbmrp3_data\install_prefs.ini" w
    ${IfNot} ${Errors}
      FileWrite $9 "; Written by ${PLUGIN_NAME} Setup from the choice made on its Privacy page.$\r$\n"
      FileWrite $9 "; Honoured ONCE by the plugin, then ignored - the in-game toggle$\r$\n"
      FileWrite $9 "; (Settings > General > Integrations) is authoritative afterwards.$\r$\n"
      FileWrite $9 "[Install]$\r$\n"
      FileWrite $9 "analyticsOptOut=1$\r$\n"
      FileWrite $9 "stamp=${PLUGIN_VERSION}$\r$\n"
      FileClose $9
    ${EndIf}
  ${Else}
    ; Left ticked: clear any marker a previous Setup wrote, so a stale file cannot
    ; re-apply against a newer version's stamp.
    Delete "${InstallPath}\mxbmrp3_data\install_prefs.ini"
  ${EndIf}
  File "${PLUGIN_SOURCE_PATH}\LICENSE"
  File "${PLUGIN_SOURCE_PATH}\THIRD_PARTY_LICENSES.md"

  ; Fonts
  SetOutPath "${InstallPath}\mxbmrp3_data\fonts"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\fonts\*.fnt"

  ; Textures
  SetOutPath "${InstallPath}\mxbmrp3_data\textures"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\textures\*.tga"

  ; Icons
  SetOutPath "${InstallPath}\mxbmrp3_data\icons"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\icons\*.tga"

  ; Panel themes (one folder per theme: corner/edge/center .tga + theme.ini, plus
  ; an optional icons\ subfolder overriding icons from the set above).
  ; /r because each theme is its own subdirectory -- unlike the flat asset folders
  ; above, the set of directory names is not known here -- and it carries the icon
  ; subfolder with it.
  ; The DEBUG theme is not here to exclude: it is not BUILT. Its master and ini
  ; live in assets/themes/, and a skinner cuts the slices with tools/themeslice
  ; when they want it -- so nothing downstream has to remember to leave it out.
  SetOutPath "${InstallPath}\mxbmrp3_data\themes"
  File /r "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\themes\*.*"

  ; Gamepad packs (one folder per pad: its .tga set + gamepad.ini placing the buttons
  ; on the artwork). /r for the same reason as themes -- each pack is its own
  ; subdirectory and the set of names is not known here.
  SetOutPath "${InstallPath}\mxbmrp3_data\gamepads"
  File /r "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\gamepads\*.*"

  ; Pit board packs (one folder per board: background.tga + pitboard.ini placing the
  ; rows on it). /r for the same reason as themes and gamepads.
  SetOutPath "${InstallPath}\mxbmrp3_data\pitboards"
  File /r "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\pitboards\*.*"

  ; Spotter voice packs. Only the TEXT-ONLY `default` pack is bundled -- it is
  ; a few KB of phrases spoken by Windows TTS, and it ships so the spotter's
  ; wording is a file people can read and edit. Packs with RECORDED audio are
  ; a separate download (tens of MB of wav for a feature that is off until you
  ; turn it on), baked by tools/spottergen and extracted here or into
  ; the user's own Documents folder. /r for the same reason as themes,
  ; gamepads and pitboards -- each pack is its own subdirectory.
  SetOutPath "${InstallPath}\mxbmrp3_data\spotters"
  File /r "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\spotters\*.*"

  ; Gauges packs (one folder per set: tacho.tga + speedo.tga + gauge.ini stating
  ; what each face READS and where its needle sweeps). /r for the same reason as
  ; every other pack type -- each set is its own subdirectory.
  SetOutPath "${InstallPath}\mxbmrp3_data\gauges"
  File /r "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\gauges\*.*"

  ; Web overlay files (root files + each asset subfolder)
  SetOutPath "${InstallPath}\mxbmrp3_data\web"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\*.*"
  SetOutPath "${InstallPath}\mxbmrp3_data\web\js"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\js\*.*"
  SetOutPath "${InstallPath}\mxbmrp3_data\web\fonts"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\fonts\*.*"
  SetOutPath "${InstallPath}\mxbmrp3_data\web\icons"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\icons\*.*"

  ; Web overlay logos (optional - the folder may legitimately be empty)
  SetOutPath "${InstallPath}\mxbmrp3_data\web\logos"
  File /nonfatal "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\logos\*.*"

  DetailPrint "${GameName} installation complete."
!macroend

; ---------------------------------------------------------------------------
; On-demand elevation helpers
;
; The installer/uninstaller run un-elevated (RequestExecutionLevel user) and only
; relaunch themselves elevated when the chosen game folder actually needs admin (e.g.
; a default Steam install under Program Files). When the game lives somewhere the user
; can already write (a Steam library on another drive, a portable install), nothing
; elevates and no UAC prompt appears. Uninstall keys go to HKLM when we have admin and
; HKCU otherwise, so the un-elevated path can still register in Add/Remove Programs.
; ---------------------------------------------------------------------------

; OutVar = "1" if we can write the machine-wide (HKLM) registry, i.e. we have admin.
; Functional test — the only thing that actually matters is whether the HKLM uninstall
; key can be written, so probe it directly instead of inspecting the token.
!macro TEST_MACHINE_REG_WRITABLE OutVar
  ClearErrors
  WriteRegStr HKLM64 "Software\${PLUGIN_NAME}" "_wtest" "1"
  ${If} ${Errors}
    StrCpy ${OutVar} "0"
  ${Else}
    DeleteRegValue HKLM64 "Software\${PLUGIN_NAME}" "_wtest"
    DeleteRegKey /ifempty HKLM64 "Software\${PLUGIN_NAME}"
    StrCpy ${OutVar} "1"
  ${EndIf}
!macroend

; OutVar = "1" if Folder can be created/written without elevation. Creates the folder if
; missing (harmless if it exists) then round-trips a temp file. Uses $R9 as scratch.
!macro TEST_FOLDER_WRITABLE Folder OutVar
  ClearErrors
  CreateDirectory "${Folder}"
  FileOpen $R9 "${Folder}\.mxbmrp3_wtest.tmp" w
  ${If} ${Errors}
    StrCpy ${OutVar} "0"
  ${Else}
    FileClose $R9
    Delete "${Folder}\.mxbmrp3_wtest.tmp"
    StrCpy ${OutVar} "1"
  ${EndIf}
!macroend

; Write the Add/Remove Programs uninstall keys under the given root (HKLM64 with admin,
; else HKCU64). Same key layout for both so the uninstaller can read either. INSTDIR and
; the per-game selection/paths must already be set.
!macro WRITE_UNINSTALL_REG ROOT
  WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayName" "${PLUGIN_NAME}"
  WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
  WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$INSTDIR"
  WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "Publisher" "${PLUGIN_PUBLISHER}"
  WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayVersion" "${PLUGIN_VERSION}"
  WriteRegDWORD ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoModify" 1
  WriteRegDWORD ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoRepair" 1
  ${If} $isMXBikesSelected == "1"
    WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath" "$MXBikesInstallPath"
  ${EndIf}
  ${If} $isGPBikesSelected == "1"
    WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath" "$GPBikesInstallPath"
  ${EndIf}
  ${If} $isKRPSelected == "1"
    WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath" "$KRPInstallPath"
  ${EndIf}
!macroend

; Uninstaller: after removing selected games, either delete the whole key (nothing left) or
; repoint InstallLocation/UninstallString at a surviving game. Operates on the hive that
; actually holds the keys. Uses $R0/$R1/$R2 as scratch.
!macro UN_FINALIZE_REG ROOT
  ReadRegStr $R0 ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
  ReadRegStr $R1 ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
  ReadRegStr $R2 ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
  ${If} $R0 == ""
  ${AndIf} $R1 == ""
  ${AndIf} $R2 == ""
    DeleteRegKey ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}"
    DetailPrint "All installations removed."
  ${Else}
    ${If} $R0 != ""
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$R0"
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$R0\${PLUGIN_NAME_LC}_uninstall.exe"
    ${ElseIf} $R1 != ""
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$R1"
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$R1\${PLUGIN_NAME_LC}_uninstall.exe"
    ${Else}
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$R2"
      WriteRegStr ${ROOT} "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$R2\${PLUGIN_NAME_LC}_uninstall.exe"
    ${EndIf}
    DetailPrint "Partial uninstall complete. Some installations remain."
  ${EndIf}
!macroend

; General Settings
Name "${PLUGIN_NAME}"

; Run un-elevated by default; we relaunch ourselves elevated on demand (see the elevation
; helpers above) only when the chosen game folder needs admin.
RequestExecutionLevel user
SetCompressor /SOLID LZMA
Target AMD64-Unicode
OutFile "${OUTPUT_DIR}\${PLUGIN_NAME_LC}-Setup.exe"

; Variables
Var pluginInstallActionChoice   ; "0" = install/upgrade path, "1" = run uninstaller
Var freshInstallSelected        ; "1" = wipe savepath data before installing
Var removeUserDataSelected      ; uninstaller: "1" = also delete savepath data
Var analyticsOptOut             ; "1" = user unticked usage statistics on the Privacy page
Var userDocuments               ; Documents folder of the user who launched Setup
Var isElevatedRun               ; "1" = this process is the relaunched elevated child
Var useMachineReg               ; "1" = write uninstall keys to HKLM (we have admin), else HKCU
Var unKeysInMachine             ; uninstaller: "1" = uninstall keys live in HKLM, else HKCU
Var isPluginAlreadyInstalled

; MX Bikes variables
Var MXBikesInstallPath
Var isMXBikesPathAutoDetected
Var isMXBikesSelected

; GP Bikes variables
Var GPBikesInstallPath
Var isGPBikesPathAutoDetected
Var isGPBikesSelected

; Kart Racing Pro variables
Var KRPInstallPath
Var isKRPPathAutoDetected
Var isKRPSelected

; Directory page controls
Var MXBikesPathCtrl
Var GPBikesPathCtrl
Var KRPPathCtrl
Var MXBikesBrowseBtn
Var GPBikesBrowseBtn
Var KRPBrowseBtn
Var MXBikesCheckbox
Var GPBikesCheckbox
Var KRPCheckbox

; Existing-install page + uninstall page controls
Var existingPluginNoteLabel
Var removeUserDataCheckbox
Var analyticsCheckbox

; Welcome to MXBMRP3 Setup (skipped in the relaunched elevated child — the user already
; made every choice in the original, un-elevated window)
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageIfElevatedChild
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation of ${PLUGIN_NAME} for PiBoSo racing games.$\n$\nSupported games:$\n  • MX Bikes$\n  • GP Bikes$\n  • Kart Racing Pro$\n$\nSetup will try to find your game installations automatically.$\n$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

; Existing MXBMRP3 Installation Detected
Page Custom ShowExistingPluginInstallPage RunUninstaller

; Privacy and usage statistics
;
; Analytics is ON by default and always has been, disclosed in the README. That
; disclosure is only a CHOICE if the player sees it before the first ping goes out,
; which is what this page is for -- and it is a condition of the SignPath Foundation
; OSS policy (describe the behaviour, show it during installation, offer an option
; to disable it). Ticked by default so it states the real default rather than
; quietly changing it; unticking writes the marker read by core/install_prefs.h.
Page Custom ShowPrivacyPage LeavePrivacyPage

; Choose Games and Install Locations
Page Custom ShowGameSelectionPage LeaveGameSelectionPage

; Installing
!insertmacro MUI_PAGE_INSTFILES

; Completing MXBMRP3 Setup
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been installed on your computer.$\n$\nYour settings and data are stored per-game in:$\n  Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
!insertmacro MUI_PAGE_FINISH

; Uninstalling - select what to remove
UninstPage Custom un.ShowUninstallSelectionPage un.LeaveUninstallSelectionPage

; Uninstalling
!insertmacro MUI_UNPAGE_INSTFILES

; Completing MXBMRP3 Uninstall
;
; This text is the DATA-KEPT case. When the user ticks "also remove settings and
; data" on the selection page, un.FinishPageShow rewrites it — telling someone to
; manually delete a folder the uninstaller just deleted reads as "the wipe didn't
; work", which is the opposite of what a final screen should convey.
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nTo remove all settings and data, manually delete:$\n  Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
!define MUI_PAGE_CUSTOMFUNCTION_SHOW un.FinishPageShow
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
ShowInstDetails show
ShowUninstDetails show

; File properties
;
; Mirrors the DLL's VERSIONINFO block (mxbmrp3.rc) so Setup.exe and the plugin it
; installs identify themselves the same way in Explorer's Details tab and Task
; Manager. FileDescription matters most: it is the "Program name" Windows shows on
; the UAC consent prompt when Setup relaunches itself elevated, so it must lead with
; a product name, not a URL (the URL moved to Comments, which is where it belongs).
; Its wording is the GitHub repository description with a "Setup" prefix, matching
; mxbmrp3.rc's FileDescription — keep the two in step. Naming the engine family
; rather than the individual games is deliberate: the DLL's old wording enumerated
; them and went stale, still claiming "MX Bikes and GP Bikes" a release cycle after
; Kart Racing Pro shipped. Keeping it short also matters here specifically, because
; the UAC prompt truncates a long "Program name".
;
; The values are duplicated rather than shared because .rc and .nsi are compiled by
; different toolchains with no common header — keep the two blocks in step by hand.
;
; This block is also load-bearing for antivirus reputation. Unsigned installers are
; scored partly on version-info completeness and internal consistency, and this one
; previously carried only 5 keys, no CompanyName/OriginalFilename, and a build
; TIMESTAMP where FileVersion belongs — so FileVersion and ProductVersion disagreed
; on every build. PLUGIN_VERSION is the "MAJOR.MINOR.PATCH.BUILD" string derived from
; resource.h + the git commit count, i.e. exactly what the DLL's FILEVERSION carries.
VIProductVersion "${PLUGIN_VERSION}"
VIAddVersionKey "CompanyName"      "${PLUGIN_PUBLISHER}"
VIAddVersionKey "FileDescription"  "MXBMRP3 Setup - Plugin for PiBoSo Racing Simulators"
VIAddVersionKey "FileVersion"      "${PLUGIN_VERSION}"
VIAddVersionKey "InternalName"     "${PLUGIN_NAME_LC}-Setup"
VIAddVersionKey "LegalCopyright"   "Copyright (c) 2025-2026 ${PLUGIN_PUBLISHER}"
VIAddVersionKey "OriginalFilename" "${PLUGIN_NAME_LC}-Setup.exe"
VIAddVersionKey "ProductName"      "${PLUGIN_NAME}"
VIAddVersionKey "ProductVersion"   "${PLUGIN_VERSION}"
VIAddVersionKey "Comments"         "${PLUGIN_URL}"

; .onInit: Determine registry view & locate games
Function .onInit
  SetRegView 64

  ; Initialize variables
  StrCpy $isPluginAlreadyInstalled "0"
  StrCpy $pluginInstallActionChoice "0"
  StrCpy $freshInstallSelected "0"
  StrCpy $analyticsOptOut "0"      ; analytics ships ON; the Privacy page can only turn it off
  StrCpy $isMXBikesSelected "0"
  StrCpy $isGPBikesSelected "0"
  StrCpy $isKRPSelected "0"
  StrCpy $isMXBikesPathAutoDetected "0"
  StrCpy $isGPBikesPathAutoDetected "0"
  StrCpy $isKRPPathAutoDetected "0"
  StrCpy $isElevatedRun "0"

  ; Resolve the launching user's Documents (not the elevated admin's)
  !insertmacro RESOLVE_USER_DOCUMENTS

  ; Do we have admin rights (can we write the machine-wide HKLM uninstall key)?
  !insertmacro TEST_MACHINE_REG_WRITABLE $useMachineReg

  ; Are we the relaunched elevated child? If so, every choice was already made in the
  ; original window and handed to us on the command line — load it and skip the wizard.
  ${GetParameters} $9
  ClearErrors
  ${GetOptions} $9 "/ELEVATED" $8
  ${IfNot} ${Errors}
    StrCpy $isElevatedRun "1"
    Call LoadInstallStateFromCmdline
    Return
  ${EndIf}

  ; Check for existing MXBMRP3 install in registry (HKLM first, then per-user HKCU)
  ReadRegStr $0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
  ${If} $0 == ""
    ReadRegStr $0 HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
    StrCpy $R8 "HKCU64"
  ${Else}
    StrCpy $R8 "HKLM64"
  ${EndIf}
  IfFileExists "$0" 0 skip_existing_check
    ${If} $R8 == "HKLM64"
      ReadRegStr $INSTDIR HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    ${Else}
      ReadRegStr $INSTDIR HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    ${EndIf}
    IfFileExists "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" 0 skip_existing_check
      StrCpy $isPluginAlreadyInstalled "1"
      ; Check which games have the plugin installed (paths are full plugins paths)
      ${If} $R8 == "HKLM64"
        ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
        ReadRegStr $GPBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
        ReadRegStr $KRPInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
      ${Else}
        ReadRegStr $MXBikesInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
        ReadRegStr $GPBikesInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
        ReadRegStr $KRPInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
      ${EndIf}
      ${If} $MXBikesInstallPath == ""
      ${AndIf} $GPBikesInstallPath == ""
      ${AndIf} $KRPInstallPath == ""
        ; Legacy installation (pre-multi-game) - was MX Bikes only
        StrCpy $MXBikesInstallPath "$INSTDIR"
      ${EndIf}
  skip_existing_check:

  ; Auto-detect games that aren't already in registry
  ${If} $MXBikesInstallPath == ""
    Call DetectMXBikes
  ${Else}
    ; Already have path from registry, mark as detected
    StrCpy $isMXBikesPathAutoDetected "1"
  ${EndIf}

  ${If} $GPBikesInstallPath == ""
    Call DetectGPBikes
  ${Else}
    ; Already have path from registry, mark as detected
    StrCpy $isGPBikesPathAutoDetected "1"
  ${EndIf}

  ${If} $KRPInstallPath == ""
    Call DetectKRP
  ${Else}
    ; Already have path from registry, mark as detected
    StrCpy $isKRPPathAutoDetected "1"
  ${EndIf}

  ; Pre-select detected games
  ${If} $isMXBikesPathAutoDetected == "1"
    StrCpy $isMXBikesSelected "1"
  ${EndIf}
  ${If} $isGPBikesPathAutoDetected == "1"
    StrCpy $isGPBikesSelected "1"
  ${EndIf}
  ${If} $isKRPPathAutoDetected == "1"
    StrCpy $isKRPSelected "1"
  ${EndIf}

  ; Set INSTDIR to first detected game (paths already include \plugins)
  ${If} $MXBikesInstallPath != ""
    StrCpy $INSTDIR "$MXBikesInstallPath"
  ${ElseIf} $GPBikesInstallPath != ""
    StrCpy $INSTDIR "$GPBikesInstallPath"
  ${ElseIf} $KRPInstallPath != ""
    StrCpy $INSTDIR "$KRPInstallPath"
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES64\MX Bikes\plugins"
  ${EndIf}
FunctionEnd

; Skip a wizard page when running as the relaunched elevated child (used as the MUI
; Welcome page's PRE callback, and mirrored inline in the custom pages' create functions).
Function SkipPageIfElevatedChild
  ${If} $isElevatedRun == "1"
    Abort
  ${EndIf}
FunctionEnd

; Elevated child: reconstruct the user's choices from the command line the parent passed.
; A game is "selected" iff its /XXX="path" option is present; /FRESH=1 requests the data wipe.
Function LoadInstallStateFromCmdline
  ${GetParameters} $9

  StrCpy $isMXBikesSelected "0"
  ClearErrors
  ${GetOptions} $9 "/MXB=" $MXBikesInstallPath
  ${IfNot} ${Errors}
    StrCpy $isMXBikesSelected "1"
  ${EndIf}

  StrCpy $isGPBikesSelected "0"
  ClearErrors
  ${GetOptions} $9 "/GPB=" $GPBikesInstallPath
  ${IfNot} ${Errors}
    StrCpy $isGPBikesSelected "1"
  ${EndIf}

  StrCpy $isKRPSelected "0"
  ClearErrors
  ${GetOptions} $9 "/KRP=" $KRPInstallPath
  ${IfNot} ${Errors}
    StrCpy $isKRPSelected "1"
  ${EndIf}

  ClearErrors
  ${GetOptions} $9 "/FRESH=" $freshInstallSelected
  ${If} ${Errors}
    StrCpy $freshInstallSelected "0"
  ${EndIf}

  ; The Privacy page is skipped in the elevated child like every other custom page,
  ; so the choice has to arrive here or an opt-out would be silently lost exactly
  ; when the game lives somewhere that needs elevation.
  ClearErrors
  ${GetOptions} $9 "/NOSTATS=" $analyticsOptOut
  ${If} ${Errors}
    StrCpy $analyticsOptOut "0"
  ${EndIf}

  ; INSTDIR (uninstaller lands here) = first selected game's plugins folder
  ${If} $isMXBikesSelected == "1"
    StrCpy $INSTDIR "$MXBikesInstallPath"
  ${ElseIf} $isGPBikesSelected == "1"
    StrCpy $INSTDIR "$GPBikesInstallPath"
  ${Else}
    StrCpy $INSTDIR "$KRPInstallPath"
  ${EndIf}
FunctionEnd

; Detect MX Bikes installation (sets full plugins path)
Function DetectMXBikes
  ; Try Steam registry first
  ReadRegStr $R0 HKLM64 "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App ${MXBIKES_STEAM_APPID}" "InstallLocation"
  ${If} $R0 != ""
    IfFileExists "$R0\${MXBIKES_EXE}" 0 mxb_try_programfiles
      StrCpy $MXBikesInstallPath "$R0\plugins"
      StrCpy $isMXBikesPathAutoDetected "1"
      Return
  ${EndIf}

  mxb_try_programfiles:
  ; Try Program Files
  StrCpy $R0 "$PROGRAMFILES64\MX Bikes"
  IfFileExists "$R0\${MXBIKES_EXE}" 0 mxb_not_found
    StrCpy $MXBikesInstallPath "$R0\plugins"
    StrCpy $isMXBikesPathAutoDetected "1"
    Return

  mxb_not_found:
  StrCpy $MXBikesInstallPath ""
  StrCpy $isMXBikesPathAutoDetected "0"
FunctionEnd

; Detect GP Bikes installation (sets full plugins path)
Function DetectGPBikes
  ; Try Steam registry first
  ReadRegStr $R0 HKLM64 "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App ${GPBIKES_STEAM_APPID}" "InstallLocation"
  ${If} $R0 != ""
    IfFileExists "$R0\${GPBIKES_EXE}" 0 gpb_try_programfiles
      StrCpy $GPBikesInstallPath "$R0\plugins"
      StrCpy $isGPBikesPathAutoDetected "1"
      Return
  ${EndIf}

  gpb_try_programfiles:
  ; Try Program Files
  StrCpy $R0 "$PROGRAMFILES64\GP Bikes"
  IfFileExists "$R0\${GPBIKES_EXE}" 0 gpb_not_found
    StrCpy $GPBikesInstallPath "$R0\plugins"
    StrCpy $isGPBikesPathAutoDetected "1"
    Return

  gpb_not_found:
  StrCpy $GPBikesInstallPath ""
  StrCpy $isGPBikesPathAutoDetected "0"
FunctionEnd

; Detect Kart Racing Pro installation (sets full plugins path)
Function DetectKRP
  ; Try Steam registry first
  ReadRegStr $R0 HKLM64 "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App ${KRP_STEAM_APPID}" "InstallLocation"
  ${If} $R0 != ""
    IfFileExists "$R0\${KRP_EXE}" 0 krp_try_programfiles
      StrCpy $KRPInstallPath "$R0\plugins"
      StrCpy $isKRPPathAutoDetected "1"
      Return
  ${EndIf}

  krp_try_programfiles:
  ; Try Program Files
  StrCpy $R0 "$PROGRAMFILES64\Kart Racing Pro"
  IfFileExists "$R0\${KRP_EXE}" 0 krp_not_found
    StrCpy $KRPInstallPath "$R0\plugins"
    StrCpy $isKRPPathAutoDetected "1"
    Return

  krp_not_found:
  StrCpy $KRPInstallPath ""
  StrCpy $isKRPPathAutoDetected "0"
FunctionEnd

; Existing MXBMRP3 Installation Detected
Function ShowExistingPluginInstallPage
  ${If} $isElevatedRun == "1"
    Abort
  ${EndIf}
  ${If} $isPluginAlreadyInstalled == "0"
    Abort
  ${EndIf}
  StrCpy $pluginInstallActionChoice "0"
  nsDialogs::Create 1018
  Pop $R0
  ${If} $R0 == error
    Abort
  ${EndIf}
  !insertmacro MUI_HEADER_TEXT "Existing ${PLUGIN_NAME} Installation Detected" \
      "An existing version was found. Choose how you'd like to proceed."

  ; Build game list text dynamically from installed paths
  StrCpy $R1 ""
  ${If} $MXBikesInstallPath != ""
    StrCpy $R1 "MX Bikes"
  ${EndIf}
  ${If} $GPBikesInstallPath != ""
    ${If} $R1 != ""
      StrCpy $R1 "$R1, GP Bikes"
    ${Else}
      StrCpy $R1 "GP Bikes"
    ${EndIf}
  ${EndIf}
  ${If} $KRPInstallPath != ""
    ${If} $R1 != ""
      StrCpy $R1 "$R1, Kart Racing Pro"
    ${Else}
      StrCpy $R1 "Kart Racing Pro"
    ${EndIf}
  ${EndIf}
  ${If} $R1 == ""
    StrCpy $R1 "Unknown"
  ${EndIf}

  ${NSD_CreateLabel} 0 0 300u 10u "Existing ${PLUGIN_NAME} installation found for: $R1"
  Pop $R2
  ${NSD_CreateLabel} 0 18u 300u 10u "Choose your action."
  Pop $R4
  ${NSD_CreateGroupBox} 0 34u 300u 56u "Action"
  Pop $R1
  ${NSD_CreateRadioButton} 8u 46u 280u 10u "Upgrade — overwrite plugin files, keep settings and data"
  Pop $0
  ${NSD_AddStyle} $0 ${WS_GROUP}
  ${NSD_SetState} $0 1
  ${NSD_OnClick} $0 SetPluginUpgradeActionChoice
  ${NSD_CreateRadioButton} 8u 60u 280u 10u "Fresh install — overwrite plugin files and reset settings and data"
  Pop $1
  ${NSD_OnClick} $1 SetPluginFreshInstallActionChoice
  ${NSD_CreateRadioButton} 8u 74u 280u 10u "Remove (uninstall) ${PLUGIN_NAME}"
  Pop $2
  ${NSD_OnClick} $2 SetPluginUninstallActionChoice
  ${NSD_CreateLabel} 0 96u 300u 20u "Your settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}) will be preserved."
  Pop $existingPluginNoteLabel
  nsDialogs::Show
FunctionEnd

Function SetPluginUpgradeActionChoice
  StrCpy $pluginInstallActionChoice "0"
  StrCpy $freshInstallSelected "0"
  ${NSD_SetText} $existingPluginNoteLabel "Your settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}) will be preserved."
FunctionEnd

Function SetPluginFreshInstallActionChoice
  StrCpy $pluginInstallActionChoice "0"
  StrCpy $freshInstallSelected "1"
  ${NSD_SetText} $existingPluginNoteLabel "Warning: existing settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}) will be deleted for each selected game."
FunctionEnd

Function SetPluginUninstallActionChoice
  StrCpy $pluginInstallActionChoice "1"
  StrCpy $freshInstallSelected "0"
  ${NSD_SetText} $existingPluginNoteLabel "The uninstaller will let you choose whether to also remove settings and data."
FunctionEnd

Function RunUninstaller
  ${If} $pluginInstallActionChoice == "1"
    ; Read uninstaller path from registry (don't rely on $INSTDIR which may have changed).
    ; Try HKLM (machine install) first, then per-user HKCU. The uninstaller self-elevates
    ; on its own if the game folder needs admin.
    ReadRegStr $R0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
    ${If} $R0 == ""
      ReadRegStr $R0 HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
    ${EndIf}
    ${If} $R0 != ""
      ExecWait '"$R0"'
    ${EndIf}
    Quit
  ${EndIf}
FunctionEnd

; Game Selection Page
; Privacy and usage statistics
;
; Summarises what the plugin sends and offers the opt-out. The full policy is the
; README's Privacy section (linked); this page states the substance rather than
; making the player go and read it, because a link nobody opens is not disclosure.
;
; The box is TICKED by default because that IS the shipped default -- presenting it
; unticked would misstate the behaviour. Unticking is what writes the marker; leaving
; it ticked writes nothing at all, so an upgrade can never switch analytics back on
; for somebody who turned it off in-game (core/install_prefs.h explains why that
; direction is deliberately impossible).
; The Privacy page's two links. ExecShell rather than anything fancier: Setup may
; be running elevated, and handing the URL to the shell is what opens it as the
; logged-in user rather than as admin.
Function OnPrivacyLinkClick
  ExecShell "open" "${PLUGIN_URL}#privacy"
FunctionEnd

Function OnReportLinkClick
  ExecShell "open" "${PLUGIN_URL}/blob/main/analytics/REPORT.md"
FunctionEnd

Function ShowPrivacyPage
  ${If} $isElevatedRun == "1"
    Abort
  ${EndIf}
  nsDialogs::Create 1018
  Pop $R0
  ${If} $R0 == error
    Abort
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "Privacy" \
      "Choose whether to take part in the anonymous usage survey."

  ; LAYOUT BUDGET, measured from a real render rather than estimated: the usable
  ; client area is about 115u tall and 300u wide -- roughly 75 characters and 8u
  ; per line. An NSIS label does not scroll or ellipsize, it silently CLIPS, so a
  ; block that grows by one line just loses that line with nothing to notice it.
  ; That already shipped once: the closing label carried a blank line, needed
  ; three lines in 20u, and cut "You can change this..." in half.
  ; Keep each block inside its stated line count, or move detail to the README.
  ;
  ; The text is SHORT on purpose. Earlier drafts argued the case at length --
  ; download counts versus telemetry, crashes three times over -- and the page
  ; became a wall nobody reads, which persuades less than a page they finish.
  ;
  ; WHOSE crashes is stated once, and it is the game's. The plugin installs a
  ; process-wide handler and catches faults in the host; its own frames are
  ; almost never on the stack (zero across the 1.29.1 reports). Mentioned three
  ; times and never attributed, it read as "this plugin is unstable" -- both
  ; discouraging and untrue.
  ${NSD_CreateLabel} 0 0 300u 48u "${PLUGIN_NAME} can send a small anonymous ping each game launch, so the developer can see how many people use it, which features are worth keeping, and what needs fixing.$\n$\nIt sends a random install ID, the plugin version and which game, your enabled features, operating system and language, session length, and where the game faulted if it crashed."
  Pop $R1

  ; "Content you added" rather than the old "a theme or spotter voice": those
  ; two are the only content types the ping mentions at all (as shipped-name /
  ; "custom" labels; pit boards, gauges, pads and fonts are never reported),
  ; but the reader should not have to know that taxonomy for the promise to
  ; cover everything they might make. Broader wording, same truth.
  ${NSD_CreateLabel} 0 52u 300u 22u "It never sends your name, lap times, online activity, server or rider data, or any crash dump. Content you added yourself is never reported by name - at most it counts as $\"custom$\"."
  Pop $R1

  ; Phrasing borrowed from Debian's popularity-contest ("Participate in the
  ; package usage survey?"), which has had two decades to settle. It NAMES a
  ; bounded thing to join instead of asking for goodwill: "help improve
  ; MXBMRP3" is vague enough to be either a promise or a guilt trip, while
  ; "participate in the usage survey" is just what it is, and leaves the
  ; deciding to the reader.
  ;
  ; popcon's real lesson is above this line, not on it: its description gives a
  ; CHECKABLE consequence ("influences decisions such as which packages should
  ; go on the first distribution CD") rather than a promise to improve. The
  ; label above -- which features are worth keeping -- is the same move, and
  ; the link at the bottom is this page's popcon.debian.org.
  ${NSD_CreateCheckbox} 0 78u 300u 12u "Participate in the anonymous usage survey"
  Pop $analyticsCheckbox
  ${If} $analyticsOptOut != "1"
    ${NSD_Check} $analyticsCheckbox
  ${EndIf}

  ; States the ABSENCE of a downside, not the presence of one: people decline
  ; telemetry less readily when they suspect it gates a feature or earns them a
  ; nagging prompt, and neither is true. The one real consequence -- the game
  ; crashes you hit stop being reported -- is deliberately NOT here: beside the
  ; checkbox it reads as leverage, so it lives in the README's Privacy section.
  ${NSD_CreateLabel} 0 94u 300u 16u "Turning this off doesn't disable anything else; the plugin works exactly the same. You can change it any time in Settings > General > Integrations."
  Pop $R1

  ; Each link control is sized to ITS OWN text, not padded out: a link is
  ; left-aligned inside its control, so leftover width shows up as dead space
  ; between the two and reads as a layout mistake rather than a gap.
  ;
  ; Real link controls rather than pasted URLs: nobody retypes a URL off an
  ; installer page. Side by side on ONE row rather than stacked: the page has
  ; horizontal room and almost none left vertically.
  ;
  ; BOTH, because they answer different questions. The policy is the formal
  ; answer to "what exactly are you taking" -- already on this page in
  ; substance, but a reader who wants the full text should not have to hunt for
  ; it. The report answers the one thing the page cannot: what does any of this
  ; actually amount to? That one is the more persuasive, and checkable, so it
  ; gets the wider and more inviting label.
  ${NSD_CreateLink} 0 116u 54u 10u "Privacy policy"
  Pop $R2
  ${NSD_OnClick} $R2 OnPrivacyLinkClick

  ${NSD_CreateLink} 58u 116u 160u 10u "See the published usage report"
  Pop $R3
  ${NSD_OnClick} $R3 OnReportLinkClick

  nsDialogs::Show
FunctionEnd

Function LeavePrivacyPage
  ${NSD_GetState} $analyticsCheckbox $R0
  ${If} $R0 == ${BST_CHECKED}
    StrCpy $analyticsOptOut "0"
  ${Else}
    StrCpy $analyticsOptOut "1"
  ${EndIf}
FunctionEnd

Function ShowGameSelectionPage
  ${If} $isElevatedRun == "1"
    Abort
  ${EndIf}
  nsDialogs::Create 1018
  Pop $R0
  ${If} $R0 == error
    Abort
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "Select Games" \
      "Choose which games to install ${PLUGIN_NAME} for."

  ; MX Bikes section
  ${If} $isMXBikesPathAutoDetected == "1"
    ${NSD_CreateCheckbox} 0 0 300u 12u "MX Bikes (Detected)"
  ${Else}
    ${NSD_CreateCheckbox} 0 0 300u 12u "MX Bikes (Not detected)"
  ${EndIf}
  Pop $MXBikesCheckbox
  ${If} $isMXBikesSelected == "1"
    ${NSD_SetState} $MXBikesCheckbox ${BST_CHECKED}
  ${EndIf}
  ${NSD_OnClick} $MXBikesCheckbox OnMXBikesCheckboxClick

  ; MX Bikes path
  ${NSD_CreateText} 16u 16u 230u 12u "$MXBikesInstallPath"
  Pop $MXBikesPathCtrl
  ${NSD_CreateBrowseButton} 250u 15u 50u 14u "Browse..."
  Pop $MXBikesBrowseBtn
  ${NSD_OnClick} $MXBikesBrowseBtn OnMXBikesBrowse

  ; GP Bikes section
  ${If} $isGPBikesPathAutoDetected == "1"
    ${NSD_CreateCheckbox} 0 40u 300u 12u "GP Bikes (Detected)"
  ${Else}
    ${NSD_CreateCheckbox} 0 40u 300u 12u "GP Bikes (Not detected)"
  ${EndIf}
  Pop $GPBikesCheckbox
  ${If} $isGPBikesSelected == "1"
    ${NSD_SetState} $GPBikesCheckbox ${BST_CHECKED}
  ${EndIf}
  ${NSD_OnClick} $GPBikesCheckbox OnGPBikesCheckboxClick

  ; GP Bikes path
  ${NSD_CreateText} 16u 56u 230u 12u "$GPBikesInstallPath"
  Pop $GPBikesPathCtrl
  ${NSD_CreateBrowseButton} 250u 55u 50u 14u "Browse..."
  Pop $GPBikesBrowseBtn
  ${NSD_OnClick} $GPBikesBrowseBtn OnGPBikesBrowse

  ; Kart Racing Pro section
  ${If} $isKRPPathAutoDetected == "1"
    ${NSD_CreateCheckbox} 0 80u 300u 12u "Kart Racing Pro (Detected)"
  ${Else}
    ${NSD_CreateCheckbox} 0 80u 300u 12u "Kart Racing Pro (Not detected)"
  ${EndIf}
  Pop $KRPCheckbox
  ${If} $isKRPSelected == "1"
    ${NSD_SetState} $KRPCheckbox ${BST_CHECKED}
  ${EndIf}
  ${NSD_OnClick} $KRPCheckbox OnKRPCheckboxClick

  ; Kart Racing Pro path
  ${NSD_CreateText} 16u 96u 230u 12u "$KRPInstallPath"
  Pop $KRPPathCtrl
  ${NSD_CreateBrowseButton} 250u 95u 50u 14u "Browse..."
  Pop $KRPBrowseBtn
  ${NSD_OnClick} $KRPBrowseBtn OnKRPBrowse

  ; Info text
  ${NSD_CreateLabel} 0 120u 300u 12u "Select at least one game to install."
  Pop $R2

  ; Set initial state
  Call UpdateNextButtonState
  Call UpdateControlStates

  nsDialogs::Show
FunctionEnd

; Enable/disable path controls based on checkbox state
Function UpdateControlStates
  ${If} $isMXBikesSelected == "1"
    EnableWindow $MXBikesPathCtrl 1
    EnableWindow $MXBikesBrowseBtn 1
  ${Else}
    EnableWindow $MXBikesPathCtrl 0
    EnableWindow $MXBikesBrowseBtn 0
  ${EndIf}

  ${If} $isGPBikesSelected == "1"
    EnableWindow $GPBikesPathCtrl 1
    EnableWindow $GPBikesBrowseBtn 1
  ${Else}
    EnableWindow $GPBikesPathCtrl 0
    EnableWindow $GPBikesBrowseBtn 0
  ${EndIf}

  ${If} $isKRPSelected == "1"
    EnableWindow $KRPPathCtrl 1
    EnableWindow $KRPBrowseBtn 1
  ${Else}
    EnableWindow $KRPPathCtrl 0
    EnableWindow $KRPBrowseBtn 0
  ${EndIf}
FunctionEnd

Function OnMXBikesCheckboxClick
  ${NSD_GetState} $MXBikesCheckbox $isMXBikesSelected
  Call UpdateNextButtonState
  Call UpdateControlStates
FunctionEnd

Function OnGPBikesCheckboxClick
  ${NSD_GetState} $GPBikesCheckbox $isGPBikesSelected
  Call UpdateNextButtonState
  Call UpdateControlStates
FunctionEnd

Function OnKRPCheckboxClick
  ${NSD_GetState} $KRPCheckbox $isKRPSelected
  Call UpdateNextButtonState
  Call UpdateControlStates
FunctionEnd

; Enable/disable Next button based on game selection
Function UpdateNextButtonState
  GetDlgItem $R0 $HWNDPARENT 1  ; 1 = Next/Install button
  ${If} $isMXBikesSelected == "1"
  ${OrIf} $isGPBikesSelected == "1"
  ${OrIf} $isKRPSelected == "1"
    EnableWindow $R0 1  ; Enable
  ${Else}
    EnableWindow $R0 0  ; Disable
  ${EndIf}
FunctionEnd

Function OnMXBikesBrowse
  ; Get parent folder for browse dialog (strip \plugins if present)
  ${If} $MXBikesInstallPath == ""
    StrCpy $R1 "$PROGRAMFILES64"
  ${Else}
    ${GetParent} "$MXBikesInstallPath" $R1
  ${EndIf}
  nsDialogs::SelectFolderDialog "Select MX Bikes installation folder" "$R1"
  Pop $R0
  ${If} $R0 != "error"
    ; Check if selected folder already ends with \plugins
    ${GetFileName} "$R0" $R1
    ${If} $R1 == "plugins"
      StrCpy $MXBikesInstallPath "$R0"
    ${Else}
      StrCpy $MXBikesInstallPath "$R0\plugins"
    ${EndIf}
    ${NSD_SetText} $MXBikesPathCtrl "$MXBikesInstallPath"
    ; Auto-check the checkbox when user browses
    ${NSD_SetState} $MXBikesCheckbox ${BST_CHECKED}
    StrCpy $isMXBikesSelected "1"
    Call UpdateNextButtonState
    Call UpdateControlStates
  ${EndIf}
FunctionEnd

Function OnGPBikesBrowse
  ; Get parent folder for browse dialog (strip \plugins if present)
  ${If} $GPBikesInstallPath == ""
    StrCpy $R1 "$PROGRAMFILES64"
  ${Else}
    ${GetParent} "$GPBikesInstallPath" $R1
  ${EndIf}
  nsDialogs::SelectFolderDialog "Select GP Bikes installation folder" "$R1"
  Pop $R0
  ${If} $R0 != "error"
    ; Check if selected folder already ends with \plugins
    ${GetFileName} "$R0" $R1
    ${If} $R1 == "plugins"
      StrCpy $GPBikesInstallPath "$R0"
    ${Else}
      StrCpy $GPBikesInstallPath "$R0\plugins"
    ${EndIf}
    ${NSD_SetText} $GPBikesPathCtrl "$GPBikesInstallPath"
    ; Auto-check the checkbox when user browses
    ${NSD_SetState} $GPBikesCheckbox ${BST_CHECKED}
    StrCpy $isGPBikesSelected "1"
    Call UpdateNextButtonState
    Call UpdateControlStates
  ${EndIf}
FunctionEnd

Function OnKRPBrowse
  ; Get parent folder for browse dialog (strip \plugins if present)
  ${If} $KRPInstallPath == ""
    StrCpy $R1 "$PROGRAMFILES64"
  ${Else}
    ${GetParent} "$KRPInstallPath" $R1
  ${EndIf}
  nsDialogs::SelectFolderDialog "Select Kart Racing Pro installation folder" "$R1"
  Pop $R0
  ${If} $R0 != "error"
    ; Check if selected folder already ends with \plugins
    ${GetFileName} "$R0" $R1
    ${If} $R1 == "plugins"
      StrCpy $KRPInstallPath "$R0"
    ${Else}
      StrCpy $KRPInstallPath "$R0\plugins"
    ${EndIf}
    ${NSD_SetText} $KRPPathCtrl "$KRPInstallPath"
    ; Auto-check the checkbox when user browses
    ${NSD_SetState} $KRPCheckbox ${BST_CHECKED}
    StrCpy $isKRPSelected "1"
    Call UpdateNextButtonState
    Call UpdateControlStates
  ${EndIf}
FunctionEnd

Function LeaveGameSelectionPage
  ; Get current text from path controls (these are full plugins paths)
  ${NSD_GetText} $MXBikesPathCtrl $MXBikesInstallPath
  ${NSD_GetText} $GPBikesPathCtrl $GPBikesInstallPath
  ${NSD_GetText} $KRPPathCtrl $KRPInstallPath

  ; Validate MX Bikes path if selected
  ${If} $isMXBikesSelected == "1"
    ${If} $MXBikesInstallPath == ""
      MessageBox MB_OK|MB_ICONEXCLAMATION "Please specify the MX Bikes plugins folder."
      Abort
    ${EndIf}
    ; Validate path looks correct (folder named "plugins" with game exe in parent)
    ${GetFileName} "$MXBikesInstallPath" $R1
    ${GetParent} "$MXBikesInstallPath" $R0
    ${If} $R1 != "plugins"
    ${OrIfNot} ${FileExists} "$R0\${MXBIKES_EXE}"
      MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 \
        "This does not appear to be the MX Bikes plugins folder. Continue anyway?" \
        IDNO abort_validation
    ${EndIf}
  ${EndIf}

  ; Validate GP Bikes path if selected
  ${If} $isGPBikesSelected == "1"
    ${If} $GPBikesInstallPath == ""
      MessageBox MB_OK|MB_ICONEXCLAMATION "Please specify the GP Bikes plugins folder."
      Abort
    ${EndIf}
    ; Validate path looks correct (folder named "plugins" with game exe in parent)
    ${GetFileName} "$GPBikesInstallPath" $R1
    ${GetParent} "$GPBikesInstallPath" $R0
    ${If} $R1 != "plugins"
    ${OrIfNot} ${FileExists} "$R0\${GPBIKES_EXE}"
      MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 \
        "This does not appear to be the GP Bikes plugins folder. Continue anyway?" \
        IDNO abort_validation
    ${EndIf}
  ${EndIf}

  ; Validate Kart Racing Pro path if selected
  ${If} $isKRPSelected == "1"
    ${If} $KRPInstallPath == ""
      MessageBox MB_OK|MB_ICONEXCLAMATION "Please specify the Kart Racing Pro plugins folder."
      Abort
    ${EndIf}
    ; Validate path looks correct (folder named "plugins" with game exe in parent)
    ${GetFileName} "$KRPInstallPath" $R1
    ${GetParent} "$KRPInstallPath" $R0
    ${If} $R1 != "plugins"
    ${OrIfNot} ${FileExists} "$R0\${KRP_EXE}"
      MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 \
        "This does not appear to be the Kart Racing Pro plugins folder. Continue anyway?" \
        IDNO abort_validation
    ${EndIf}
  ${EndIf}

  ; Confirm the destructive fresh-install data wipe (irreversible)
  ${If} $freshInstallSelected == "1"
    MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 \
      "Fresh install will permanently delete all existing ${PLUGIN_NAME} settings and data (profiles, stats, benchmarks, logs, crash dumps) for the selected games before reinstalling.$\n$\nThis cannot be undone. Continue?" \
      IDYES +2
    Abort
  ${EndIf}

  ; Set INSTDIR for uninstaller (paths already include \plugins)
  ${If} $isMXBikesSelected == "1"
    StrCpy $INSTDIR "$MXBikesInstallPath"
  ${ElseIf} $isGPBikesSelected == "1"
    StrCpy $INSTDIR "$GPBikesInstallPath"
  ${Else}
    StrCpy $INSTDIR "$KRPInstallPath"
  ${EndIf}

  ; On-demand elevation: if any selected plugins folder isn't writable with our current
  ; rights, relaunch ourselves elevated and hand the choices over on the command line. The
  ; elevated child skips the wizard and installs; we then quit. (Already-elevated children
  ; never reach this — they don't display this page.)
  ${If} $isElevatedRun == "0"
    StrCpy $R2 "0"  ; needElevation
    ${If} $isMXBikesSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$MXBikesInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}
    ${If} $isGPBikesSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$GPBikesInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}
    ${If} $isKRPSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$KRPInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}

    ${If} $R2 == "1"
      ; Build the relaunch command line (presence of /XXX implies that game is selected)
      StrCpy $R4 "/ELEVATED"
      ${If} $isMXBikesSelected == "1"
        StrCpy $R4 '$R4 /MXB="$MXBikesInstallPath"'
      ${EndIf}
      ${If} $isGPBikesSelected == "1"
        StrCpy $R4 '$R4 /GPB="$GPBikesInstallPath"'
      ${EndIf}
      ${If} $isKRPSelected == "1"
        StrCpy $R4 '$R4 /KRP="$KRPInstallPath"'
      ${EndIf}
      ${If} $freshInstallSelected == "1"
        StrCpy $R4 '$R4 /FRESH=1'
      ${EndIf}
      ${If} $analyticsOptOut == "1"
        StrCpy $R4 '$R4 /NOSTATS=1'
      ${EndIf}

      ClearErrors
      ExecShellWait "runas" "$EXEPATH" '$R4'
      ${IfNot} ${Errors}
        ; Elevated child owns the install and shows its own progress/finish; we're done.
        Quit
      ${EndIf}
      ; Elevation declined/unavailable: stay on this page so the user can pick a folder
      ; they can write to, go Back, or Cancel — don't tear down the whole wizard.
      MessageBox MB_ICONEXCLAMATION \
        "Administrator rights are required to install into the selected folder.$\n$\nChoose a folder you can write to, or close Setup and re-run it as administrator."
      Abort
    ${EndIf}
  ${EndIf}

  Return

  abort_validation:
  Abort
FunctionEnd

; Install
Section "Install ${PLUGIN_NAME}" Section_InstallPlugin
  SetAutoClose false

  ; Fresh install: wipe existing settings/data for each selected game before copying files
  ${If} $freshInstallSelected == "1"
    DetailPrint "Fresh install requested - clearing existing settings and data..."
    ${If} $isMXBikesSelected == "1"
      !insertmacro REMOVE_USER_DATA "${MXBIKES_DOCS_FOLDER}"
    ${EndIf}
    ${If} $isGPBikesSelected == "1"
      !insertmacro REMOVE_USER_DATA "${GPBIKES_DOCS_FOLDER}"
    ${EndIf}
    ${If} $isKRPSelected == "1"
      !insertmacro REMOVE_USER_DATA "${KRP_DOCS_FOLDER}"
    ${EndIf}
  ${EndIf}

  ; Install for each selected game (paths already include \plugins). The file list
  ; lives ONCE in INSTALL_GAME_FILES so all three games always ship the same payload.
  ${If} $isMXBikesSelected == "1"
    !insertmacro INSTALL_GAME_FILES "$MXBikesInstallPath" "${MXBIKES_DLO}" "MX Bikes"
  ${EndIf}

  ${If} $isGPBikesSelected == "1"
    !insertmacro INSTALL_GAME_FILES "$GPBikesInstallPath" "${GPBIKES_DLO}" "GP Bikes"
  ${EndIf}

  ${If} $isKRPSelected == "1"
    !insertmacro INSTALL_GAME_FILES "$KRPInstallPath" "${KRP_DLO}" "Kart Racing Pro"
  ${EndIf}

  ; Write uninstaller to INSTDIR (first selected game's plugins folder)
  WriteUninstaller "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"

  ; Copy uninstaller to each selected game folder (so partial uninstall works)
  ${If} $isMXBikesSelected == "1"
  ${AndIf} $MXBikesInstallPath != $INSTDIR
    CopyFiles /SILENT "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" "$MXBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
  ${EndIf}
  ${If} $isGPBikesSelected == "1"
  ${AndIf} $GPBikesInstallPath != $INSTDIR
    CopyFiles /SILENT "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" "$GPBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
  ${EndIf}
  ${If} $isKRPSelected == "1"
  ${AndIf} $KRPInstallPath != $INSTDIR
    CopyFiles /SILENT "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" "$KRPInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
  ${EndIf}

  ; Registry entries (Add/Remove Programs + uninstaller data). Machine-wide HKLM when we
  ; have admin, otherwise the per-user HKCU hive so an un-elevated install still registers.
  ${If} $useMachineReg == "1"
    !insertmacro WRITE_UNINSTALL_REG HKLM64
  ${Else}
    !insertmacro WRITE_UNINSTALL_REG HKCU64
  ${EndIf}

  DetailPrint "Installation complete."
SectionEnd

; ============================================================================
; UNINSTALLER FUNCTIONS
; ============================================================================

Function un.onInit
  SetRegView 64
  ; Initialize selection (will be set by page)
  StrCpy $isMXBikesSelected "0"
  StrCpy $isGPBikesSelected "0"
  StrCpy $isKRPSelected "0"
  StrCpy $removeUserDataSelected "0"
  StrCpy $removeUserDataCheckbox "0"
  StrCpy $isElevatedRun "0"
  ; Resolve the launching user's Documents (not the elevated admin's)
  !insertmacro RESOLVE_USER_DOCUMENTS

  ; Where do the uninstall keys live — machine-wide (HKLM) or per-user (HKCU)? This is
  ; the uninstaller's hive selector; it doesn't need the installer's $useMachineReg probe
  ; (deletes target whichever hive $unKeysInMachine points to, and clear both to be safe).
  ReadRegStr $0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
  ${If} $0 != ""
    StrCpy $unKeysInMachine "1"
  ${Else}
    StrCpy $unKeysInMachine "0"
  ${EndIf}

  ; Elevated child? The parent already collected the selection + paths and passed them on
  ; the command line — load them and skip the selection page.
  ${GetParameters} $9
  ClearErrors
  ${GetOptions} $9 "/ELEVATED" $8
  ${IfNot} ${Errors}
    StrCpy $isElevatedRun "1"
    Call un.LoadUninstallStateFromCmdline
    Return
  ${EndIf}

  ; Read installed paths from whichever hive holds the keys
  ${If} $unKeysInMachine == "1"
    ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
    ReadRegStr $GPBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
    ReadRegStr $KRPInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
  ${Else}
    ReadRegStr $MXBikesInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
    ReadRegStr $GPBikesInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
    ReadRegStr $KRPInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
  ${EndIf}

  ; Handle legacy (pre-multi-game) installations that only have InstallLocation
  ${If} $MXBikesInstallPath == ""
  ${AndIf} $GPBikesInstallPath == ""
  ${AndIf} $KRPInstallPath == ""
    ${If} $unKeysInMachine == "1"
      ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    ${Else}
      ReadRegStr $MXBikesInstallPath HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    ${EndIf}
  ${EndIf}
FunctionEnd

; Elevated uninstaller child: reconstruct selection, data flag and paths from the command
; line the parent passed (presence of /UXXX="path" ⇒ that game is selected for removal).
Function un.LoadUninstallStateFromCmdline
  ${GetParameters} $9

  StrCpy $isMXBikesSelected "0"
  ClearErrors
  ${GetOptions} $9 "/UMXB=" $MXBikesInstallPath
  ${IfNot} ${Errors}
    StrCpy $isMXBikesSelected "1"
  ${EndIf}

  StrCpy $isGPBikesSelected "0"
  ClearErrors
  ${GetOptions} $9 "/UGPB=" $GPBikesInstallPath
  ${IfNot} ${Errors}
    StrCpy $isGPBikesSelected "1"
  ${EndIf}

  StrCpy $isKRPSelected "0"
  ClearErrors
  ${GetOptions} $9 "/UKRP=" $KRPInstallPath
  ${IfNot} ${Errors}
    StrCpy $isKRPSelected "1"
  ${EndIf}

  ClearErrors
  ${GetOptions} $9 "/UDATA=" $removeUserDataSelected
  ${If} ${Errors}
    StrCpy $removeUserDataSelected "0"
  ${EndIf}
FunctionEnd

Function un.ShowUninstallSelectionPage
  ${If} $isElevatedRun == "1"
    Abort
  ${EndIf}
  nsDialogs::Create 1018
  Pop $R0
  ${If} $R0 == error
    Abort
  ${EndIf}

  !insertmacro MUI_HEADER_TEXT "Select Components to Remove" \
      "Choose which game installations to remove."

  ; Track vertical position for dynamic layout
  StrCpy $R9 "0"  ; Current Y position in dialog units

  ; MX Bikes section (only show if installed)
  ${If} $MXBikesInstallPath != ""
    ${NSD_CreateCheckbox} 0 $R9u 300u 12u "MX Bikes"
    Pop $MXBikesCheckbox
    ${NSD_SetState} $MXBikesCheckbox ${BST_CHECKED}
    StrCpy $isMXBikesSelected "1"
    ${NSD_OnClick} $MXBikesCheckbox un.OnMXBikesCheckboxClick

    ; MX Bikes path (read-only display)
    IntOp $R9 $R9 + 16
    ${NSD_CreateText} 16u $R9u 284u 12u "$MXBikesInstallPath"
    Pop $MXBikesPathCtrl
    SendMessage $MXBikesPathCtrl ${EM_SETREADONLY} 1 0

    IntOp $R9 $R9 + 24
  ${EndIf}

  ; GP Bikes section (only show if installed)
  ${If} $GPBikesInstallPath != ""
    ${NSD_CreateCheckbox} 0 $R9u 300u 12u "GP Bikes"
    Pop $GPBikesCheckbox
    ${NSD_SetState} $GPBikesCheckbox ${BST_CHECKED}
    StrCpy $isGPBikesSelected "1"
    ${NSD_OnClick} $GPBikesCheckbox un.OnGPBikesCheckboxClick

    ; GP Bikes path (read-only display)
    IntOp $R9 $R9 + 16
    ${NSD_CreateText} 16u $R9u 284u 12u "$GPBikesInstallPath"
    Pop $GPBikesPathCtrl
    SendMessage $GPBikesPathCtrl ${EM_SETREADONLY} 1 0

    IntOp $R9 $R9 + 24
  ${EndIf}

  ; Kart Racing Pro section (only show if installed)
  ${If} $KRPInstallPath != ""
    ${NSD_CreateCheckbox} 0 $R9u 300u 12u "Kart Racing Pro"
    Pop $KRPCheckbox
    ${NSD_SetState} $KRPCheckbox ${BST_CHECKED}
    StrCpy $isKRPSelected "1"
    ${NSD_OnClick} $KRPCheckbox un.OnKRPCheckboxClick

    ; Kart Racing Pro path (read-only display)
    IntOp $R9 $R9 + 16
    ${NSD_CreateText} 16u $R9u 284u 12u "$KRPInstallPath"
    Pop $KRPPathCtrl
    SendMessage $KRPPathCtrl ${EM_SETREADONLY} 1 0

    IntOp $R9 $R9 + 24
  ${EndIf}

  ; "Also remove settings and data" — only offered when a savepath data folder actually
  ; exists for one of the installed games (Documents\PiBoSo\[Game]\mxbmrp3).
  StrCpy $R8 "0"
  ${If} $MXBikesInstallPath != ""
    IfFileExists "$userDocuments\PiBoSo\${MXBIKES_DOCS_FOLDER}\${PLUGIN_NAME_LC}\*.*" 0 +2
      StrCpy $R8 "1"
  ${EndIf}
  ${If} $GPBikesInstallPath != ""
    IfFileExists "$userDocuments\PiBoSo\${GPBIKES_DOCS_FOLDER}\${PLUGIN_NAME_LC}\*.*" 0 +2
      StrCpy $R8 "1"
  ${EndIf}
  ${If} $KRPInstallPath != ""
    IfFileExists "$userDocuments\PiBoSo\${KRP_DOCS_FOLDER}\${PLUGIN_NAME_LC}\*.*" 0 +2
      StrCpy $R8 "1"
  ${EndIf}

  ${If} $R8 == "1"
    IntOp $R9 $R9 + 4
    ${NSD_CreateCheckbox} 0 $R9u 300u 20u "Also delete settings and data (Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC})"
    Pop $removeUserDataCheckbox
    ${If} $removeUserDataSelected == "1"
      ${NSD_SetState} $removeUserDataCheckbox ${BST_CHECKED}
    ${EndIf}
    ${NSD_OnClick} $removeUserDataCheckbox un.OnRemoveUserDataCheckboxClick
    IntOp $R9 $R9 + 24
  ${EndIf}

  ; Info text (no extra padding - matches install page)
  ${NSD_CreateLabel} 0 $R9u 300u 12u "Select at least one game to uninstall."
  Pop $R2

  ; Set initial button state
  Call un.UpdateUninstallButtonState

  nsDialogs::Show
FunctionEnd

Function un.OnRemoveUserDataCheckboxClick
  ${NSD_GetState} $removeUserDataCheckbox $removeUserDataSelected
FunctionEnd

; Finish page: swap in the data-removed wording when the wipe actually ran.
;
; SHOW rather than PRE: MUI2 builds this page with nsDialogs and creates the text
; label inside its own show handler (Contrib/Modern UI 2/Pages/Finish.nsh), so a
; PRE function would run before the control exists. It publishes the handle as
; $mui.FinishPage.Text, which is what makes this a supported override rather than
; a GetDlgItem guess at a control id.
;
; Wording is deliberately scoped to "the games you uninstalled": the wipe in
; un.RemoveUserData only touches games selected for uninstall, so data for a game
; left installed survives, and a blanket "all your data is gone" would be a lie
; in the partial-uninstall case.
Function un.FinishPageShow
  ${If} $removeUserDataSelected == "1"
    SendMessage $mui.FinishPage.Text ${WM_SETTEXT} 0 \
      "STR:${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nSettings and data for the games you uninstalled have also been removed.$\n$\nClick Finish to close Setup."
  ${EndIf}
FunctionEnd

Function un.OnMXBikesCheckboxClick
  ${NSD_GetState} $MXBikesCheckbox $isMXBikesSelected
  Call un.UpdateUninstallButtonState
FunctionEnd

Function un.OnGPBikesCheckboxClick
  ${NSD_GetState} $GPBikesCheckbox $isGPBikesSelected
  Call un.UpdateUninstallButtonState
FunctionEnd

Function un.OnKRPCheckboxClick
  ${NSD_GetState} $KRPCheckbox $isKRPSelected
  Call un.UpdateUninstallButtonState
FunctionEnd

; Enable/disable Uninstall button based on game selection
Function un.UpdateUninstallButtonState
  GetDlgItem $R0 $HWNDPARENT 1  ; 1 = Next/Uninstall button
  ${If} $isMXBikesSelected == "1"
  ${OrIf} $isGPBikesSelected == "1"
  ${OrIf} $isKRPSelected == "1"
    EnableWindow $R0 1  ; Enable
  ${Else}
    EnableWindow $R0 0  ; Disable
  ${EndIf}
FunctionEnd

Function un.LeaveUninstallSelectionPage
  ; Check at least one is selected
  ${If} $isMXBikesSelected != "1"
  ${AndIf} $isGPBikesSelected != "1"
  ${AndIf} $isKRPSelected != "1"
    MessageBox MB_OK|MB_ICONEXCLAMATION "Please select at least one game to uninstall from."
    Abort
  ${EndIf}

  ; Confirm the destructive data removal (irreversible)
  ${If} $removeUserDataSelected == "1"
    MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 \
      "This will permanently delete all ${PLUGIN_NAME} settings and data (profiles, stats, benchmarks, logs, crash dumps) for the selected games.$\n$\nThis cannot be undone. Continue?" \
      IDYES +2
    Abort
  ${EndIf}

  ; On-demand elevation: if any selected plugins folder can't be written with our current
  ; rights, relaunch the uninstaller elevated with the selection on the command line.
  ${If} $isElevatedRun == "0"
    StrCpy $R2 "0"  ; needElevation
    ${If} $isMXBikesSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$MXBikesInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}
    ${If} $isGPBikesSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$GPBikesInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}
    ${If} $isKRPSelected == "1"
      !insertmacro TEST_FOLDER_WRITABLE "$KRPInstallPath" $R3
      ${If} $R3 == "0"
        StrCpy $R2 "1"
      ${EndIf}
    ${EndIf}

    ${If} $R2 == "1"
      StrCpy $R4 "/ELEVATED /UDATA=$removeUserDataSelected"
      ${If} $isMXBikesSelected == "1"
        StrCpy $R4 '$R4 /UMXB="$MXBikesInstallPath"'
      ${EndIf}
      ${If} $isGPBikesSelected == "1"
        StrCpy $R4 '$R4 /UGPB="$GPBikesInstallPath"'
      ${EndIf}
      ${If} $isKRPSelected == "1"
        StrCpy $R4 '$R4 /UKRP="$KRPInstallPath"'
      ${EndIf}

      ClearErrors
      ExecShellWait "runas" "$EXEPATH" '$R4'
      ${IfNot} ${Errors}
        ; Elevated child owns the removal; we're done.
        Quit
      ${EndIf}
      ; Elevation declined/unavailable: stay on this page so the user can adjust the
      ; selection or Cancel, rather than tearing down the uninstaller.
      MessageBox MB_ICONEXCLAMATION \
        "Administrator rights are required to remove ${PLUGIN_NAME} from the selected folder.$\n$\nClose this and re-run the uninstaller as administrator, or cancel."
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

; Uninstall
Section "Uninstall"
  SetAutoClose false
  SetRegView 64

  DetailPrint "Removing ${PLUGIN_NAME} files..."

  ; Remove from MX Bikes if selected
  ${If} $isMXBikesSelected == "1"
  ${AndIf} $MXBikesInstallPath != ""
    DetailPrint "Removing from MX Bikes..."
    Delete "$MXBikesInstallPath\${MXBIKES_DLO}"
    RMDir /r "$MXBikesInstallPath\mxbmrp3_data"
    ; /REBOOTOK on the SELF-delete only. The uninstaller runs as NSIS's temp copy
    ; (Au_.exe), and its Delete of the original exe races the original stub still
    ; closing its own image handle. A plain Delete loses that race silently (no
    ; retry) and the exe is orphaned forever; /REBOOTOK falls back to
    ; MoveFileEx(DELAY_UNTIL_REBOOT), so the file is gone now or at next boot.
    ; Everything else this section deletes is not running, so stays plain.
    Delete /REBOOTOK "$MXBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
    ; Clear this path from registry (delete from both hives; no-op on the absent one)
    DeleteRegValue HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
    DeleteRegValue HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
  ${EndIf}

  ; Remove from GP Bikes if selected
  ${If} $isGPBikesSelected == "1"
  ${AndIf} $GPBikesInstallPath != ""
    DetailPrint "Removing from GP Bikes..."
    Delete "$GPBikesInstallPath\${GPBIKES_DLO}"
    RMDir /r "$GPBikesInstallPath\mxbmrp3_data"
    Delete /REBOOTOK "$GPBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"  ; see the MX Bikes note
    ; Clear this path from registry (delete from both hives; no-op on the absent one)
    DeleteRegValue HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
    DeleteRegValue HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
  ${EndIf}

  ; Remove from Kart Racing Pro if selected
  ${If} $isKRPSelected == "1"
  ${AndIf} $KRPInstallPath != ""
    DetailPrint "Removing from Kart Racing Pro..."
    Delete "$KRPInstallPath\${KRP_DLO}"
    RMDir /r "$KRPInstallPath\mxbmrp3_data"
    Delete /REBOOTOK "$KRPInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"  ; see the MX Bikes note
    ; Clear this path from registry (delete from both hives; no-op on the absent one)
    DeleteRegValue HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
    DeleteRegValue HKCU64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "KRPPath"
  ${EndIf}

  ; Optionally remove settings and data (Documents\PiBoSo\[Game]\mxbmrp3) for each
  ; uninstalled game, if the user opted in on the selection page.
  ${If} $removeUserDataSelected == "1"
    DetailPrint "Removing ${PLUGIN_NAME} settings and data..."
    ${If} $isMXBikesSelected == "1"
      !insertmacro REMOVE_USER_DATA "${MXBIKES_DOCS_FOLDER}"
    ${EndIf}
    ${If} $isGPBikesSelected == "1"
      !insertmacro REMOVE_USER_DATA "${GPBIKES_DOCS_FOLDER}"
    ${EndIf}
    ${If} $isKRPSelected == "1"
      !insertmacro REMOVE_USER_DATA "${KRP_DOCS_FOLDER}"
    ${EndIf}
  ${EndIf}

  ; Check what remains and finalize the keys in whichever hive holds them
  ${If} $unKeysInMachine == "1"
    !insertmacro UN_FINALIZE_REG HKLM64
  ${Else}
    !insertmacro UN_FINALIZE_REG HKCU64
  ${EndIf}

  DetailPrint "Uninstallation complete."
SectionEnd
