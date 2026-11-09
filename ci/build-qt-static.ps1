<#
.SYNOPSIS
    Baut ein statisches Qt 6 mit MSVC für den statischen Windows-Build von PandocUi.

.DESCRIPTION
    Lädt die Quellen von qtbase, qttools und qttranslations von download.qt.io, prüft deren
    MD5-Prüfsummen und baut sie als statische Bibliotheken mit statischer C-Laufzeit (/MT),
    nur als Release. Von qttools werden nur die Linguist-Werkzeuge (lupdate, lrelease …)
    gebaut; qttranslations liefert die Qt-eigenen Übersetzungen (qtbase_de.qm …).

    Das Ergebnis lässt sich mit  .\build.ps1 -QtDir <Prefix>  verwenden.

.PARAMETER Version
    Qt-Version, z. B. 6.8.5.

.PARAMETER Prefix
    Installationsziel. Standard: %USERPROFILE%\Qt\<Version>\msvc64_static
    (dort sucht build.ps1 -Static danach).

.PARAMETER WorkDir
    Arbeitsordner für Quellen und Build (einige GB). Standard: %TEMP%\qt-static-<Version>.
    Wird nach erfolgreichem Build gelöscht, außer mit -KeepWorkDir.

.PARAMETER DebugInfo
    Qt mit Debug-Informationen bauen (für Stacktraces bis in Qt hinein; deutlich größer).

.EXAMPLE
    .\ci\build-qt-static.ps1
.EXAMPLE
    .\ci\build-qt-static.ps1 -Version 6.8.5 -Prefix C:\qt-static -WorkDir D:\qtb
#>
[CmdletBinding()]
param(
    [string]$Version = '6.8.5',
    [string]$Prefix,
    [string]$WorkDir,
    [switch]$DebugInfo,
    [switch]$KeepWorkDir
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
. (Join-Path $PSScriptRoot 'windows-env.ps1')

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Stop-Build "Ungültige Qt-Version '$Version' (erwartet z. B. 6.8.5)."
}
if (-not $Prefix) {
    $Prefix = Join-Path $env:USERPROFILE "Qt\$Version\msvc64_static"
}
if (-not $WorkDir) {
    $WorkDir = Join-Path $env:TEMP "qt-static-$Version"
}
$Prefix = [IO.Path]::GetFullPath($Prefix)
$WorkDir = [IO.Path]::GetFullPath($WorkDir)

if (Test-Path (Join-Path $Prefix 'lib\cmake\Qt6\Qt6Config.cmake')) {
    Stop-Build "Unter $Prefix ist bereits ein Qt installiert. Ordner löschen oder anderes -Prefix angeben."
}

Enter-MsvcEnvironment

$modules = 'qtbase', 'qttools', 'qttranslations'
$series = ($Version -split '\.')[0..1] -join '.'
$baseUrl = "https://download.qt.io/archive/qt/$series/$Version/submodules"
$curl = Join-Path $env:SystemRoot 'System32\curl.exe'
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'   # bsdtar, entpackt auch .tar.xz

New-Item -ItemType Directory -Force $WorkDir | Out-Null

# --- Quellen laden und prüfen -------------------------------------------------
Write-Step "Lade Qt $Version ($($modules -join ', '))"
$sumsFile = Join-Path $WorkDir 'md5sums.txt'
Invoke-Native $curl @('-fsSL', '--retry', '3', '-o', $sumsFile, "$baseUrl/md5sums.txt")
$sums = Get-Content $sumsFile

$sourceDirs = @{}
foreach ($module in $modules) {
    # Ab Qt 6.8.4 heißen die Archive *-everywhere-opensource-src-*, davor *-everywhere-src-*
    $pattern = '^([0-9a-f]{32})\s+\*?(' + $module + '-everywhere-(?:opensource-)?src-' +
               [regex]::Escape($Version) + '\.tar\.xz)$'
    $match = $sums | ForEach-Object { [regex]::Match($_.Trim(), $pattern) } |
        Where-Object { $_.Success } | Select-Object -First 1
    if (-not $match) {
        Stop-Build "$module $Version nicht in $baseUrl/md5sums.txt gefunden."
    }
    $expected = $match.Groups[1].Value
    $fileName = $match.Groups[2].Value
    $archive = Join-Path $WorkDir $fileName

    if (-not (Test-Path $archive) -or (Get-FileHash $archive -Algorithm MD5).Hash -ne $expected) {
        Write-Host "  $fileName"
        Invoke-Native $curl @('-fsSL', '--retry', '3', '-o', $archive, "$baseUrl/$fileName")
    }
    $actual = (Get-FileHash $archive -Algorithm MD5).Hash
    if ($actual -ne $expected) {
        Stop-Build "Prüfsumme von $fileName stimmt nicht (erwartet $expected, erhalten $actual)."
    }

    $extractDir = Join-Path $WorkDir "src\$module"
    if (Test-Path $extractDir) {
        Remove-Item -Recurse -Force $extractDir
    }
    New-Item -ItemType Directory -Force $extractDir | Out-Null
    Invoke-Native $tar @('-xf', $archive, '-C', $extractDir)
    $sourceDirs[$module] = @(Get-ChildItem $extractDir -Directory)[0].FullName
}

# --- qtbase -------------------------------------------------------------------
$buildRoot = Join-Path $WorkDir 'build'
if (Test-Path $buildRoot) {
    Remove-Item -Recurse -Force $buildRoot
}

Write-Step "Baue qtbase (statisch, /MT) nach $Prefix"
$qtbaseBuild = Join-Path $buildRoot 'qtbase'
New-Item -ItemType Directory -Force $qtbaseBuild | Out-Null
$configureArgs = @('-prefix', $Prefix, '-release', '-static', '-static-runtime',
                   '-nomake', 'examples', '-nomake', 'tests')
if ($DebugInfo) {
    $configureArgs += '-force-debug-info'
}
Push-Location $qtbaseBuild
try {
    Invoke-Native (Join-Path $sourceDirs['qtbase'] 'configure.bat') $configureArgs
    Invoke-Native 'cmake' @('--build', '.', '--parallel')
    Invoke-Native 'cmake' @('--install', '.')
} finally {
    Pop-Location
}

# --- qttools (nur Linguist) und qttranslations --------------------------------
$qtCmake = Join-Path $Prefix 'bin\qt-cmake.bat'
$moduleOptions = @{
    # Nicht benötigte Werkzeuge abschalten; Linguist (lupdate/lrelease) bleibt an.
    qttools = @('-DFEATURE_assistant=OFF', '-DFEATURE_designer=OFF', '-DFEATURE_qdoc=OFF',
                '-DFEATURE_clang=OFF', '-DFEATURE_clangcpp=OFF',
                '-DFEATURE_distancefieldgenerator=OFF', '-DFEATURE_pixeltool=OFF',
                '-DFEATURE_qtdiag=OFF', '-DFEATURE_qtplugininfo=OFF', '-DFEATURE_qdbus=OFF',
                '-DFEATURE_kmap2qmap=OFF', '-DFEATURE_qev=OFF',
                '-DFEATURE_qtattributionsscanner=OFF')
    qttranslations = @()
}
foreach ($module in 'qttools', 'qttranslations') {
    Write-Step "Baue $module"
    $moduleBuild = Join-Path $buildRoot $module
    $cmakeArgs = @('-S', $sourceDirs[$module], '-B', $moduleBuild, '-G', 'Ninja',
                   '-DCMAKE_BUILD_TYPE=Release') + $moduleOptions[$module]
    Invoke-Native $qtCmake $cmakeArgs
    Invoke-Native 'cmake' @('--build', $moduleBuild, '--parallel')
    Invoke-Native 'cmake' @('--install', $moduleBuild)
}

# --- Abschluss ----------------------------------------------------------------
foreach ($required in 'lib\Qt6Core.lib', 'lib\Qt6Widgets.lib', 'bin\lrelease.exe',
                      'translations\qtbase_de.qm') {
    if (-not (Test-Path (Join-Path $Prefix $required))) {
        Stop-Build "Nach dem Build fehlt $required in $Prefix."
    }
}

if (-not $KeepWorkDir) {
    Write-Step "Lösche Arbeitsordner $WorkDir"
    Remove-Item -Recurse -Force $WorkDir
}

Write-Host ''
Write-Host "Fertig: statisches Qt $Version in $Prefix" -ForegroundColor Green
