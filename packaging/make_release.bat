@echo off
setlocal EnableDelayedExpansion

REM This script lives in packaging\ but operates on the repo root. %~dp0 is its own
REM dir (…\packaging\, trailing backslash); cd to the parent so every relative path
REM below (mxbmrp3\resource.h, dist\, build\, tools\, mxbmrp3.sln) resolves from root
REM regardless of where the script was invoked from.
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%.." || exit /b 1

REM --------------------------------------------------------
REM Usage: make_release.bat [/y]
REM
REM The version is derived automatically to match the DLL: VER_MAJOR/MINOR/PATCH from
REM mxbmrp3\resource.h + the git commit count (the same source the StampVersion
REM pre-build target stamps into the binary), so nothing is typed by hand.
REM
REM Builds all game configurations and packages for release:
REM   - mxbmrp3.dlo (MX Bikes)
REM   - mxbmrp3_gpb.dlo (GP Bikes)
REM   - mxbmrp3_krp.dlo (Kart Racing Pro)
REM   - mxbmrp3_data/ (shared data)
REM
REM Options:
REM   /y  Skip confirmation when overwriting existing release
REM --------------------------------------------------------

set "SKIP_CONFIRM=0"
if /i "%~1"=="/y" set "SKIP_CONFIRM=1"

REM Derive the version: MAJOR.MINOR.PATCH from resource.h + BUILD from the git commit
REM count. This mirrors mxbmrp3.vcxproj's StampVersion target, so the zip/installer
REM version always matches the auto-stamped DLL FILEVERSION.
for /f "tokens=3" %%i in ('findstr /b /c:"#define VER_MAJOR " mxbmrp3\resource.h') do set "VMAJOR=%%i"
for /f "tokens=3" %%i in ('findstr /b /c:"#define VER_MINOR " mxbmrp3\resource.h') do set "VMINOR=%%i"
for /f "tokens=3" %%i in ('findstr /b /c:"#define VER_PATCH " mxbmrp3\resource.h') do set "VPATCH=%%i"
for /f %%i in ('git rev-list --count HEAD') do set "VBUILD=%%i"
if not defined VBUILD set "VBUILD=0"
if not defined VMAJOR (
    echo ERROR: could not read VER_MAJOR/MINOR/PATCH from mxbmrp3\resource.h
    exit /b 1
)
set "VERSION=%VMAJOR%.%VMINOR%.%VPATCH%.%VBUILD%"
set "RELEASE_NAME=mxbmrp3"
REM Absolute so makensis (which resolves paths relative to the .nsi's dir, not CWD)
REM writes/reads the right place from packaging\mxbmrp3.nsi.
set "VERSION_DIR=%CD%\dist\%RELEASE_NAME%-v%VERSION%"
set "STAGING_DIR=%CD%\dist\staging"
set "SOLUTION=mxbmrp3.sln"

echo.
echo === Building release %RELEASE_NAME% v%VERSION% ===

REM 1) Check if release already exists and warn user
if exist "%VERSION_DIR%\%RELEASE_NAME%.zip" (
    if "%SKIP_CONFIRM%"=="0" (
        echo.
        echo WARNING: Release v%VERSION% already exists in %VERSION_DIR%
        echo This will overwrite the existing release files.
        echo.
        set /p "CONFIRM=Continue? [y/N] "
        if /i not "!CONFIRM!"=="y" (
            echo Aborted.
            exit /b 1
        )
    ) else (
        echo.
        echo Overwriting existing release v%VERSION%...
    )
)

REM 2) Build all game configurations via the All-Release meta-config
REM    (build_all/build_all.vcxproj dispatches to mxbmrp3.vcxproj in parallel
REM    for MXB-Release, GPB-Release, KRP-Release — single source of truth with
REM    the Visual Studio dropdown's All-Release option).
echo.
echo === Building All-Release (MXB + GPB + KRP) ===
msbuild "%SOLUTION%" /p:Configuration=All-Release /p:Platform=x64 /m /v:minimal || (
    echo ERROR: All-Release build failed
    exit /b 1
)

REM 3) Clean up staging folder from previous builds
if exist "%STAGING_DIR%" (
    echo.
    echo Cleaning up staging folder...
    rmdir /s /q "%STAGING_DIR%"
)

REM 4) Create version output directory
if not exist "%VERSION_DIR%" mkdir "%VERSION_DIR%"

REM 5) Create staging folder structure
mkdir "%STAGING_DIR%\mxbmrp3_data" 2>nul

REM 6) Copy data directory recursively
echo.
echo Copying data files...
xcopy ".\mxbmrp3_data" "%STAGING_DIR%\mxbmrp3_data" /E /I /Y || exit /b %ERRORLEVEL%

REM 7) Copy the .dlo files from each build
echo.
echo Copying DLO files...
copy ".\build\MXB-Release\mxbmrp3.dlo" "%STAGING_DIR%\mxbmrp3.dlo" || exit /b %ERRORLEVEL%
copy ".\build\GPB-Release\mxbmrp3_gpb.dlo" "%STAGING_DIR%\mxbmrp3_gpb.dlo" || exit /b %ERRORLEVEL%
copy ".\build\KRP-Release\mxbmrp3_krp.dlo" "%STAGING_DIR%\mxbmrp3_krp.dlo" || exit /b %ERRORLEVEL%

REM 8) Copy docs
copy ".\README.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\LICENSE" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\THIRD_PARTY_LICENSES.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%

REM 9) Generate release README from the shared template (single source of truth;
REM    release.yml renders the SAME release_readme.txt so the local and CI zips match).
powershell -NoProfile -Command "(Get-Content '%SCRIPT_DIR%release_readme.txt') -replace '__VERSION__', '%VERSION%' | Set-Content '%STAGING_DIR%\README.txt'" || exit /b %ERRORLEVEL%

REM 10) Zip it up to version directory
REM     Max Deflate (-mx=9) for the smallest archive that Windows Explorer can still
REM     double-click-extract (standard .zip). Matches release.yml so CI and local zips
REM     are identical. (Solid LZMA would be smaller but needs 7-Zip/WinRAR to open.)
echo.
echo Creating ZIP archive...
7z a -tzip -mx=9 "%VERSION_DIR%\%RELEASE_NAME%.zip" "%STAGING_DIR%\*" || exit /b %ERRORLEVEL%
echo.
echo ZIP complete: %VERSION_DIR%\%RELEASE_NAME%.zip

REM 11) Build NSIS installer to version directory
echo.
echo Building installer...
makensis -DPLUGIN_VERSION="%VERSION%" -DOUTPUT_DIR="%VERSION_DIR%" -DPLUGIN_SOURCE_PATH="%STAGING_DIR%" "%SCRIPT_DIR%mxbmrp3.nsi" || exit /b %ERRORLEVEL%
echo.
echo Installer built: %VERSION_DIR%\%RELEASE_NAME%-Setup.exe

REM 11b) Archive debug symbols (.pdb + .map) next to the release. A crash reported by
REM      the analytics dashboard is just "mxbmrp3.dlo+0xNNNN" — an RVA that only maps
REM      to a function against the symbols for THIS exact build. The next rebuild
REM      overwrites build\*-Release\*.pdb, so capture them now (zipped; not shipped to
REM      users). Keep this zip for any build whose version might appear in a crash.
echo.
echo Archiving debug symbols...
7z a "%VERSION_DIR%\%RELEASE_NAME%-symbols-v%VERSION%.zip" ^
    ".\build\MXB-Release\*.pdb" ".\build\MXB-Release\*.map" ^
    ".\build\GPB-Release\*.pdb" ".\build\GPB-Release\*.map" ^
    ".\build\KRP-Release\*.pdb" ".\build\KRP-Release\*.map" || exit /b %ERRORLEVEL%
echo.
echo Symbols archived: %VERSION_DIR%\%RELEASE_NAME%-symbols-v%VERSION%.zip

REM 11c) Generate CycloneDX SBOM (third-party components inside the DLL) from
REM      mxbmrp3\vendor\vendored.json — same generator the release workflow uses.
echo.
echo Generating SBOM...
python tools\gen_sbom.py --version "%VERSION%" --out "%VERSION_DIR%\%RELEASE_NAME%.cdx.json" || exit /b %ERRORLEVEL%
echo SBOM written: %VERSION_DIR%\%RELEASE_NAME%.cdx.json

REM 12) Clean up staging folder
rmdir /s /q "%STAGING_DIR%"

echo.
echo === Release build complete ===
echo.
echo Output:
echo   %VERSION_DIR%\%RELEASE_NAME%.zip         ^(Manual install^)
echo   %VERSION_DIR%\%RELEASE_NAME%-Setup.exe   ^(Installer^)
echo.

pause
