# ============================================================================
# packaging/msi/build_msi.ps1
# Builds mxbmrp3-Setup.msi from the release staging tree: ONE .msi that opens in
# the Windows display language (ten languages, English otherwise).
#
#   pwsh packaging/msi/build_msi.ps1 -Staging <dir> -Version 1.35.2.1234 -Out <dir>
#
# Windows only (WiX's cabinet and MSI back end are native Windows code).
# Needs the WiX .NET tool and two extensions, pinned in release.yml:
#   dotnet tool install --global wix --version 5.0.2
#   wix extension add -g WixToolset.UI.wixext/5.0.2 WixToolset.Util.wixext/5.0.2
#
# HOW ONE MSI SPEAKS TEN LANGUAGES. An MSI database has one language. The
# standard way to carry more (the one Windows' own installers use) is a
# language TRANSFORM per extra language, stored inside the package under its
# decimal LCID, plus every LCID listed in the package's Template summary
# property: Windows Installer then applies the transform matching the user's
# display language by itself. So: build the package once per culture with the
# SAME ProductCode, diff each against English into a transform, and embed the
# transforms into the English one. Maintenance and uninstall reuse the transform
# applied at install, which is how the uninstaller "remembers" the language.
#
# Why WiX 5 and not 6+: WiX 6 binaries come under the Open Source Maintenance
# Fee EULA; WiX 5 is plain MS-RL. Revisit when 5 stops building on the runner.
# ============================================================================
param(
    [Parameter(Mandatory)] [string] $Staging,
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$here = $PSScriptRoot
$work = Join-Path ([IO.Path]::GetTempPath()) ("mxbmrp3-msi-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work, $Out | Out-Null
$Staging = (Resolve-Path $Staging).Path
$Out = (Resolve-Path $Out).Path

function Invoke-Checked([string] $what, [scriptblock] $cmd) {
    & $cmd
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

# English first: it is the base package the others become transforms of.
$cultures = [ordered]@{
    'en-US' = 1033; 'de-DE' = 1031; 'es-ES' = 3082; 'fr-FR' = 1036; 'it-IT' = 1040
    'nl-NL' = 1043; 'nb-NO' = 1044; 'pt-BR' = 1046; 'sv-SE' = 1053; 'tr-TR' = 1055
}

# The one custom action (ca/check_folders.cpp), MSVC x64.
Invoke-Checked 'configure custom action' { cmake -S "$here\ca" -B "$work\ca" -G 'Visual Studio 17 2022' -A x64 }
Invoke-Checked 'build custom action' { cmake --build "$work\ca" --config Release -- /m /v:minimal }
$caDll = "$work\ca\Release\mxbmrp3_msi_ca.dll"
if (-not (Test-Path $caDll)) { throw "custom action DLL not built: $caDll" }

Invoke-Checked 'gen_payload' { python "$here\gen_payload.py" $Staging "$work\payload.wxs" }
Invoke-Checked 'gen_wxl' { python "$here\gen_wxl.py" (Join-Path $here '..\lang') "$work\loc" }
foreach ($c in $cultures.Keys) {
    if (-not (Test-Path "$work\loc\$c.wxl")) { throw "gen_wxl wrote no $c.wxl" }
}

# One ProductCode for every language build of this release, or the transforms
# would describe a different product and fail to apply.
$productCode = [guid]::NewGuid().ToString('B').ToUpper()
Write-Host "ProductCode $productCode"

foreach ($c in $cultures.Keys) {
    Invoke-Checked "wix build $c" {
        wix build -arch x64 -culture $c -loc "$work\loc\$c.wxl" `
            -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext `
            -d "Version=$Version" -d "ProductCode=$productCode" -d "CaDll=$caDll" `
            -cc "$work\cab" `
            "$here\mxbmrp3.wxs" "$here\ui.wxs" "$work\payload.wxs" `
            -o "$work\$c.msi"
    }
}

$final = Join-Path $Out 'mxbmrp3-Setup.msi'
Copy-Item "$work\en-US.msi" $final -Force

foreach ($c in $cultures.Keys) {
    if ($c -eq 'en-US') { continue }
    $lcid = $cultures[$c]
    Invoke-Checked "wix msi transform $c" {
        wix msi transform -t language -out "$work\$lcid.mst" "$work\en-US.msi" "$work\$c.msi"
    }
}

# Embed the transforms and list the languages. Windows Installer's COM API,
# late-bound (it has no type library PowerShell can bind to). Arguments are
# unwrapped from PowerShell's PSObject first: a wrapped COM record reaches
# InvokeMember as the wrong VARIANT type and fails with DISP_E_TYPEMISMATCH.
function ConvertTo-ComArgs([object[]] $argv) {
    , [object[]]@($argv | ForEach-Object { if ($_ -is [psobject]) { $_.psobject.BaseObject } else { $_ } })
}
function Invoke-Com($obj, [string] $name, [object[]] $argv = @()) {
    $obj.GetType().InvokeMember($name, [Reflection.BindingFlags]::InvokeMethod, $null, $obj, (ConvertTo-ComArgs $argv))
}
function Set-ComProp($obj, [string] $name, [object[]] $argv) {
    $obj.GetType().InvokeMember($name, [Reflection.BindingFlags]::SetProperty, $null, $obj, (ConvertTo-ComArgs $argv)) | Out-Null
}
$installer = New-Object -ComObject WindowsInstaller.Installer
Write-Host 'Embedding language transforms'
$db = Invoke-Com $installer 'OpenDatabase' @($final, [int]1)   # msiOpenDatabaseModeTransact
$view = Invoke-Com $db 'OpenView' @('SELECT `Name`, `Data` FROM `_Storages`')
Invoke-Com $view 'Execute' | Out-Null
foreach ($c in $cultures.Keys) {
    if ($c -eq 'en-US') { continue }
    $lcid = $cultures[$c]
    $rec = Invoke-Com $installer 'CreateRecord' @([int]2)
    Set-ComProp $rec 'StringData' @([int]1, [string]$lcid)
    Invoke-Com $rec 'SetStream' @([int]2, [string]"$work\$lcid.mst") | Out-Null
    Invoke-Com $view 'Modify' @([int]3, $rec) | Out-Null   # msiViewModifyAssign
}
Invoke-Com $view 'Close' | Out-Null
Write-Host 'Listing the languages'
$summary = $db.GetType().InvokeMember('SummaryInformation', [Reflection.BindingFlags]::GetProperty, $null, $db, [object[]]@([int]1))
Set-ComProp $summary 'Property' @([int]7, [string]('x64;' + (($cultures.Values | ForEach-Object { "$_" }) -join ',')))
Invoke-Com $summary 'Persist' | Out-Null
Invoke-Com $db 'Commit' | Out-Null
[Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null
[Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null

Remove-Item -Recurse -Force $work
$size = [math]::Round((Get-Item $final).Length / 1MB, 1)
Write-Host "Built $final ($size MB, $($cultures.Count) languages)"
