# PandocUi

Grafische Oberfläche für [pandoc](https://pandoc.org), erstellt mit Qt 6 (Widgets) und C++17.

## Ablauf

Die Oberfläche führt als Wizard durch fünf Schritte. Links zeigt eine Schrittleiste alle
Schritte mit einer Kurzfassung der jeweiligen Auswahl; erledigte Schritte sind mit einem
Haken markiert und lassen sich jederzeit direkt anklicken. Rechts steht immer nur die
Seite des aktuellen Schritts, unten führen „Zurück“ und „Weiter“ (Alt+Z / Alt+W) durch
den Ablauf. Ein Schritt ist erst erreichbar, wenn die vorherigen vollständig sind – was
noch fehlt, steht als Hinweis unten links.

1. **Eingabedatei** wählen (Dialog, Pfad eintippen oder Datei ins Fenster ziehen).
   Das Eingabeformat wird anhand der Dateiendung erkannt, bei `.xml`/`.json` anhand
   des Inhalts. Mit „Format automatisch erkennen“ abgewählt lässt es sich manuell setzen.
2. **Ausgabeformate** ankreuzen: Oben stehen häufige Formate, darunter alle weiteren,
   die die installierte pandoc-Version anbietet. Die Liste lässt sich filtern.
3. **Optionen je Format**: Für jedes gewählte Format gibt es einen eigenen Reiter
   (Allgemein, Metadaten, formatspezifische Optionen, Erweitert, freie Zusatzargumente)
   mit Live-Vorschau des pandoc-Befehls.
4. **Ausgabeordner** wählen (oder den Ordner der Eingabedatei verwenden). Die Dateinamen
   leiten sich vom Namen der Eingabedatei ab, z. B. `bericht.md` → `bericht.docx`.
   Teilen sich mehrere Formate eine Endung, wird der Formatname eingefügt
   (`bericht.html`, `bericht.revealjs.html`). Die Eingabedatei wird nie überschrieben.
5. **Erzeugen**: Eine Zusammenfassung zeigt Eingabe, Zielordner und alle Ausgabedateien
   samt Anzahl geänderter Optionen. „Konvertieren“ (Strg+Eingabe) erzeugt die Formate
   nacheinander; Fortschritt, Befehle, Warnungen und Fehler erscheinen im Protokoll.
   Die Konvertierung lässt sich abbrechen.

Auswahl, Ordner und Optionen werden beim Beenden gespeichert (`QSettings`).

## Presets

Ein Preset speichert die gewählten Ausgabeformate (in ihrer Reihenfolge) samt aller
Optionen; Eingabedatei und Ausgabeordner gehören nicht dazu.

- **Speichern:** Diskettensymbol neben der Preset-Auswahl oder *Presets → Als Preset speichern …* (Strg+S)
- **Laden:** Preset in der Auswahlliste in Schritt 2 („Ausgabeformate“) anklicken
- **Löschen:** Papierkorbsymbol oder *Presets → Preset löschen*
- **Weitergeben:** *Presets → Preset exportieren … / importieren …* (JSON-Datei)

Presets liegen als JSON-Dateien im Anwendungsdatenordner (*Presets → Preset-Ordner öffnen*),
unter Windows z. B. `%APPDATA%\PandocUi\PandocUi\presets`, unter Linux
`~/.local/share/PandocUi/PandocUi/presets`. Gespeichert werden nur Werte, die vom Standard
abweichen – die Dateien lassen sich daher auch gut von Hand bearbeiten:

```json
{
    "pandocui-preset": 1,
    "name": "Bericht A4",
    "formats": ["pdf", "docx"],
    "options": {
        "pdf":  { "--pdf-engine": "xelatex", "--toc": true, "V:papersize": "a4" },
        "docx": { "--reference-doc": "C:\\Vorlagen\\firma.docx" }
    }
}
```

Formate, die die installierte pandoc-Version nicht kennt, werden beim Laden übersprungen
und im Protokoll gemeldet.

## Sprachen

Die Oberfläche gibt es auf **Deutsch** und **Englisch**. Standardmäßig richtet sie sich nach
der Systemsprache; Systeme, die nicht auf Deutsch eingestellt sind, erhalten Englisch.
Umschalten lässt sich über *Datei → Sprache* (mit Angebot zum Neustart) oder beim Start:

```bash
PandocUi --lang en
```

Die Texte im Quelltext sind deutsch (`tr("…")`); die englische Übersetzung liegt in
`translations/pandocui_en.ts` und wird beim Build als `.qm` in die Anwendung eingebettet.
Nach Textänderungen im Quelltext die Übersetzungsdatei aktualisieren und anschließend
die neuen Einträge (z. B. mit Qt Linguist) übersetzen:

```bash
cmake --build ~/builds/pandocui-release --target update_translations
linguist translations/pandocui_en.ts
```

Eine weitere Sprache kommt hinzu, indem man eine Datei `translations/pandocui_<code>.ts`
in `CMakeLists.txt` einträgt und den Code in `Translation::availableLanguages()`
(`src/translation.cpp`) ergänzt.

## Voraussetzungen

- Qt ≥ 6.5 (Modul Widgets und die Linguist-Werkzeuge `lupdate`/`lrelease`, für die Tests zusätzlich Test)
- CMake ≥ 3.19, ein C++17-Compiler
- pandoc im `PATH` (oder über *Datei → pandoc-Programm festlegen …* auswählen)
- für PDF eine PDF-Engine, z. B. `pdflatex`/`xelatex` (TeX Live, MiKTeX), `typst` oder `weasyprint`

## Bauen

Für beide Plattformen gibt es ein Skript, das konfiguriert, baut und die Tests ausführt.

### Linux / WSL (Debian): `build.sh`

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools cmake ninja-build g++ pandoc
./build.sh                       # Release-Build mit Tests
./build.sh --debug --run         # Debug-Build und danach starten
./build.sh --install ~/.local    # bauen und nach ~/.local/bin installieren
./build.sh --help                # alle Optionen
```

Build-Verzeichnis ist `build/linux-<typ>`. Liegt das Projekt unter WSL auf einem
Windows-Laufwerk (`/mnt/c/...`), wird stattdessen in `~/builds/pandocui-<typ>` gebaut,
weil das Linux-Dateisystem deutlich schneller ist. Unter WSLg öffnet sich das Fenster
direkt unter Windows. Eine eigene Qt-Installation wird über `CMAKE_PREFIX_PATH` angegeben.

### Windows (MSVC): `build.ps1` bzw. `build.cmd`

Voraussetzungen: Visual Studio 2022 oder neuer mit „Desktopentwicklung mit C++“
(inkl. „C++-CMake-Tools für Windows“) und Qt 6 für MSVC 64-bit aus dem Qt Online Installer.

```bash
build.cmd                                         # Release-Build, Tests, Bereitstellung
build.cmd -Config Debug -Run                      # Debug-Build und danach starten
build.cmd -QtDir C:\Qt\6.8.2\msvc2022_64 -Clean   # Qt-Pfad vorgeben, sauber neu bauen
```

`build.cmd` ruft `build.ps1` unabhängig von der PowerShell-Ausführungsrichtlinie auf.
Das Skript

1. findet Visual Studio über `vswhere` und richtet die x64-Entwicklungsumgebung ein,
2. sucht Qt (`-QtDir`, sonst `QTDIR`/`Qt6_DIR`, sonst die neueste Version unter `C:\Qt` bzw. `%USERPROFILE%\Qt`),
3. baut mit CMake und Ninja nach `build\windows-<typ>` und führt die Tests aus,
4. stellt mit `windeployqt` eine lauffähige Anwendung samt Qt-DLLs in `dist\PandocUi` zusammen.

Weitere Schalter: `-BuildDir`, `-DeployDir`, `-NoTests`, `-NoDeploy` (siehe `Get-Help .\build.ps1 -Detailed`).
pandoc wird nicht mitgeliefert, sondern muss installiert sein (`winget install --id JohnMacFarlane.Pandoc`).

Alternativ das Projekt (`CMakeLists.txt`) direkt in Qt Creator oder Visual Studio öffnen.

## Tests

Die Build-Skripte führen die Tests automatisch aus (abschaltbar mit `--no-tests` bzw. `-NoTests`).
Die Tests prüfen Formaterkennung, Dateinamen, das Zusammensetzen der Argumente und
die Oberfläche (offscreen). Ist pandoc installiert, wird `tests/data/beispiel.md`
zusätzlich tatsächlich nach HTML, DOCX, GFM, ODT und – falls `pdflatex` vorhanden – PDF umgewandelt.

## Linux-Pakete

```bash
cmake --build ~/builds/pandocui-release && cd ~/builds/pandocui-release
cpack -G DEB    # Debian/Ubuntu
cpack -G RPM    # Rocky Linux/RHEL/Fedora (benötigt rpm-build)
```

Die Pakete installieren `/usr/bin/PandocUi` samt Starter im Anwendungsmenü. Das `.deb`
empfiehlt pandoc als Abhängigkeit; unter Rocky Linux/RHEL stammt pandoc aus EPEL und wird
separat installiert:

```bash
sudo dnf install epel-release && sudo dnf install pandoc
```
 `ci/build-linux.sh` erledigt Installation der Build-Abhängigkeiten,
Build, Tests und Paketierung in einem frischen Debian- oder Rocky-Linux-System, z. B.:

```bash
podman run --rm -v "$PWD:/src:ro" debian:13 bash -c 'cp -r /src /work && cd /work && bash ci/build-linux.sh'
```

## Automatischer Build und Release (GitHub Actions)

Der Workflow `.github/workflows/build-release.yml` baut und testet bei jedem Push auf `main`
und bei Pull Requests für

| Plattform | Umgebung | Ergebnis |
|-----------|----------|----------|
| Windows | `windows-latest`, MSVC, Qt 6.8 LTS | `PandocUi-<version>-windows-x64.zip` (mit Qt-DLLs, ohne pandoc) |
| macOS | `macos-26`, Universal-Binary (Apple Silicon + Intel), Qt 6.8 LTS, ab macOS 12 | `PandocUi-<version>-macos-universal.dmg` (App-Bundle, ohne pandoc) |
| Debian 13 | Container `debian:13` | `pandocui_<version>_debian13_amd64.deb` |
| Rocky Linux 10 | Container `rockylinux/rockylinux:10` | `pandocui-<version>-1.el10.x86_64.rpm` |

Die macOS-App ist nur ad hoc signiert und nicht von Apple notarisiert. Beim ersten Start
meldet macOS daher, dass der Entwickler nicht verifiziert werden kann: die App per
Rechtsklick → *Öffnen* starten oder die Quarantäne-Markierung entfernen:

```bash
xattr -dr com.apple.quarantine /Applications/PandocUi.app
```

Die Dateien stehen danach als Artefakte am Workflow-Lauf. Ein **Release** entsteht durch
einen Versions-Tag; die Version wird aus dem Tag übernommen:

```bash
git tag v0.2.0
git push origin v0.2.0
```

Der Workflow baut dann alle Plattformen mit Version 0.2.0 und veröffentlicht die Pakete
zusammen mit `SHA256SUMS.txt` als GitHub-Release mit automatisch erzeugten Release Notes.

## Aufbau

| Datei | Inhalt |
|-------|--------|
| `src/pandocformats.*` | Formaterkennung, Dateiendungen, Beschreibungen, Optionsdefinitionen je Format, Argumentaufbau |
| `src/pandocinfo.*` | pandoc finden, Version und Formatlisten abfragen |
| `src/stepnavigator.*` | Schrittleiste des Wizards |
| `src/formatoptionswidget.*` | Aus den Optionsdefinitionen erzeugte Eingabemaske je Format |
| `src/presetstore.*` | Speichern, Laden, Import und Export von Presets (JSON) |
| `src/translation.*` | Sprachauswahl und Laden der Übersetzungen |
| `translations/` | Übersetzungsdateien (Qt Linguist) |
| `src/conversionrunner.*` | Führt die pandoc-Aufrufe nacheinander per `QProcess` aus |
| `src/mainwindow.*` | Hauptfenster mit den fünf Schritten |
| `tests/` | QtTest-Tests und Beispieldokument |

Neue Optionen für ein Format werden in `specificOptions()` in `src/pandocformats.cpp`
ergänzt – die Oberfläche und die Befehlszeile ergeben sich daraus automatisch.
