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
!define PLUGIN_SOURCE_PATH "dist\staging"

; Game definitions
!define MXBIKES_STEAM_APPID "655500"
!define MXBIKES_EXE "mxbikes.exe"
!define MXBIKES_DLO "mxbmrp3.dlo"

!define GPBIKES_STEAM_APPID "848050"
!define GPBIKES_EXE "gpbikes.exe"
!define GPBIKES_DLO "mxbmrp3_gpb.dlo"
!ifndef PLUGIN_VERSION
  !define PLUGIN_VERSION 1.0.0.0
  ;!error "PLUGIN_VERSION is not defined. Please define it before building."
!endif
!ifndef OUTPUT_DIR
  !define OUTPUT_DIR "dist"
!endif

!define REG_UNINSTALL_KEY_PATH "Software\Microsoft\Windows\CurrentVersion\Uninstall"

; General Settings
Name "${PLUGIN_NAME}"

RequestExecutionLevel admin
SetCompressor /SOLID LZMA
Target AMD64-Unicode
OutFile "${OUTPUT_DIR}\${PLUGIN_NAME_LC}-Setup.exe"

; Variables
Var pluginInstallActionChoice
Var isPluginAlreadyInstalled
Var existingInstallGame  ; "MXBikes", "GPBikes", or "Both"

; MX Bikes variables
Var MXBikesInstallPath
Var isMXBikesPathAutoDetected
Var isMXBikesSelected

; GP Bikes variables
Var GPBikesInstallPath
Var isGPBikesPathAutoDetected
Var isGPBikesSelected

; Directory page controls
Var MXBikesPathCtrl
Var GPBikesPathCtrl
Var MXBikesBrowseBtn
Var GPBikesBrowseBtn
Var MXBikesCheckbox
Var GPBikesCheckbox

; Welcome to MXBMRP3 Setup
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation of ${PLUGIN_NAME} for PiBoSo racing games.$\n$\nSupported games:$\n  • MX Bikes$\n  • GP Bikes$\n$\nSetup will try to find your game installations automatically.$\n$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

; Existing MXBMRP3 Installation Detected
Page Custom ShowExistingPluginInstallPage RunUninstaller

; Choose Games and Install Locations
Page Custom ShowGameSelectionPage LeaveGameSelectionPage

; Installing
!insertmacro MUI_PAGE_INSTFILES

; Completing MXBMRP3 Setup
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been installed on your computer.$\n$\nYour settings and data are stored per-game in:$\n  Documents\PiBoSo\<Game>\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
!insertmacro MUI_PAGE_FINISH

; Uninstalling - select what to remove
UninstPage Custom un.ShowUninstallSelectionPage un.LeaveUninstallSelectionPage

; Uninstalling
!insertmacro MUI_UNPAGE_INSTFILES

; Completing MXBMRP3 Uninstall
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nTo remove all settings and data, manually delete:$\n  Documents\PiBoSo\<Game>\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
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
  StrCpy $existingInstallGame ""
  StrCpy $isMXBikesSelected "0"
  StrCpy $isGPBikesSelected "0"
  StrCpy $isMXBikesPathAutoDetected "0"
  StrCpy $isGPBikesPathAutoDetected "0"

  ; Check for existing MXBMRP3 install in registry
  ReadRegStr $0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
  IfFileExists "$0" 0 skip_existing_check
    ReadRegStr $INSTDIR HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    IfFileExists "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" 0 skip_existing_check
      StrCpy $isPluginAlreadyInstalled "1"
      ; Check which games have the plugin installed (paths are full plugins paths)
      ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
      ReadRegStr $GPBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
      ${If} $MXBikesInstallPath != ""
      ${AndIf} $GPBikesInstallPath != ""
        StrCpy $existingInstallGame "Both"
      ${ElseIf} $MXBikesInstallPath != ""
        StrCpy $existingInstallGame "MXBikes"
      ${ElseIf} $GPBikesInstallPath != ""
        StrCpy $existingInstallGame "GPBikes"
      ${Else}
        ; Legacy installation (pre-multi-game) - was MX Bikes only
        StrCpy $existingInstallGame "MXBikes"
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

  ; Pre-select detected games
  ${If} $isMXBikesPathAutoDetected == "1"
    StrCpy $isMXBikesSelected "1"
  ${EndIf}
  ${If} $isGPBikesPathAutoDetected == "1"
    StrCpy $isGPBikesSelected "1"
  ${EndIf}

  ; Set INSTDIR to first detected game (paths already include \plugins)
  ${If} $MXBikesInstallPath != ""
    StrCpy $INSTDIR "$MXBikesInstallPath"
  ${ElseIf} $GPBikesInstallPath != ""
    StrCpy $INSTDIR "$GPBikesInstallPath"
  ${Else}
    StrCpy $INSTDIR "$PROGRAMFILES64\MX Bikes\plugins"
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

; Existing MXBMRP3 Installation Detected
Function ShowExistingPluginInstallPage
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

  ; Build game list text
  ${If} $existingInstallGame == "Both"
    StrCpy $R1 "MX Bikes and GP Bikes"
  ${ElseIf} $existingInstallGame == "MXBikes"
    StrCpy $R1 "MX Bikes"
  ${ElseIf} $existingInstallGame == "GPBikes"
    StrCpy $R1 "GP Bikes"
  ${Else}
    StrCpy $R1 "Unknown"
  ${EndIf}

  ${NSD_CreateLabel} 0 0 300u 10u "Existing ${PLUGIN_NAME} installation found for: $R1"
  Pop $R2
  ${NSD_CreateLabel} 0 18u 300u 10u "Choose your action."
  Pop $R4
  ${NSD_CreateGroupBox} 0 40u 300u 42u "Action"
  Pop $R1
  ${NSD_CreateRadioButton} 8u 54u 280u 10u "Overwrite (upgrade) existing installation of ${PLUGIN_NAME}"
  Pop $0
  ${NSD_AddStyle} $0 ${WS_GROUP}
  ${NSD_SetState} $0 1
  ${NSD_OnClick} $0 SetPluginInstallActionChoice
  ${NSD_CreateRadioButton} 8u 68u 280u 10u "Remove (uninstall) ${PLUGIN_NAME}"
  Pop $1
  ${NSD_OnClick} $1 SetPluginUninstallActionChoice
  ${NSD_CreateLabel} 0 88u 300u 10u "Note: ${PLUGIN_NAME} settings and data will be preserved."
  Pop $R4
  nsDialogs::Show
FunctionEnd

Function SetPluginInstallActionChoice
  StrCpy $pluginInstallActionChoice "0"
FunctionEnd

Function SetPluginUninstallActionChoice
  StrCpy $pluginInstallActionChoice "1"
FunctionEnd

Function RunUninstaller
  ${If} $pluginInstallActionChoice == "1"
    ; Read uninstaller path from registry (don't rely on $INSTDIR which may have changed)
    ReadRegStr $R0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
    ${If} $R0 != ""
      ExecWait '"$R0"'
    ${EndIf}
    Quit
  ${EndIf}
FunctionEnd

; Game Selection Page
Function ShowGameSelectionPage
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

  ; Info text
  ${NSD_CreateLabel} 0 80u 300u 12u "Select at least one game to install."
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

; Enable/disable Next button based on game selection
Function UpdateNextButtonState
  GetDlgItem $R0 $HWNDPARENT 1  ; 1 = Next/Install button
  ${If} $isMXBikesSelected == "1"
  ${OrIf} $isGPBikesSelected == "1"
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

Function LeaveGameSelectionPage
  ; Get current text from path controls (these are full plugins paths)
  ${NSD_GetText} $MXBikesPathCtrl $MXBikesInstallPath
  ${NSD_GetText} $GPBikesPathCtrl $GPBikesInstallPath

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

  ; Set INSTDIR for uninstaller (paths already include \plugins)
  ${If} $isMXBikesSelected == "1"
    StrCpy $INSTDIR "$MXBikesInstallPath"
  ${Else}
    StrCpy $INSTDIR "$GPBikesInstallPath"
  ${EndIf}

  Return

  abort_validation:
  Abort
FunctionEnd

; Install
Section "Install ${PLUGIN_NAME}" Section_InstallPlugin
  SetAutoClose false

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

    ; Data files
    SetOutPath "$MXBikesInstallPath\mxbmrp3_data"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\tooltips.json"

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

    ; Data files
    SetOutPath "$GPBikesInstallPath\mxbmrp3_data"
    File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\tooltips.json"

    DetailPrint "GP Bikes installation complete."
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

  ; Registry entries
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayName" "${PLUGIN_NAME}"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "Publisher" "${PLUGIN_PUBLISHER}"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayVersion" "${PLUGIN_VERSION}"
  WriteRegDWORD HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoModify" 1
  WriteRegDWORD HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoRepair" 1

  ; Store game root paths (used by uninstaller)
  ${If} $isMXBikesSelected == "1"
    WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath" "$MXBikesInstallPath"
  ${EndIf}
  ${If} $isGPBikesSelected == "1"
    WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath" "$GPBikesInstallPath"
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
  ; Read installed paths from registry
  ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
  ReadRegStr $GPBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"

  ; Handle legacy (pre-multi-game) installations that only have InstallLocation
  ${If} $MXBikesInstallPath == ""
  ${AndIf} $GPBikesInstallPath == ""
    ReadRegStr $MXBikesInstallPath HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
  ${EndIf}
FunctionEnd

Function un.ShowUninstallSelectionPage
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

  ; Info text (no extra padding - matches install page)
  ${NSD_CreateLabel} 0 $R9u 300u 12u "Select at least one game to uninstall."
  Pop $R2

  ; Set initial button state
  Call un.UpdateUninstallButtonState

  nsDialogs::Show
FunctionEnd

Function un.OnMXBikesCheckboxClick
  ${NSD_GetState} $MXBikesCheckbox $isMXBikesSelected
  Call un.UpdateUninstallButtonState
FunctionEnd

Function un.OnGPBikesCheckboxClick
  ${NSD_GetState} $GPBikesCheckbox $isGPBikesSelected
  Call un.UpdateUninstallButtonState
FunctionEnd

; Enable/disable Uninstall button based on game selection
Function un.UpdateUninstallButtonState
  GetDlgItem $R0 $HWNDPARENT 1  ; 1 = Next/Uninstall button
  ${If} $isMXBikesSelected == "1"
  ${OrIf} $isGPBikesSelected == "1"
    EnableWindow $R0 1  ; Enable
  ${Else}
    EnableWindow $R0 0  ; Disable
  ${EndIf}
FunctionEnd

Function un.LeaveUninstallSelectionPage
  ; Check at least one is selected
  ${If} $isMXBikesSelected != "1"
  ${AndIf} $isGPBikesSelected != "1"
    MessageBox MB_OK|MB_ICONEXCLAMATION "Please select at least one game to uninstall from."
    Abort
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
    ; Clear this path from registry
    DeleteRegValue HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
  ${EndIf}

  ; Remove from GP Bikes if selected
  ${If} $isGPBikesSelected == "1"
  ${AndIf} $GPBikesInstallPath != ""
    DetailPrint "Removing from GP Bikes..."
    Delete "$GPBikesInstallPath\${GPBIKES_DLO}"
    RMDir /r "$GPBikesInstallPath\mxbmrp3_data"
    Delete "$GPBikesInstallPath\${PLUGIN_NAME_LC}_uninstall.exe"
    ; Clear this path from registry
    DeleteRegValue HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
  ${EndIf}

  ; Check if anything remains installed
  ReadRegStr $R0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "MXBikesPath"
  ReadRegStr $R1 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "GPBikesPath"
  ${If} $R0 == ""
  ${AndIf} $R1 == ""
    ; Nothing left installed, clean up registry entirely
    DeleteRegKey HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}"
    DetailPrint "All installations removed."
  ${Else}
    ; Update InstallLocation to remaining game
    ${If} $R0 != ""
      WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$R0"
      WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$R0\${PLUGIN_NAME_LC}_uninstall.exe"
    ${Else}
      WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$R1"
      WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$R1\${PLUGIN_NAME_LC}_uninstall.exe"
    ${EndIf}
    DetailPrint "Partial uninstall complete. Some installations remain."
  ${EndIf}

  DetailPrint "Uninstallation complete."
SectionEnd
