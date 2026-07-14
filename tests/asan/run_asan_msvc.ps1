<#
============================================================================
 tests/asan/run_asan_msvc.ps1
 Faithful memory-safety test: run the REAL plugin DLL under MSVC AddressSanitizer,
 driven through the real DLL-boundary callbacks by callback_fuzzer.cpp. This is
 Layer 2 from README.md — it covers the live PluginData / StatsManager map
 lifecycle that the native harness cannot.

 WINDOWS ONLY. Requires Visual Studio 2022 (x64) with the "C++ AddressSanitizer"
 individual component installed. Run from a "x64 Native Tools Command Prompt for
 VS 2022" (so cl / msbuild are on PATH), or let the script find vcvars.

   # 1) Build the plugin DLL with ASan first (MXB-Debug|x64 — there is no plain
   #    'Debug' config that maps the plugin project), e.g.:
   #    msbuild mxbmrp3.sln /p:Configuration=MXB-Debug /p:Platform=x64 `
   #      /p:EnableASAN=true /p:BasicRuntimeChecks=Default /p:LinkIncremental=false
   #    (EnableASAN forces the DLL to the dynamic debug CRT /MDd via
   #     mxbmrp3/Directory.Build.targets, so it shares the ASan runtime with the fuzzer)
   # 2) Then drive it under ASan (DLL output is build\MXB-Debug\mxbmrp3.dlo):
   pwsh tests/asan/run_asan_msvc.ps1 -Dll .\build\MXB-Debug\mxbmrp3.dlo -Iterations 5000000

 Exit code is the fuzzer's: 0 = survived clean; non-zero = ASan caught a fault
 (the console shows the writing + allocating stacks — the culprit).
============================================================================
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Dll,                               # path to the ASan-instrumented mxbmrp3.dlo
    [int]$Iterations = 5000000,
    [string]$OutDir = "$PSScriptRoot\build-msvc"
)
$ErrorActionPreference = "Stop"

if (-not (Test-Path $Dll)) { throw "DLL not found: $Dll  (build it with /fsanitize=address first — see README.md)" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Ensure cl.exe is available; if not, source vcvars from a VS2022 install.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "cl.exe not on PATH and vswhere not found. Run from a 'x64 Native Tools' prompt." }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsPath" }
    Write-Host "Importing MSVC environment from $vcvars ..."
    cmd /c "`"$vcvars`" && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
}

$fuzzerSrc = Join-Path $PSScriptRoot "..\integration\callback_fuzzer.cpp"
$fuzzerExe = Join-Path $OutDir "callback_fuzzer.exe"

# /MDd (dynamic debug CRT) matches the DLL, which EnableASAN forces to /MDd via
# mxbmrp3/Directory.Build.targets, so both modules share the single
# clang_rt.asan_dynamic-x86_64.dll (see README's ASan-runtime note). The static
# runtime (/MTd) is per-module and fails to load across the fuzzer->DLL boundary on
# recent MSVC toolsets with error 127 (ERROR_PROC_NOT_FOUND).
Write-Host "Building boundary fuzzer with /fsanitize=address (/MDd to match the DLL) ..."
& cl /nologo /std:c++17 /MDd /EHsc /Zi /fsanitize=address `
     $fuzzerSrc /Fe:$fuzzerExe /Fo:"$OutDir\" /Fd:"$OutDir\" | Write-Host
if ($LASTEXITCODE -ne 0) { throw "fuzzer build failed ($LASTEXITCODE)" }

# The /MDd fuzzer + plugin resolve the dynamic ASan runtime
# (clang_rt.asan_dynamic-x86_64.dll) AND the dynamic debug CRT DLLs
# (vcruntime140d/msvcp140d from the VC toolset, ucrtbased from the Windows SDK) at
# load time. A vcvars/'x64 Native Tools' shell has all of these on PATH, so no
# staging is needed here (the CI job stages them explicitly because its shell is
# leaner). If a run fails at load with error 126 naming a *d.dll, that dependency
# isn't on PATH -- open this from a full Native Tools prompt.
$env:ASAN_OPTIONS = "abort_on_error=1:halt_on_error=1:print_stats=1"

Write-Host "Running $Iterations iterations against $Dll under ASan ..."
& $fuzzerExe $Dll $Iterations
$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "`nASAN MSVC FUZZER: SURVIVED CLEAN ($Iterations iterations)." -ForegroundColor Green
} else {
    Write-Host "`nASAN MSVC FUZZER: FAULT DETECTED (exit $rc) — see the stacks above for the culprit." -ForegroundColor Red
}
exit $rc
