# ============================================================================
# packaging/msi/test_msi.ps1
# Installs and uninstalls mxbmrp3-Setup.msi on REAL Windows Installer and
# asserts the on-disk and registry outcomes, the MSI's counterpart of
# tests/integration/run_installer_test.sh (NSIS under Wine).
#
#   pwsh packaging/msi/test_msi.ps1 -Msi <mxbmrp3-Setup.msi> -Staging <dir> -Version <v>
#
# DESTRUCTIVE AND ADMIN-ONLY: it plants fake games under Program Files and C:\,
# writes Steam and NSIS uninstall keys, and deletes Documents\PiBoSo\<game>\
# mxbmrp3. It is meant for a throwaway CI runner (release.yml's msi job) and
# refuses to run anywhere a real game is installed.
#
# Silent (/qn) runs drive the same property logic the dialogs set (each
# dialog choice IS a public property, listed in mxbmrp3.wxs), so this covers
# everything but the pages themselves, which are checked by hand.
# ============================================================================
param(
    [Parameter(Mandatory)] [string] $Msi,
    [Parameter(Mandatory)] [string] $Staging,
    [Parameter(Mandatory)] [string] $Version
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Msi = (Resolve-Path $Msi).Path
$Staging = (Resolve-Path $Staging).Path
$logs = Join-Path $env:RUNNER_TEMP 'msi-logs'
if (-not $env:RUNNER_TEMP) { $logs = Join-Path ([IO.Path]::GetTempPath()) 'msi-logs' }
New-Item -ItemType Directory -Force -Path $logs | Out-Null

$script:fails = 0
$script:run = 0
function Pass([string] $m) { Write-Host "PASS: $m" }
function Fail([string] $m) { Write-Host "FAIL: $m"; $script:fails++ }
function Check([bool] $ok, [string] $m) { if ($ok) { Pass $m } else { Fail $m } }
function Has([string] $p, [string] $m) { Check (Test-Path -LiteralPath $p) $m }
function Hasnt([string] $p, [string] $m) { Check (-not (Test-Path -LiteralPath $p)) $m }

function Msiexec([string] $what, [string[]] $argv) {
    $script:run++
    $log = Join-Path $logs ("{0:D2}-{1}.log" -f $script:run, ($what -replace '[^\w]+', '-'))
    $p = Start-Process msiexec.exe -ArgumentList ($argv + @('/qn', '/norestart', '/l*v', "`"$log`"")) -Wait -PassThru
    Check ($p.ExitCode -eq 0) "$what (msiexec exit $($p.ExitCode), log $log)"
}
function Install([string] $what, [string[]] $props = @()) { Msiexec $what (@('/i', "`"$Msi`"") + $props) }
function Uninstall([string] $what, [string[]] $props = @()) { Msiexec $what (@('/x', "`"$Msi`"") + $props) }

$pf = $env:ProgramW6432
$docs = [Environment]::GetFolderPath('MyDocuments')
$mxbGame = Join-Path $pf 'MX Bikes'                 # found under Program Files
$gpbGame = 'C:\MxbmrpTest\Steam\GP Bikes'           # found only through Steam's key
$krpGame = 'C:\MxbmrpTest\Other\KRP'                # found only when given
$mxb = "$mxbGame\plugins"; $gpb = "$gpbGame\plugins"; $krp = "$krpGame\plugins"
$mxbData = "$docs\PiBoSo\MX Bikes\mxbmrp3"
$gpbData = "$docs\PiBoSo\GP Bikes\mxbmrp3"
$krpData = "$docs\PiBoSo\Kart Racing Pro\mxbmrp3"
$nsisKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\MXBMRP3'
$steamGpb = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 848050'

foreach ($real in @("$pf\GP Bikes", "$pf\Kart Racing Pro", $mxbGame, $mxbData, $gpbData, $krpData)) {
    if (Test-Path $real) { throw "refusing to run: $real exists (this test deletes game and player folders)" }
}

foreach ($g in @(@($mxbGame, 'mxbikes.exe'), @($gpbGame, 'gpbikes.exe'), @($krpGame, 'kart.exe'))) {
    New-Item -ItemType Directory -Force -Path $g[0] | Out-Null
    Set-Content -Path (Join-Path $g[0] $g[1]) -Value 'game'
}
New-Item -Force -Path $steamGpb | Out-Null
Set-ItemProperty -Path $steamGpb -Name InstallLocation -Value $gpbGame

function Plant-Data([string] $dir) {
    New-Item -ItemType Directory -Force -Path "$dir\profiles" | Out-Null
    Set-Content -Path "$dir\mxbmrp3.ini" -Value '[General]'
    Set-Content -Path "$dir\profiles\stats.json" -Value '{}'
}
function Prefs([string] $plugins) {
    $f = "$plugins\mxbmrp3_data\install_prefs.ini"
    if (Test-Path $f) { return (Get-Content -Raw $f) } else { return '' }
}
function Arp() {
    Get-ChildItem 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
        Get-ItemProperty | Where-Object { $_.PSObject.Properties['DisplayName'] -and $_.DisplayName -eq 'MXBMRP3' -and $_.PSChildName -like '{*' }
}
# Every staged file must land under <plugins> byte for byte, whichever earlier
# content sat there. The licences go inside mxbmrp3_data, as with NSIS.
function Check-Payload([string] $plugins, [string] $dlo, [string] $label) {
    $bad = 0
    $expect = @{ $dlo = "$Staging\$dlo"
                 'mxbmrp3_data\LICENSE' = "$Staging\LICENSE"
                 'mxbmrp3_data\THIRD_PARTY_LICENSES.md' = "$Staging\THIRD_PARTY_LICENSES.md" }
    Get-ChildItem -Recurse -File "$Staging\mxbmrp3_data" | ForEach-Object {
        $expect[$_.FullName.Substring($Staging.Length + 1)] = $_.FullName
    }
    foreach ($rel in $expect.Keys) {
        $dst = Join-Path $plugins $rel
        if (-not (Test-Path -LiteralPath $dst) -or
            (Get-FileHash -LiteralPath $dst).Hash -ne (Get-FileHash -LiteralPath $expect[$rel]).Hash) {
            if ($bad -lt 5) { Write-Host "   differs or missing: $dst" }
            $bad++
        }
    }
    Check ($bad -eq 0) "$label payload matches staging ($($expect.Count) files)"
}

# ---------------------------------------------------------------------------
Write-Host '== 0. package'
$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($Msi, 0))
$si = $db.GetType().InvokeMember('SummaryInformation', 'GetProperty', $null, $db, @(0))
$template = $si.GetType().InvokeMember('Property', 'GetProperty', $null, $si, @(7))
Check ($template -eq 'x64;1033,1031,3082,1036,1040,1043,1044,1046,1053,1055') "Template lists the ten languages ($template)"
$view = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db, @('SELECT `Name` FROM `_Storages`'))
$view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
$storages = @()
while ($rec = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)) {
    $storages += $rec.GetType().InvokeMember('StringData', 'GetProperty', $null, $rec, @(1))
}
$view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null) | Out-Null
Check ((@($storages | Sort-Object) -join ',') -eq '1031,1036,1040,1043,1044,1046,1053,1055,3082') "nine language transforms embedded ($($storages -join ','))"
[Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null

# ---------------------------------------------------------------------------
Write-Host '== 1. install: detected games, analytics opted out'
Install 'install detected, opt-out' @('ANALYTICS=0')
Has   "$mxb\mxbmrp3.dlo"        'MX Bikes installed (Program Files detection)'
Has   "$gpb\mxbmrp3_gpb.dlo"    'GP Bikes installed (Steam key detection)'
Hasnt "$mxb\mxbmrp3_gpb.dlo"    'only the matching DLO per game'
Hasnt "$krp"                    'Kart Racing Pro untouched (not detected)'
Check-Payload $mxb 'mxbmrp3.dlo' 'MX Bikes'
Check-Payload $gpb 'mxbmrp3_gpb.dlo' 'GP Bikes'
$p = Prefs $mxb
Check ($p -match '(?m)^language=en\s*$') 'install_prefs.ini: language=en'
Check ($p -match '(?m)^analyticsOptOut=1\s*$') 'install_prefs.ini: analyticsOptOut=1 on opt-out'
Check ($p -match "(?m)^stamp=$([regex]::Escape($Version))\s*$") "install_prefs.ini: stamp=$Version"
Check ((Prefs $gpb) -match '(?m)^analyticsOptOut=1\s*$') 'opt-out written for every game'
$arp = @(Arp)
Check ($arp.Count -eq 1) 'one Add/Remove Programs entry'
if ($arp.Count -eq 1) {
    Check ($arp[0].DisplayVersion -like ($Version -replace '\.\d+$', '') + '*') "ARP version $($arp[0].DisplayVersion)"
    Check ($arp[0].Publisher -eq 'thomas4f') 'ARP publisher'
    Check ($arp[0].NoRepair -eq 1) 'ARP NoRepair, as NSIS'
}
Check ((Get-ItemProperty 'HKLM:\Software\MXBMRP3').MXBPlugins -eq "$mxb\") 'MX Bikes folder remembered'

Write-Host '== 2. plain uninstall keeps player data, removes the whole payload'
Plant-Data $mxbData
Set-Content -Path "$mxb\mxbmrp3_data\added-by-updater.txt" -Value 'x'
Uninstall 'uninstall'
Hasnt "$mxb\mxbmrp3.dlo"        'MX Bikes DLO removed'
Hasnt "$mxb\mxbmrp3_data"       'mxbmrp3_data removed whole (incl. a file the MSI did not install)'
Hasnt "$gpb\mxbmrp3_gpb.dlo"    'GP Bikes DLO removed'
Has   "$mxbGame\mxbikes.exe"    'game itself untouched'
Has   "$mxbData\mxbmrp3.ini"    'player settings kept on a plain uninstall'
Hasnt 'HKLM:\Software\MXBMRP3'  'registry key removed'
Check (@(Arp).Count -eq 0)      'Add/Remove Programs entry removed'

Write-Host '== 3. Swedish, one game given on the command line, analytics left on'
Install 'install sv, KRP only' @('TRANSFORMS=:1053', "KRP_DIR=`"$krp\`"", 'MXB_SEL=0', 'GPB_SEL=0')
Has   "$krp\mxbmrp3_krp.dlo"    'KRP installed to the given folder'
Hasnt "$mxb\mxbmrp3.dlo"        'MX Bikes skipped with MXB_SEL=0'
Hasnt "$gpb\mxbmrp3_gpb.dlo"    'GP Bikes skipped with GPB_SEL=0'
$p = Prefs $krp
Check ($p -match '(?m)^language=sv\s*$') 'install_prefs.ini: language=sv from the Swedish transform'
Check ($p -notmatch 'analyticsOptOut') 'no opt-out line when the box stays ticked'
Plant-Data $krpData
Uninstall 'uninstall, delete data' @('WIPEDATA=1')
Hasnt "$krp\mxbmrp3_krp.dlo"    'KRP removed (folder remembered)'
Hasnt "$krpData"                'KRP player data deleted on request'
Has   "$mxbData\mxbmrp3.ini"    'data of a game that was not installed untouched'

Write-Host '== 4. taking over an NSIS install'
# What NSIS Setup leaves: files, its uninstaller in each plugins folder, an
# opt-out from an earlier Setup, and its Add/Remove Programs key holding the
# plugins folders. GP Bikes sits where only that key knows it.
$gpbNsis = 'C:\MxbmrpTest\Elsewhere\GPB\plugins'
foreach ($d in @(@($mxb, 'mxbmrp3.dlo'), @($gpbNsis, 'mxbmrp3_gpb.dlo'))) {
    New-Item -ItemType Directory -Force -Path "$($d[0])\mxbmrp3_data\fonts" | Out-Null
    Set-Content -Path "$($d[0])\$($d[1])" -Value 'old plugin'
    Set-Content -Path "$($d[0])\mxbmrp3_uninstall.exe" -Value 'nsis'
    Set-Content -Path "$($d[0])\mxbmrp3_data\fonts\retired.fnt" -Value 'old'
    Set-Content -Path "$($d[0])\mxbmrp3_data\install_prefs.ini" -Value "[Install]`r`nlanguage=de`r`nanalyticsOptOut=1`r`nstamp=1.0.0.1`r`n"
}
# A NEWER versioned plugin (any PE with a higher version will do), as a later
# release's NSIS or zip install leaves it: the file-version rules alone would
# keep it beside this release's data.
Copy-Item "$env:SystemRoot\System32\notepad.exe" "$mxb\mxbmrp3.dlo" -Force
Copy-Item -Recurse "$Staging\mxbmrp3_data\web" "$mxb\mxbmrp3_data\" -Force
Get-ChildItem -Recurse -File "$mxb\mxbmrp3_data\web" | ForEach-Object { Set-Content -Path $_.FullName -Value 'stale' }
New-Item -Force -Path $nsisKey | Out-Null
Set-ItemProperty -Path $nsisKey -Name DisplayName -Value 'MXBMRP3'
Set-ItemProperty -Path $nsisKey -Name UninstallString -Value "$mxb\mxbmrp3_uninstall.exe"
Set-ItemProperty -Path $nsisKey -Name InstallLocation -Value $mxb
Set-ItemProperty -Path $nsisKey -Name MXBikesPath -Value $mxb
Set-ItemProperty -Path $nsisKey -Name GPBikesPath -Value $gpbNsis

Install 'install over NSIS'
Check-Payload $mxb 'mxbmrp3.dlo' 'MX Bikes (over NSIS files)'
Check-Payload $gpbNsis 'mxbmrp3_gpb.dlo' 'GP Bikes (NSIS folder taken over)'
Hasnt "$gpb\mxbmrp3_gpb.dlo"              'NSIS folder wins over Steam detection'
Hasnt "$mxb\mxbmrp3_uninstall.exe"        'NSIS uninstaller removed (MX Bikes)'
Hasnt "$gpbNsis\mxbmrp3_uninstall.exe"    'NSIS uninstaller removed (GP Bikes)'
Hasnt $nsisKey                            'NSIS Add/Remove Programs entry removed'
$p = Prefs $mxb
# The Privacy page was not answered (an upgrade skips it), so the earlier
# opt-out is kept with its OLD stamp, as the .exe Setup does.
Check ($p -match '(?m)^analyticsOptOut=1\s*$') "an earlier Setup's opt-out is carried over"
Check ($p -match '(?m)^stamp=1\.0\.0\.1\s*$')   'the carried opt-out keeps its old stamp'
Check ((Prefs $gpbNsis) -match '(?m)^analyticsOptOut=1\s*$') 'carried over for every game'
Check ($p -match '(?m)^language=en\s*$')  'language rewritten by this Setup'
Uninstall 'uninstall after takeover'
Hasnt "$mxb\mxbmrp3_data"                 'leftover NSIS files go with mxbmrp3_data'
Hasnt "$gpbNsis\mxbmrp3_gpb.dlo"          'GP Bikes removed from the NSIS folder'

Write-Host '== 5. NSIS entry kept while it still covers a game this MSI skips'
New-Item -ItemType Directory -Force -Path $mxb, $gpbNsis | Out-Null
New-Item -Force -Path $nsisKey | Out-Null
# NSIS points its entry at the FIRST game's folder (InstallLocation and its
# uninstaller), which is exactly the game the MSI takes over here.
Set-ItemProperty -Path $nsisKey -Name UninstallString -Value "$mxb\mxbmrp3_uninstall.exe"
Set-ItemProperty -Path $nsisKey -Name InstallLocation -Value $mxb
Set-ItemProperty -Path $nsisKey -Name MXBikesPath -Value $mxb
Set-ItemProperty -Path $nsisKey -Name GPBikesPath -Value $gpbNsis
Set-Content -Path "$mxb\mxbmrp3_uninstall.exe" -Value 'nsis'
Set-Content -Path "$gpbNsis\mxbmrp3_uninstall.exe" -Value 'nsis'
# An earlier opt-out, but this time the player answers the Privacy page with
# the box ticked (PRIVACY_ASKED=1, as its Next sets): that is a yes, not carried.
New-Item -ItemType Directory -Force -Path "$mxb\mxbmrp3_data" | Out-Null
Set-Content -Path "$mxb\mxbmrp3_data\install_prefs.ini" -Value "[Install]`r`nanalyticsOptOut=1`r`nstamp=1.0.0.1`r`n"
Install 'install MX Bikes only over a two-game NSIS install' @('GPB_SEL=0', 'PRIVACY_ASKED=1')
Check ((Prefs $mxb) -notmatch 'analyticsOptOut') 'an answered Privacy page drops an earlier opt-out'
Has   $nsisKey                            'NSIS entry kept for GP Bikes'
Has   "$gpbNsis\mxbmrp3_uninstall.exe"    "GP Bikes' NSIS uninstaller kept"
Hasnt "$mxb\mxbmrp3_uninstall.exe"        "MX Bikes' NSIS uninstaller removed"
$k = Get-ItemProperty -Path $nsisKey
Check ($null -eq $k.PSObject.Properties['MXBikesPath']) 'NSIS entry no longer lists MX Bikes (its uninstaller cannot offer it)'
Check ($k.GPBikesPath -eq $gpbNsis)       'NSIS entry still lists GP Bikes'
Check ($k.UninstallString -eq "$gpbNsis\mxbmrp3_uninstall.exe") "NSIS UninstallString repointed at GP Bikes ($($k.UninstallString))"
Check (Test-Path -LiteralPath $k.UninstallString) 'NSIS UninstallString names a file that exists'
Check ($k.InstallLocation -eq $gpbNsis)   "NSIS InstallLocation repointed at GP Bikes ($($k.InstallLocation))"
Uninstall 'uninstall'
$k = Get-ItemProperty -Path $nsisKey -ErrorAction SilentlyContinue
Check ($k -and $k.UninstallString -eq "$gpbNsis\mxbmrp3_uninstall.exe") "NSIS entry left as repointed after the MSI's uninstall"
Remove-Item -Recurse -Force $nsisKey, $gpbNsis

Write-Host '== 6. fresh install deletes settings first; partial uninstall'
Plant-Data $gpbData
Install 'fresh install' @('FRESH=1')
Hasnt "$mxbData"                          'MX Bikes data deleted by Fresh install'
Hasnt "$gpbData"                          'GP Bikes data deleted by Fresh install'
Plant-Data $mxbData
Plant-Data $gpbData
Install 'remove GP Bikes only, delete its data' @('REMOVE=GPB', 'WIPEDATA=1')
Hasnt "$gpb\mxbmrp3_gpb.dlo"              'GP Bikes removed'
Hasnt "$gpbData"                          'GP Bikes data deleted'
Has   "$mxb\mxbmrp3.dlo"                  'MX Bikes kept'
Has   "$mxbData\mxbmrp3.ini"              'MX Bikes data kept'
Check (@(Arp).Count -eq 1)                'still listed in Add/Remove Programs'

# The Change page's boxes: reduced UI runs the UI sequence (where they are
# set) but shows no wizard page, so the log's closing property dump says how
# the page would open. A box is ticked for ANY value, so absent = unticked.
$script:run++
$log = Join-Path $logs ("{0:D2}-change-page-boxes.log" -f $script:run)
$p = Start-Process msiexec.exe -ArgumentList @('/i', "`"$Msi`"", '/qr', '/norestart', '/l*v', "`"$log`"") -PassThru
if (-not $p.WaitForExit(180000)) { $p.Kill(); Fail 'change page: reduced-UI run timed out' }
$props = Get-Content -LiteralPath $log | Where-Object { $_ -match '^Property\(C\): (MXB|GPB|KRP)_SEL = ' }
Check (@($props | Where-Object { $_ -match 'MXB_SEL = 1\s*$' }).Count -eq 1) "change page: MX Bikes box ticked (installed) [$($props -join '; ')]"
Check (@($props | Where-Object { $_ -match '(GPB|KRP)_SEL = ' }).Count -eq 0) 'change page: GP Bikes and KRP boxes unticked (not installed)'

Uninstall 'uninstall'
Has   "$mxbData\mxbmrp3.ini"              'MX Bikes data kept on plain uninstall'

Write-Host '== 7. nothing to install for'
Remove-Item -Recurse -Force $mxbGame
Remove-Item -Force $steamGpb
$p = Start-Process msiexec.exe -ArgumentList @('/i', "`"$Msi`"", '/qn', '/l*v', "`"$logs\nogame.log`"") -Wait -PassThru
Check ($p.ExitCode -eq 1603) "no game found: install refused (exit $($p.ExitCode))"
Check (@(Arp).Count -eq 0)                'and nothing registered'

Write-Host '== 8. the Games page folder check (ca/check_folders.cpp)'
# The wizard is the only caller, and silent installs never run it, so call the
# REAL custom action inside the built package through an installer session:
# set the properties the Games page would, DoAction, read what it set.
$caGame = 'C:\MxbmrpTest\CA\MX Bikes'
New-Item -ItemType Directory -Force -Path "$caGame\plugins" | Out-Null
Set-Content -Path "$caGame\mxbikes.exe" -Value 'game'
$installer.UILevel = 2   # msiUILevelNone
$installer.GetType().InvokeMember('EnableLog', 'InvokeMethod', $null, $installer, @('voicewarmupx', "$logs\folder-check.log")) | Out-Null
$session = $installer.GetType().InvokeMember('OpenPackage', 'InvokeMethod', $null, $installer, @($Msi, 0))   # 1 (ignore machine state) gives a restricted engine, which runs no custom action
function Set-SessionProp([string] $n, [string] $v) { $session.GetType().InvokeMember('Property', 'SetProperty', $null, $session, @($n, $v)) | Out-Null }
function Get-SessionProp([string] $n) { $session.GetType().InvokeMember('Property', 'GetProperty', $null, $session, @($n)) }
function Run-Check {
    $r = $session.GetType().InvokeMember('DoAction', 'InvokeMethod', $null, $session, @('CheckGameFolders'))
    Check ($r -eq 1) "folder check ran (msiDoActionStatusSuccess, got $r)"
}
Set-SessionProp 'MXB_SEL' '1'
Set-SessionProp 'MXB_DIR' "$caGame\plugins\"
Run-Check
Check ((Get-SessionProp 'FOLDERWARN') -eq '') 'game exe beside the plugins folder: no warning'
Set-SessionProp 'MXB_DIR' "$caGame\plugins"
Run-Check
Check ((Get-SessionProp 'FOLDERWARN') -eq '') 'no warning without the trailing backslash either'
Set-SessionProp 'GPB_SEL' '1'
Set-SessionProp 'GPB_DIR' 'C:\MxbmrpTest\CA\Nowhere\plugins\'
Set-SessionProp 'KRP_SEL' '1'
Set-SessionProp 'KRP_DIR' 'C:\'
Run-Check
$t = Get-SessionProp 'FOLDERWARNTEXT'
Check ((Get-SessionProp 'FOLDERWARN') -eq '1') 'two wrong folders: warning'
Check ($t -match 'not appear to be the GP Bikes plugins folder') "GP Bikes gets the sentence [$($t -replace '\r?\n', ' | ')]"
Check ($t -match 'not appear to be the Kart Racing Pro plugins folder') 'Kart Racing Pro gets its own sentence'
Check ($t -match [regex]::Escape('C:\MxbmrpTest\CA\Nowhere\plugins\')) 'with its folder'
Check ($t -notmatch 'MX Bikes') 'the good folder is not listed'
Check (([regex]::Matches($t, 'Continue anyway\?')).Count -eq 1) 'the question is asked once, after the list'
Set-SessionProp 'GPB_SEL' ''
Set-SessionProp 'KRP_SEL' '0'
Run-Check
Check ((Get-SessionProp 'FOLDERWARN') -eq '') 'unticked games are not checked'
Set-SessionProp 'GPB_SEL' '1'
Set-SessionProp 'GPB_DIR' ('C:\' + ('a' * 600) + '\plugins\')
Run-Check
Check ((Get-SessionProp 'FOLDERWARN') -eq '') 'an overlong folder is skipped, never a false warning or a crash'
[Runtime.InteropServices.Marshal]::ReleaseComObject($session) | Out-Null
$session = $null
[GC]::Collect(); [GC]::WaitForPendingFinalizers()

Remove-Item -Recurse -Force 'C:\MxbmrpTest', "$docs\PiBoSo" -ErrorAction SilentlyContinue
Write-Host "== $($script:fails) failure(s)"
if ($script:fails) { exit 1 }
