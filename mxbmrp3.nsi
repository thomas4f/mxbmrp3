; mxbmrp3.nsi
; Build for 64-bit using https://github.com/negrutiu/nsis

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define PLUGIN_NAME "MXBMRP3"
!define PLUGIN_NAME_LC "mxbmrp3"
!define PLUGIN_PUBLISHER "thomas4f"
!define MXBIKES_STEAM_APPID "655500"
!define PLUGIN_SOURCE_PATH "dist\mxbmrp3"
!ifndef PLUGIN_VERSION
  !define PLUGIN_VERSION 1.0.0.0
  ;!error "PLUGIN_VERSION is not defined. Please define it before building."
!endif

!define VC_REDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!define VC_REDIST_EXE_PATH "$TEMP\vc_redist.x64.exe"
!define REG_UNINSTALL_KEY_PATH "Software\Microsoft\Windows\CurrentVersion\Uninstall"

; General Settings
Name "${PLUGIN_NAME}"

RequestExecutionLevel admin
SetCompressor /SOLID LZMA
Target AMD64-Unicode
OutFile "dist\${PLUGIN_NAME_LC}-Setup.exe"

; Variables
Var pluginInstallActionChoice
Var isPluginAlreadyInstalled
Var MXBikesInstallPath
Var isMXBikesPathAutoDetected

; Welcome to MXBMRP3 Setup
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation or upgrade of ${PLUGIN_NAME} for MX Bikes.$\n$\nIt will try to find your MX Bikes installation automatically.$\nIf it can't, you'll be asked to locate it.$\n$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

; Existing MXBMRP3 Installation Detected
Page Custom ShowExistingPluginInstallPage RunUninstaller

; Choose Install Location
!define MUI_DIRECTORYPAGE_TEXT_TOP "${PLUGIN_NAME} should be installed in the plugins folder of your MX Bikes installation."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "Location of MX Bikes plugin folder"
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE ValidatePluginInstallPath
!define MUI_PAGE_CUSTOMFUNCTION_SHOW UpdateDirectoryPageText
!insertmacro MUI_PAGE_DIRECTORY

; Installing
!insertmacro MUI_PAGE_INSTFILES

; Completing MXBMRP3 Setup
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been installed on your computer.$\n$\nYour settings and data will be stored in:$\n$\n$PROFILE\Documents\PiBoSo\MX Bikes\${PLUGIN_NAME_LC}\$\n$\nClick finish to close Setup."
!insertmacro MUI_PAGE_FINISH

; Uninstalling
!insertmacro MUI_UNPAGE_INSTFILES

; Completing MXBMRP3 Uninstall
!define MUI_FINISHPAGE_TEXT "${PLUGIN_NAME} has been uninstalled from your computer.$\n$\nTo remove all settings and data, manually delete:$\n$\n$PROFILE\Documents\PiBoSo\MX Bikes\${PLUGIN_NAME_LC}\$\n$\nClick Finish to close Setup."
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

; .onInit: Determine registry view & locate MX Bikes
Function .onInit
  SetRegView 64

  ; Check for an existing MXBMRP3 install in the registry
  StrCpy $isPluginAlreadyInstalled "0"
  ReadRegStr $0 HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString"
  IfFileExists "$0" 0 skip_installed
    StrCpy $isPluginAlreadyInstalled "1"
    ReadRegStr $INSTDIR HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation"
    ; verify that the uninstaller exists
    IfFileExists "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe" +2
      StrCpy $isPluginAlreadyInstalled "0"
  skip_installed:

  ${If} $isPluginAlreadyInstalled == "1"
    Return
  ${EndIf}

  ; No existing plugin registry entry, auto-detect MX Bikes or fallback
  ReadRegStr $MXBikesInstallPath HKLM64 "Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App ${MXBIKES_STEAM_APPID}" "InstallLocation"
  ${If} $MXBikesInstallPath != ""
    IfFileExists "$MXBikesInstallPath\mxbikes.exe" 0 invalid_MXBikes
      StrCpy $isMXBikesPathAutoDetected "1"
      Goto set_default
    invalid_MXBikes:
      StrCpy $MXBikesInstallPath ""
      StrCpy $isMXBikesPathAutoDetected "0"
  ${Else}
    StrCpy $isMXBikesPathAutoDetected "0"
  ${EndIf}

  set_default:
  ${If} $isMXBikesPathAutoDetected == "0"
    StrCpy $MXBikesInstallPath "$PROGRAMFILES64\MX Bikes"
	; verify that the mxbikes.exe exists
    IfFileExists "$MXBikesInstallPath\mxbikes.exe" 0 no_detected
      StrCpy $isMXBikesPathAutoDetected "1"
  no_detected:
  ${EndIf}

  StrCpy $INSTDIR "$MXBikesInstallPath\plugins"
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
  ${NSD_CreateLabel} 0 0 300u 10u "Existing ${PLUGIN_NAME} install location:"
  Pop $R2
  ${NSD_CreateText} 0 14u 300u 12u "$INSTDIR"
  Pop $R3
  System::Call user32::SendMessage(i$R3,i${EM_SETREADONLY},i1,i0)
  ${NSD_CreateLabel} 0 33u 300u 10u "Choose your action."
  Pop $R4
  ${NSD_CreateGroupBox} 0 70u 300u 42u "Action"
  Pop $R1
  ${NSD_CreateRadioButton} 8u 84u 280u 10u "Overwrite (upgrade) existing installation of ${PLUGIN_NAME}"
  Pop $0
  ${NSD_AddStyle} $0 ${WS_GROUP}
  ${NSD_SetState} $0 1
  ${NSD_OnClick} $0 SetPluginInstallActionChoice
  ${NSD_CreateRadioButton} 8u 98u 280u 10u "Remove (uninstall) ${PLUGIN_NAME}"
  Pop $1
  ${NSD_OnClick} $1 SetPluginUninstallActionChoice
  ${NSD_CreateLabel} 0 115u 300u 10u "Note: ${PLUGIN_NAME} settings and data will be preserved."
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
    ExecWait "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
    Quit
  ${EndIf}
FunctionEnd

; Update directory page text
Function UpdateDirectoryPageText
  ; choose the text to display
  ${If} $isPluginAlreadyInstalled == "1"
    StrCpy $R0 "✔ Using existing ${PLUGIN_NAME} location.$\n$\nClick Browse if you want to change the location. Click Install to start the installation."
  ${ElseIf} $isMXBikesPathAutoDetected == "1"
    StrCpy $R0 "✔ MX Bikes installation location was found automatically!$\n$\nClick Browse if you want to change the location. Click Install to start the installation."
  ${Else}
    StrCpy $R0 "✖ Could not auto-detect the installation location of MX Bikes.$\n$\nClick Browse to change the location. Click Install to start the installation."
   ${EndIf}

  ; create the control
  System::Call 'user32::CreateWindowEx(i 0, t"STATIC", t"$R0", \
    i${WS_CHILD}|${WS_VISIBLE}|${SS_LEFT}, \
    i23u, i99u, i450u, i44u, \
    i$HWNDPARENT, i1001, i0, i0) i.r1'

  ; ask the parent dialog for its font
  System::Call 'user32::SendMessage(i$HWNDPARENT, i${WM_GETFONT}, i0, i0) i.r2'

  ; use that font
  System::Call 'user32::SendMessage(ir1, i${WM_SETFONT}, ir2, i1) i .r3'
FunctionEnd

; Validate install location
Function ValidatePluginInstallPath
  ; Check if folder is named "plugins"
  ${GetFileName} "$INSTDIR" $R0
  StrCmp $R0 "plugins" check_mxbikes show_warning

  check_mxbikes:
  ; Check if parent contains mxbikes.exe
  IfFileExists "$INSTDIR\..\mxbikes.exe" valid_path show_warning

  show_warning:
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "This does not appear to be the MX Bikes plugins folder. Continue anyway?" IDYES valid_path IDNO abort_install

  valid_path:
  Return

  abort_install:
  Abort
FunctionEnd

; Ensure vc_redist is installed
Function CheckVCRedistInstalled
  SetRegView 64
  StrCpy $1 "0"
  StrCpy $2 0
  LoopVCRT:
    EnumRegKey $3 HKLM64 "SOFTWARE\Microsoft\VisualStudio" $2
    StrCmp $3 "" done_vcrt
    ReadRegDWORD $0 HKLM64 "SOFTWARE\Microsoft\VisualStudio\$3\VC\Runtimes\x64" "Installed"
    ${If} $0 == 1
      StrCpy $1 "1"
      Return
    ${EndIf}
    IntOp $2 $2 + 1
    Goto LoopVCRT
  done_vcrt:
FunctionEnd

; Offer to install vc_redist
Function PromptInstallVCRedist
  Call CheckVCRedistInstalled
  ${If} $1 == "0"
    MessageBox MB_YESNO|MB_ICONQUESTION \
      "Microsoft Visual C++ Redistributable is missing. Install now?" \
      IDNO skip_redist_install
    Call DownloadAndInstallVCRedist
  skip_redist_install:
  ${EndIf}
FunctionEnd

; Install vc_redist
Function DownloadAndInstallVCRedist
  retry_download:
    DetailPrint "Downloading vc_redist.x64.exe..."
    nsisdl::download "${VC_REDIST_URL}" "${VC_REDIST_EXE_PATH}"
    Pop $0
    StrCmp $0 "success" +2
      MessageBox MB_RETRYCANCEL|MB_ICONSTOP \
        "Failed to download vc_redist. Retry?" IDRETRY retry_download

    DetailPrint "Installing vc_redist..."
    ExecWait '"${VC_REDIST_EXE_PATH}" /install /quiet /norestart' $1
    Delete "${VC_REDIST_EXE_PATH}"

    ; treat 0 or 3010 (reboot required) as OK
    IntCmp $1 0 +3
    IntCmp $1 3010 +2
      MessageBox MB_ICONSTOP \
        "vc_redist installer returned exit code $1. Retry?" IDRETRY retry_download
FunctionEnd

; Install
Section "Install ${PLUGIN_NAME}" Section_InstallPlugin
  SetAutoClose false

  DetailPrint "Installing ${PLUGIN_NAME}..."
  SetOutPath "$INSTDIR"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3.dlo"
  SetOutPath "$INSTDIR\mxbmrp3_data"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\EnterSansman-Italic.fnt"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\FuzzyBubbles-Regular.fnt"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\Tiny5-Regular.fnt"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\pitboard_hud.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\RobotoMono-Regular.fnt"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\RobotoMono-Bold.fnt"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\pointer.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\gear-circle.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\speedo_widget.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\tacho_widget.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\radar_hud.tga"
  File "${PLUGIN_SOURCE_PATH}\mxbmrp3_data\radar_sector.tga"
  Call PromptInstallVCRedist
  WriteUninstaller "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayName" "${PLUGIN_NAME}"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "UninstallString" "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "Publisher" "${PLUGIN_PUBLISHER}"
  WriteRegStr HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "DisplayVersion" "${PLUGIN_VERSION}"
  WriteRegDWORD HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoModify" 1
  WriteRegDWORD HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}" "NoRepair" 1
  DetailPrint "Installation complete."
SectionEnd

; Uninstall
Section "Uninstall"
  SetAutoClose false

  DetailPrint "Removing ${PLUGIN_NAME} files..."
  Delete "$INSTDIR\mxbmrp3.dlo"
  Delete "$INSTDIR\mxbmrp3_data\EnterSansman-Italic.fnt"
  Delete "$INSTDIR\mxbmrp3_data\FuzzyBubbles-Regular.fnt"
  Delete "$INSTDIR\mxbmrp3_data\Tiny5-Regular.fnt"
  Delete "$INSTDIR\mxbmrp3_data\pitboard_hud.tga"
  Delete "$INSTDIR\mxbmrp3_data\RobotoMono-Regular.fnt"
  Delete "$INSTDIR\mxbmrp3_data\RobotoMono-Bold.fnt"
  Delete "$INSTDIR\mxbmrp3_data\pointer.tga"
  Delete "$INSTDIR\mxbmrp3_data\gear-circle.tga"
  Delete "$INSTDIR\mxbmrp3_data\speedo_widget.tga"
  Delete "$INSTDIR\mxbmrp3_data\tacho_widget.tga"
  Delete "$INSTDIR\mxbmrp3_data\radar_hud.tga"
  Delete "$INSTDIR\mxbmrp3_data\radar_sector.tga"
  RMDir "$INSTDIR\mxbmrp3_data"
  Delete "$INSTDIR\${PLUGIN_NAME_LC}_uninstall.exe"
  DeleteRegKey HKLM64 "${REG_UNINSTALL_KEY_PATH}\${PLUGIN_NAME}"
  DetailPrint "Uninstallation complete."
SectionEnd
