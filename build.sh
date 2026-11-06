#!/usr/bin/env bash
# Baut PandocUi unter Linux (auch WSL).
#
#   ./build.sh                      Release-Build mit Tests
#   ./build.sh --debug --run        Debug-Build, danach starten
#   ./build.sh --install ~/.local   Bauen und nach ~/.local installieren
#
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="Release"
BUILD_DIR=""
CLEAN=0
RUN_TESTS=1
RUN_APP=0
INSTALL_PREFIX=""
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
    cat <<EOF
Aufruf: $(basename "$0") [Optionen]

Optionen:
  -d, --debug            Debug-Build (Standard: Release)
  -b, --build-dir DIR    Build-Verzeichnis (Standard: build/linux-<typ>,
                         unter WSL auf /mnt/...: ~/builds/pandocui-<typ>)
  -c, --clean            Build-Verzeichnis vorher löschen
      --no-tests         Tests weder bauen noch ausführen
  -r, --run              Anwendung nach dem Build starten
  -i, --install PREFIX   Nach PREFIX installieren (z. B. ~/.local)
  -j, --jobs N           Anzahl paralleler Jobs (Standard: $JOBS)
  -h, --help             Diese Hilfe
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--debug)     BUILD_TYPE="Debug" ;;
        -b|--build-dir) BUILD_DIR="${2:?Verzeichnis fehlt}"; shift ;;
        -c|--clean)     CLEAN=1 ;;
        --no-tests)     RUN_TESTS=0 ;;
        -r|--run)       RUN_APP=1 ;;
        -i|--install)   INSTALL_PREFIX="${2:?Präfix fehlt}"; shift ;;
        -j|--jobs)      JOBS="${2:?Anzahl fehlt}"; shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unbekannte Option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

step()  { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
fail()  { printf '\033[1;31mFehler: %s\033[0m\n' "$*" >&2; exit 1; }

# --- Werkzeuge prüfen ---------------------------------------------------------
step "Prüfe Werkzeuge"
command -v cmake >/dev/null || fail "cmake fehlt (Debian/Ubuntu: sudo apt install cmake)"
command -v c++   >/dev/null || fail "C++-Compiler fehlt (Debian/Ubuntu: sudo apt install g++)"

GENERATOR=()
if command -v ninja >/dev/null; then
    GENERATOR=(-G Ninja)
fi

if ! command -v qmake6 >/dev/null && ! command -v qtpaths6 >/dev/null \
   && [[ -z "${CMAKE_PREFIX_PATH:-}" && -z "${Qt6_DIR:-}" ]]; then
    echo "Hinweis: Qt 6 nicht im PATH gefunden – CMake sucht trotzdem in den Standardpfaden."
    echo "         Debian/Ubuntu: sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools"
    echo "         Eigene Qt-Installation: CMAKE_PREFIX_PATH=/pfad/zu/Qt/6.x/gcc_64 $0"
fi
command -v pandoc >/dev/null || echo "Hinweis: pandoc ist nicht installiert – die Anwendung benötigt es zur Laufzeit."

# --- Build-Verzeichnis --------------------------------------------------------
type_lower="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
if [[ -z "$BUILD_DIR" ]]; then
    if [[ "$SOURCE_DIR" == /mnt/* ]]; then
        # Windows-Laufwerk unter WSL: auf dem Linux-Dateisystem bauen (deutlich schneller)
        BUILD_DIR="$HOME/builds/pandocui-$type_lower"
    else
        BUILD_DIR="$SOURCE_DIR/build/linux-$type_lower"
    fi
fi

if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    step "Lösche $BUILD_DIR"
    rm -rf -- "$BUILD_DIR"
fi

# --- Konfigurieren und bauen --------------------------------------------------
step "Konfiguriere ($BUILD_TYPE) in $BUILD_DIR"
TESTS_FLAG=ON
[[ $RUN_TESTS -eq 0 ]] && TESTS_FLAG=OFF
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" "${GENERATOR[@]}" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DPANDOCUI_BUILD_TESTS="$TESTS_FLAG"

step "Baue"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [[ $RUN_TESTS -eq 1 ]]; then
    step "Führe Tests aus"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ -n "$INSTALL_PREFIX" ]]; then
    step "Installiere nach $INSTALL_PREFIX"
    cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
fi

APP="$BUILD_DIR/PandocUi"
printf '\n\033[1;32mFertig:\033[0m %s\n' "$APP"

if [[ $RUN_APP -eq 1 ]]; then
    step "Starte PandocUi"
    exec "$APP"
fi
