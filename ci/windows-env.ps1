# Gemeinsame Hilfsfunktionen für build.ps1 und ci\build-qt-static.ps1 (per Dot-Sourcing geladen).

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

# Sucht Visual Studio (vswhere) und richtet die x64-Entwicklungsumgebung ein,
# sofern sie nicht schon aktiv ist. Prüft danach, ob cl, cmake und ninja verfügbar sind.
function Enter-MsvcEnvironment {
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
}
