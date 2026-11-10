<#
.SYNOPSIS
    Baut PandocUi unter Windows mit Visual Studio (MSVC) und Qt 6.

.DESCRIPTION
    Sucht Visual Studio (vswhere) und eine Qt-6-Installation für MSVC, konfiguriert das
    Projekt mit CMake/Ninja, baut es, führt die Tests aus und stellt die Anwendung in einem
    Ordner (dist\PandocUi) bereit.

    Mit einem normalen Qt (DLLs) sammelt windeployqt die Qt-DLLs neben der Anwendung ein.
    Mit einem statisch gebauten Qt (siehe ci\build-qt-static.ps1) entsteht eine einzelne
    PandocUi.exe ohne weitere Abhängigkeiten. Welche Art vorliegt, wird automatisch erkannt.

.PARAMETER QtDir
    Qt-Installation, z. B. C:\Qt\6.8.2\msvc2022_64. Ohne Angabe wird QTDIR bzw. Qt6_DIR
    verwendet oder die neueste Version unter C:\Qt bzw. %USERPROFILE%\Qt gesucht.

.PARAMETER Static
    Ohne -QtDir ein statisches Qt (Ordner msvc64_static) statt eines normalen Qt suchen.

.PARAMETER BuildQt
    Zusammen mit -Static: Wird kein statisches Qt gefunden, wird es mit
    ci\build-qt-static.ps1 gebaut (Version -QtVersion; dauert etwa eine Stunde).

.PARAMETER Config
    Release (Standard) oder Debug. Mit statischem Qt nur Release.

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -QtDir C:\Qt\6.8.2\msvc2022_64 -Config Debug -Run
.EXAMPLE
    .\build.ps1 -Static -BuildQt
#>
[CmdletBinding()]
param(
    [string]$QtDir,
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$BuildDir,
    [string]$DeployDir,
    [switch]$Static,
    [switch]$BuildQt,
    [string]$QtVersion = '6.8.5',
    [switch]$Clean,
    [switch]$NoTests,
    [switch]$NoDeploy,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$SourceDir = $PSScriptRoot
. (Join-Path $SourceDir 'ci\windows-env.ps1')

function Test-QtDir([string]$Dir) {
    return $Dir -and (Test-Path (Join-Path $Dir 'lib\cmake\Qt6\Qt6Config.cmake'))
}

# Neueste Qt-Installation unter C:\Qt bzw. %USERPROFILE%\Qt, deren Ordnername $Filter entspricht
function Find-QtDir([string]$Filter) {
    $roots = @('C:\Qt', (Join-Path $env:USERPROFILE 'Qt')) | Where-Object { Test-Path $_ }
    $found = foreach ($root in $roots) {
        Get-ChildItem $root -Directory -Filter '6.*' | ForEach-Object {
            Get-ChildItem $_.FullName -Directory -Filter $Filter
        }
    }
    $found = $found |
        Where-Object { Test-QtDir $_.FullName } |
        Sort-Object @{ Expression = { try { [version]$_.Parent.Name } catch { [version]'0.0' } }; Descending = $true },
                    @{ Expression = { $_.Name }; Descending = $true }
    if ($found) {
        return @($found)[0].FullName
    }
    return $null
}

Enter-MsvcEnvironment

# --- Qt -----------------------------------------------------------------------
if ($Static) {
    Write-Step 'Suche statisches Qt 6 (MSVC)'
} else {
    Write-Step 'Suche Qt 6 (MSVC)'
}
if (-not $QtDir -and -not $Static) {
    foreach ($candidate in @($env:QTDIR, $env:Qt6_DIR)) {
        if (Test-QtDir $candidate) {
            $QtDir = $candidate
            break
        }
        # Qt6_DIR zeigt üblicherweise auf ...\lib\cmake\Qt6
        if ($candidate -and (Test-Path (Join-Path $candidate 'Qt6Config.cmake'))) {
            $QtDir = (Resolve-Path (Join-Path $candidate '..\..\..')).Path
            break
        }
    }
}
if (-not $QtDir) {
    # Das Muster msvc*_64 trifft msvc64_static bewusst nicht.
    $QtDir = if ($Static) { Find-QtDir 'msvc*_static' } else { Find-QtDir 'msvc*_64' }
}
if (-not $QtDir -and $Static -and $BuildQt) {
    $QtDir = Join-Path $env:USERPROFILE "Qt\$QtVersion\msvc64_static"
    Write-Step "Baue statisches Qt $QtVersion nach $QtDir"
    & (Join-Path $SourceDir 'ci\build-qt-static.ps1') -Version $QtVersion -Prefix $QtDir
    if ($LASTEXITCODE -ne 0) {
        Stop-Build "Der Build des statischen Qt ist fehlgeschlagen."
    }
}
if (-not (Test-QtDir $QtDir)) {
    if ($Static) {
        Stop-Build ("Kein statisches Qt gefunden. Mit -BuildQt bauen lassen oder den Pfad mit -QtDir angeben.")
    }
    Stop-Build ("Qt 6 für MSVC wurde nicht gefunden. Mit dem Qt Online Installer die Komponente " +
                "'MSVC 2022 64-bit' installieren oder den Pfad mit -QtDir angeben.")
}
$QtDir = (Resolve-Path $QtDir).Path
# Statisches Qt: Bibliotheken vorhanden, aber keine Qt-DLLs
$StaticQt = (Test-Path (Join-Path $QtDir 'lib\Qt6Core.lib')) -and
            -not (Get-ChildItem (Join-Path $QtDir 'bin') -Filter 'Qt6Core*.dll' -ErrorAction SilentlyContinue)
Write-Host "  $QtDir$(if ($StaticQt) { ' (statisch)' })"
if ($StaticQt -and $Config -eq 'Debug') {
    Stop-Build 'Das statische Qt enthält nur Release-Bibliotheken; bitte -Config Release verwenden.'
}

# --- Konfigurieren und bauen --------------------------------------------------
if (-not $BuildDir) {
    $suffix = if ($StaticQt) { '-static' } else { '' }
    $BuildDir = Join-Path $SourceDir "build\windows-$($Config.ToLower())$suffix"
}
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Lösche $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Write-Step "Konfiguriere ($Config) in $BuildDir"
$testsFlag = if ($NoTests) { 'OFF' } else { 'ON' }
Invoke-Native 'cmake' @('-S', $SourceDir, '-B', $BuildDir, '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config", "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DPANDOCUI_BUILD_TESTS=$testsFlag")

Write-Step 'Baue'
Invoke-Native 'cmake' @('--build', $BuildDir, '--parallel')

if (-not $StaticQt) {
    # Qt-DLLs für Tests und direkten Start auffindbar machen
    $env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"
}

if (-not $NoTests) {
    Write-Step 'Führe Tests aus'
    Invoke-Native 'ctest' @('--test-dir', $BuildDir, '--output-on-failure')
}

$exe = Join-Path $BuildDir 'PandocUi.exe'

# --- Bereitstellen ------------------------------------------------------------
if (-not $NoDeploy) {
    if (-not $DeployDir) {
        $DeployDir = Join-Path $SourceDir 'dist\PandocUi'
    }
    Write-Step "Stelle Anwendung in $DeployDir zusammen"
    if (Test-Path $DeployDir) {
        Remove-Item -Recurse -Force $DeployDir
    }
    New-Item -ItemType Directory -Force $DeployDir | Out-Null
    Copy-Item $exe $DeployDir
    Copy-Item (Join-Path $SourceDir 'LICENSE') (Join-Path $DeployDir 'LICENSE.txt')
    $exe = Join-Path $DeployDir 'PandocUi.exe'

    if ($StaticQt) {
        Write-Host '  Statisch gelinkt: keine Qt-DLLs nötig.'
    } else {
        $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
        $modeFlag = if ($Config -eq 'Debug') { '--debug' } else { '--release' }
        Invoke-Native $windeployqt @($modeFlag, '--compiler-runtime', '--no-system-d3d-compiler', $exe)
    }
}

Write-Host ''
Write-Host "Fertig: $exe" -ForegroundColor Green
if (-not (Get-Command pandoc -ErrorAction SilentlyContinue)) {
    Write-Host 'Hinweis: pandoc ist nicht im PATH. Installation: winget install --id JohnMacFarlane.Pandoc' -ForegroundColor Yellow
}

if ($Run) {
    Write-Step 'Starte PandocUi'
    Start-Process $exe
}
