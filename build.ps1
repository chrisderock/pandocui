<#
.SYNOPSIS
    Baut PandocUi unter Windows mit Visual Studio (MSVC) und Qt 6.

.DESCRIPTION
    Sucht Visual Studio (vswhere) und eine Qt-6-Installation für MSVC, konfiguriert das
    Projekt mit CMake/Ninja, baut es, führt die Tests aus und stellt die Anwendung mit
    windeployqt in einem eigenständigen Ordner (dist\PandocUi) zusammen.

.PARAMETER QtDir
    Qt-Installation, z. B. C:\Qt\6.8.2\msvc2022_64. Ohne Angabe wird QTDIR bzw. Qt6_DIR
    verwendet oder die neueste Version unter C:\Qt bzw. %USERPROFILE%\Qt gesucht.

.PARAMETER Config
    Release (Standard) oder Debug.

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -QtDir C:\Qt\6.8.2\msvc2022_64 -Config Debug -Run
#>
[CmdletBinding()]
param(
    [string]$QtDir,
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$BuildDir,
    [string]$DeployDir,
    [switch]$Clean,
    [switch]$NoTests,
    [switch]$NoDeploy,
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$SourceDir = $PSScriptRoot

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host "==> $Text" -ForegroundColor Cyan
}

function Stop-Build([string]$Text) {
    Write-Host "Fehler: $Text" -ForegroundColor Red
    exit 1
}

function Invoke-Native([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        Stop-Build "$File endete mit Exit-Code $LASTEXITCODE."
    }
}

function Test-QtDir([string]$Dir) {
    return $Dir -and (Test-Path (Join-Path $Dir 'lib\cmake\Qt6\Qt6Config.cmake'))
}

# --- Visual Studio ------------------------------------------------------------
Write-Step 'Suche Visual Studio'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Stop-Build 'Visual Studio wurde nicht gefunden (vswhere.exe fehlt).'
}
$vsPath = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vsPath = @($vsPath)[0]
if (-not $vsPath) {
    Stop-Build 'Keine Visual-Studio-Installation mit C++-Werkzeugen gefunden (Workload "Desktopentwicklung mit C++").'
}
Write-Host "  $vsPath"

if (-not $env:VSCMD_VER) {
    # VsDevCmd ruft vswhere.exe über den PATH auf
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

foreach ($tool in 'cl', 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Stop-Build "$tool nicht gefunden. Im Visual Studio Installer die Komponente 'C++-CMake-Tools für Windows' installieren."
    }
}
Write-Host "  cmake: $((Get-Command cmake).Source)"

# --- Qt -----------------------------------------------------------------------
Write-Step 'Suche Qt 6 (MSVC)'
if (-not $QtDir) {
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
    $roots = @('C:\Qt', (Join-Path $env:USERPROFILE 'Qt')) | Where-Object { Test-Path $_ }
    $found = foreach ($root in $roots) {
        Get-ChildItem $root -Directory -Filter '6.*' | ForEach-Object {
            Get-ChildItem $_.FullName -Directory -Filter 'msvc*_64'
        }
    }
    $found = $found |
        Where-Object { Test-QtDir $_.FullName } |
        Sort-Object @{ Expression = { try { [version]$_.Parent.Name } catch { [version]'0.0' } }; Descending = $true },
                    @{ Expression = { $_.Name }; Descending = $true }
    if ($found) {
        $QtDir = @($found)[0].FullName
    }
}
if (-not (Test-QtDir $QtDir)) {
    Stop-Build ("Qt 6 für MSVC wurde nicht gefunden. Mit dem Qt Online Installer die Komponente " +
                "'MSVC 2022 64-bit' installieren oder den Pfad mit -QtDir angeben.")
}
$QtDir = (Resolve-Path $QtDir).Path
Write-Host "  $QtDir"

# --- Konfigurieren und bauen --------------------------------------------------
if (-not $BuildDir) {
    $BuildDir = Join-Path $SourceDir "build\windows-$($Config.ToLower())"
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

# Qt-DLLs für Tests und direkten Start auffindbar machen
$env:PATH = "$(Join-Path $QtDir 'bin');$env:PATH"

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
    $exe = Join-Path $DeployDir 'PandocUi.exe'

    $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
    $modeFlag = if ($Config -eq 'Debug') { '--debug' } else { '--release' }
    Invoke-Native $windeployqt @($modeFlag, '--compiler-runtime', '--no-system-d3d-compiler', $exe)
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
