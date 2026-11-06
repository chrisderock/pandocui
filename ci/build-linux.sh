#!/usr/bin/env bash
# Baut, testet und paketiert PandocUi in einem Debian- bzw. Rocky-Linux-Container.
# Ergebnis: Paket (.deb bzw. .rpm) im Verzeichnis dist/.
#
#   ci/build-linux.sh                         Version aus CMakeLists.txt
#   PANDOCUI_RELEASE_VERSION=0.2.0 ci/build-linux.sh
#
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$SOURCE_DIR/build/ci"
DIST_DIR="$SOURCE_DIR/dist"

# shellcheck disable=SC1091
. /etc/os-release
echo "==> System: $PRETTY_NAME"

case "$ID" in
    debian|ubuntu)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            ca-certificates cmake ninja-build g++ file dpkg-dev \
            qt6-base-dev qt6-tools-dev qt6-l10n-tools qt6-translations-l10n \
            pandoc
        GENERATOR=DEB
        ;;
    rocky|almalinux|rhel|centos)
        dnf install -y dnf-plugins-core epel-release
        dnf config-manager --set-enabled crb
        dnf install -y gcc-c++ cmake ninja-build rpm-build \
            qt6-qtbase-devel qt6-qttools-devel qt6-linguist qt6-qttranslations
        # pandoc wird nur für den Ende-zu-Ende-Test gebraucht und ist nicht überall paketiert
        dnf install -y pandoc || echo "Hinweis: pandoc nicht verfügbar – Ende-zu-Ende-Test wird übersprungen."
        GENERATOR=RPM
        ;;
    *)
        echo "Nicht unterstütztes System: $ID" >&2
        exit 1
        ;;
esac

echo "==> Bauen und testen"
rm -rf "$BUILD_DIR"
bash "$SOURCE_DIR/build.sh" --build-dir "$BUILD_DIR"

echo "==> Paket erzeugen ($GENERATOR)"
mkdir -p "$DIST_DIR"
(cd "$BUILD_DIR" && cpack -G "$GENERATOR" -B "$BUILD_DIR/packages")

shopt -s nullglob
for package in "$BUILD_DIR"/packages/*.deb; do
    # Distribution in den Namen aufnehmen: pandocui_0.1.0_amd64.deb → pandocui_0.1.0_debian13_amd64.deb
    name="$(basename "$package")"
    target="${name%_*}_${ID}${VERSION_ID%%.*}_${name##*_}"
    cp "$package" "$DIST_DIR/$target"
done
for package in "$BUILD_DIR"/packages/*.rpm; do
    cp "$package" "$DIST_DIR/"
done

echo "==> Ergebnis"
ls -l "$DIST_DIR"
