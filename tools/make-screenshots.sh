#!/usr/bin/env bash
# Erzeugt die Bildschirmfotos der README unter docs/screenshots (deutsch und englisch).
# Läuft unter Linux/WSL ohne Bildschirm (offscreen); benötigt pandoc und xelatex,
# weil in Schritt 5 tatsächlich nach PDF, HTML, DOCX und EPUB umgewandelt wird.
#
#   tools/make-screenshots.sh [build-verzeichnis]
#
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -ge 1 ]]; then
    BUILD_DIR="$1"
elif [[ "$SOURCE_DIR" == /mnt/* ]]; then
    BUILD_DIR="$HOME/builds/pandocui-screenshots"   # WSL: Linux-Dateisystem ist schneller
else
    BUILD_DIR="$SOURCE_DIR/build/screenshots"
fi
OUT_DIR="$SOURCE_DIR/docs/screenshots"

for tool in pandoc xelatex; do
    command -v "$tool" >/dev/null || { echo "Fehler: $tool wird benötigt." >&2; exit 1; }
done

GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" ${GENERATOR[@]+"${GENERATOR[@]}"} \
      -DCMAKE_BUILD_TYPE=Release -DPANDOCUI_BUILD_TOOLS=ON -DPANDOCUI_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --target pandocui_screenshots

mkdir -p "$OUT_DIR"
export QT_QPA_PLATFORM=offscreen
"$BUILD_DIR/pandocui_screenshots" "$OUT_DIR" "$SOURCE_DIR/tests/data/beispiel.md" --lang de
"$BUILD_DIR/pandocui_screenshots" "$OUT_DIR" "$SOURCE_DIR/tests/data/beispiel.md" --lang en

ls -l "$OUT_DIR"
