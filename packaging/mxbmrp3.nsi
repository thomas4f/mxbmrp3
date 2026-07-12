; mxbmrp3.nsi
; Build for 64-bit using https://github.com/negrutiu/nsis

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"

!define PLUGIN_NAME "MXBMRP3"
!define PLUGIN_NAME_LC "mxbmrp3"
!define PLUGIN_PUBLISHER "thomas4f"
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

; Welcome to MXBMRP3 Setup (skipped in the relaunched elevated child — the user already
; made every choice in the original, un-elevated window)
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageIfElevatedChild
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation of ${PLUGIN_NAME} for PiBoSo racing games.$\n$\nSupported games:$\n  • MX Bikes$\n  • GP Bikes$\n  • Kart Racing Pro$\n$\nSetup will try to find your game installations automatically.$\n$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

; Existing MXBMRP3 Installation Detected
Page Custom ShowExistingPluginInstallPage RunUninstaller

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
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nTo remove all settings and data, manually delete:$\n  Documents\PiBoSo\[Game]\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
ShowInstDetails show
ShowUninstDetails show

; File properties
VIProductVersion "${PLUGIN_VERSION}"
VIAddVersionKey "ProductName" "MXBMRP3"
VIAddVersionKey "LegalCopyright" "thomas4f"
VIAddVersionKey "FileDescription" "https://github.com/thomas4f/MXBMRP3"
VIAddVersionKey "FileVersion" "${__DATE__} ${__TIME__}"
VIAddVersionKey "ProductVersion" "${PLUGIN_VERSION}"

; .onInit: Determine registry view & locate games
Function .onInit
  SetRegView 64

  ; Initialize variables
  StrCpy $isPluginAlreadyInstalled "0"
  StrCpy $pluginInstallActionChoice "0"
  StrCpy $freshInstallSelected "0"
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

  ; Install for MX Bikes if selected (paths already include \plugins)
  ${If} $isMXBikesSelected == "1"
    DetailPrint "Installing ${PLUGIN_NAME} for MX Bikes..."

    ; Plugin DLO
    SetOutPath "$MXBikesInstallPath"
    File "${PLUGIN_SOURCE_PATH}\${MXBIKES_DLO}"

    ; Fonts
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\fonts\*.fnt"

    ; Textures
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\textures"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\textures\*.tga"

    ; Icons
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\icons\*.tga"

    ; Web overlay files
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\web"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\*.*"
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\web\js"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\js\*.js"
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\web\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\fonts\*.ttf"
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\web\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\icons\*.svg"

    ; Web overlay logos
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data\web\logos"
    File /nonfatal "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\logos\*.png"

    DetailPrint "MX Bikes installation complete."
  ${EndIf}

  ; Install for GP Bikes if selected (paths already include \plugins)
  ${If} $isGPBikesSelected == "1"
    DetailPrint "Installing ${PLUGIN_NAME} for GP Bikes..."

    ; Plugin DLO
    SetOutPath "$GPBikesInstallPath"
    File "${PLUGIN_SOURCE_PATH}\${GPBIKES_DLO}"

    ; Fonts
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\fonts\*.fnt"

    ; Textures
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\textures"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\textures\*.tga"

    ; Icons
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\icons\*.tga"

    ; Web overlay files
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\web"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\*.*"
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\web\js"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\js\*.js"
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\web\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\fonts\*.ttf"
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\web\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\icons\*.svg"

    ; Web overlay logos
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data\web\logos"
    File /nonfatal "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\logos\*.png"

    DetailPrint "GP Bikes installation complete."
  ${EndIf}

  ; Install for Kart Racing Pro if selected (paths already include \plugins)
  ${If} $isKRPSelected == "1"
    DetailPrint "Installing ${PLUGIN_NAME} for Kart Racing Pro..."

    ; Plugin DLO
    SetOutPath "$KRPInstallPath"
    File "${PLUGIN_SOURCE_PATH}\${KRP_DLO}"

    ; Fonts
    SetOutPath "$KRPInstallPath\mxbmrp3_data\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\fonts\*.fnt"

    ; Textures
    SetOutPath "$KRPInstallPath\mxbmrp3_data\textures"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\textures\*.tga"

    ; Icons
    SetOutPath "$KRPInstallPath\mxbmrp3_data\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\icons\*.tga"

    ; Web overlay files
    SetOutPath "$KRPInstallPath\mxbmrp3_data\web"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\*.*"
    SetOutPath "$KRPInstallPath\mxbmrp3_data\web\js"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\js\*.js"
    SetOutPath "$KRPInstallPath\mxbmrp3_data\web\fonts"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\fonts\*.ttf"
    SetOutPath "$KRPInstallPath\mxbmrp3_data\web\icons"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\icons\*.svg"

    ; Web overlay logos
    SetOutPath "$KRPInstallPath\mxbmrp3_data\web\logos"
    File /nonfatal "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\web\logos\*.png"

    DetailPrint "Kart Racing Pro installation complete."
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
    Delete "$MXBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
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
    Delete "$GPBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
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
    Delete "$KRPInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
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
