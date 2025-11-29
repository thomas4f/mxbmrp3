@echo off
setlocal

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
set "RELEASE_NAME=mxbmrp3-v%VERSION%"

echo.
echo === Building release %RELEASE_NAME% ===

REM 1) Create folder structure
mkdir ".\dist\%RELEASE_NAME%\mxbmrp3_data" 2>nul

REM 2) Copy data directory recursively
xcopy ".\mxbmrp3_data" ".\dist\%RELEASE_NAME%\mxbmrp3_data" /E /I /Y || exit /b %ERRORLEVEL%

REM 3) Copy the .dlo file
copy ".\build\Release\mxbmrp3.dlo" ".\dist\%RELEASE_NAME%\mxbmrp3.dlo" || exit /b %ERRORLEVEL%

REM 4) Copy docs
copy ".\README.md" ".\dist\%RELEASE_NAME%\" || exit /b %ERRORLEVEL%
copy ".\LICENSE" ".\dist\%RELEASE_NAME%\" || exit /b %ERRORLEVEL%

REM 5) Generate release README
(
echo # MXBMRP3 v%VERSION%
echo.
echo HUD plugin for MX Bikes
echo.
echo ## Installation
echo.
echo 1. Extract all files to your MX Bikes plugins folder.
echo    Your directory should look like this:
echo.
echo    MX Bikes\
echo    ^|   mxbikes.exe
echo    ^|   ...
echo    ^|
echo    +---plugins\
echo        ^|   mxbmrp3.dlo
echo        ^|
echo        +---mxbmrp3_data\
echo                *.tga
echo                *.fnt
echo.
echo 2. Launch MX Bikes - the HUD will automatically appear during sessions
echo.
echo For more information, see README.md or visit:
echo https://github.com/thomas4f/mxbmrp3
) > ".\dist\%RELEASE_NAME%\README.txt"

REM 6) Zip it up
7z a ".\dist\%RELEASE_NAME%.zip" ".\dist\%RELEASE_NAME%\*" || exit /b %ERRORLEVEL%
echo.
echo ZIP complete: dist\%RELEASE_NAME%.zip

REM 7) Build NSIS installer
makensis -DPLUGIN_VERSION="%VERSION%" mxbmrp3.nsi || exit /b %ERRORLEVEL%
echo.
echo Installer built: dist\%RELEASE_NAME%-Setup.exe

pause
