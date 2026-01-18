@echo off
setlocal EnableDelayedExpansion

REM --------------------------------------------------------
REM Usage: make_release.bat <version> [/y]
REM    e.g. make_release.bat 1.0.0.0
REM         make_release.bat 1.0.0.0 /y
REM
REM Builds all game configurations and packages for release:
REM   - mxbmrp3.dlo (MX Bikes)
REM   - mxbmrp3_gpb.dlo (GP Bikes)
REM   - mxbmrp3_data/ (shared data)
REM
REM Options:
REM   /y  Skip confirmation when overwriting existing release
REM --------------------------------------------------------

IF "%~1"=="" (
    echo Usage: %~n0 ^<version^> [/y]
    echo Example: %~n0 1.0.0.0
    echo          %~n0 1.0.0.0 /y
    exit /b 1
)

set "VERSION=%~1"
set "SKIP_CONFIRM=0"
if /i "%~2"=="/y" set "SKIP_CONFIRM=1"
set "RELEASE_NAME=mxbmrp3"
set "VERSION_DIR=.\dist\%RELEASE_NAME%-v%VERSION%"
set "STAGING_DIR=.\dist\staging"
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

REM 2) Build all game configurations
echo.
echo === Building MXB-Release ===
msbuild "%SOLUTION%" /p:Configuration=MXB-Release /p:Platform=x64 /m /v:minimal || (
    echo ERROR: MXB-Release build failed
    exit /b 1
)

echo.
echo === Building GPB-Release ===
msbuild "%SOLUTION%" /p:Configuration=GPB-Release /p:Platform=x64 /m /v:minimal || (
    echo ERROR: GPB-Release build failed
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

REM 8) Copy docs
copy ".\README.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\LICENSE" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%
copy ".\THIRD_PARTY_LICENSES.md" "%STAGING_DIR%\" || exit /b %ERRORLEVEL%

REM 9) Generate release README
(
echo # MXBMRP3 v%VERSION%
echo.
echo HUD plugin for MX Bikes and GP Bikes
echo.
echo ## Automatic Installation ^(Recommended^)
echo.
echo Run mxbmrp3-Setup.exe to install the plugin for one or both games.
echo.
echo ## Manual Installation
echo.
echo Copy the appropriate DLO file and mxbmrp3_data/ folder to [Game]/plugins/
echo.
echo - MX Bikes: mxbmrp3.dlo
echo - GP Bikes: mxbmrp3_gpb.dlo
echo.
echo     [Game]\
echo     +---plugins\
echo         +-- mxbmrp3_data\        ^<-- Add this folder
echo         +-- mxbmrp3.dlo          ^<-- Add this ^(MX Bikes only^)
echo         +-- mxbmrp3_gpb.dlo      ^<-- Add this ^(GP Bikes only^)
echo.
echo ## Notes
echo.
echo - Do NOT delete native game files ^(proxy64.dlo, proxy_udp64.dlo, xinput64.dli,
echo   or telemetry64.dlo for GP Bikes^)
echo - Each game needs its own copy of the mxbmrp3_data folder
echo - Settings are stored separately per game in Documents/PiBoSo/[Game]/mxbmrp3/
echo.
echo For more information, see README.md or visit:
echo https://github.com/thomas4f/mxbmrp3
) > "%STAGING_DIR%\README.txt"

REM 10) Zip it up to version directory
echo.
echo Creating ZIP archive...
7z a "%VERSION_DIR%\%RELEASE_NAME%.zip" "%STAGING_DIR%\*" || exit /b %ERRORLEVEL%
echo.
echo ZIP complete: %VERSION_DIR%\%RELEASE_NAME%.zip

REM 11) Build NSIS installer to version directory
echo.
echo Building installer...
makensis -DPLUGIN_VERSION="%VERSION%" -DOUTPUT_DIR="%VERSION_DIR%" mxbmrp3.nsi || exit /b %ERRORLEVEL%
echo.
echo Installer built: %VERSION_DIR%\%RELEASE_NAME%-Setup.exe

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
