@echo off
setlocal EnableDelayedExpansion

REM --------------------------------------------------------
REM Usage: make_release.bat <version>
REM    e.g. make_release.bat 1.0.0.0
REM --------------------------------------------------------

IF "%~1"=="" (
    echo Usage: %~n0 ^<version^>
    echo Example: %~n0 1.0.0.0
    exit /b 1
)

set "VERSION=%~1"
set "RELEASE_NAME=mxbmrp3"
set "VERSION_DIR=.\dist\%RELEASE_NAME%-v%VERSION%"
set "STAGING_DIR=.\dist\staging"

echo.
echo === Building release %RELEASE_NAME% v%VERSION% ===

REM 1) Check if release already exists and warn user
if exist "%VERSION_DIR%\%RELEASE_NAME%.zip" (
    echo.
    echo WARNING: Release v%VERSION% already exists in %VERSION_DIR%
    echo This will overwrite the existing release files.
    echo.
    set /p "CONFIRM=Continue? [y/N] "
    if /i not "!CONFIRM!"=="y" (
        echo Aborted.
        exit /b 1
    )
)

REM 2) Clean up staging folder from previous builds
if exist "%STAGING_DIR%" (
    echo Cleaning up staging folder...
    rmdir /s /q "%STAGING_DIR%"
)

REM 3) Create version output directory
if not exist "%VERSION_DIR%" mkdir "%VERSION_DIR%"

REM 4) Create staging folder structure
mkdir "%STAGING_DIR%\mxbmrp3_data" 2>nul

REM 5) Copy data directory recursively
xcopy ".\mxbmrp3_data" "%STAGING_DIR%\mxbmrp3_data" /E /I /Y || exit /b %ERRORLEVEL%

REM 6) Copy the .dlo file
copy ".\build\Release\mxbmrp3.dlo" "%STAGING_DIR%\mxbmrp3.dlo" || exit /b %ERRORLEVEL%

REM 7) Copy docs
copy ".\README.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\LICENSE" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\THIRD_PARTY_LICENSES.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%

REM 8) Generate release README
(
echo # MXBMRP3 v%VERSION%
echo.
echo HUD plugin for MX Bikes
echo.
echo ## Installation
echo.
echo 1. Copy the plugin files to your MX Bikes plugins folder:
echo    - Copy mxbmrp3.dlo to [MX Bikes]/plugins/
echo    - Copy the mxbmrp3_data/ folder to [MX Bikes]/plugins/
echo.
echo    Do NOT delete the existing game files (proxy64.dlo, proxy_udp64.dlo,
echo    xinput64.dli^) - these are native MX Bikes files, not old plugin versions.
echo.
echo    Your directory should look like this after installation:
echo.
echo    MX Bikes\
echo    ^|   mxbikes.exe
echo    ^|   ...
echo    ^|
echo    +---plugins\
echo        +-- mxbmrp3_data\        ^<-- Add this folder ^(from release^)
echo        ^|   +-- fonts\          ^<-- Font files ^(.fnt^)
echo        ^|   +-- textures\       ^<-- Texture files ^(.tga^)
echo        ^|   +-- icons\          ^<-- Icon files ^(.tga^)
echo        +-- mxbmrp3.dlo          ^<-- Add this ^(from release^)
echo        +-- proxy_udp64.dlo      ^<-- Keep ^(native game file^)
echo        +-- proxy64.dlo          ^<-- Keep ^(native game file^)
echo        +-- xinput64.dli         ^<-- Keep ^(native game file^)
echo.
echo 2. Launch MX Bikes - the plugin will load and can be configured via the settings menu
echo.
echo For more information, see README.md or visit:
echo https://github.com/thomas4f/mxbmrp3
) > "%STAGING_DIR%\README.txt"

REM 9) Zip it up to version directory
7z a "%VERSION_DIR%\%RELEASE_NAME%.zip" "%STAGING_DIR%\*" || exit /b %ERRORLEVEL%
echo.
echo ZIP complete: %VERSION_DIR%\%RELEASE_NAME%.zip

REM 10) Build NSIS installer to version directory
makensis -DPLUGIN_VERSION="%VERSION%" -DOUTPUT_DIR="%VERSION_DIR%" mxbmrp3.nsi || exit /b %ERRORLEVEL%
echo.
echo Installer built: %VERSION_DIR%\%RELEASE_NAME%-Setup.exe

REM 11) Clean up staging folder
rmdir /s /q "%STAGING_DIR%"

pause
