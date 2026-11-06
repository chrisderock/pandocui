# PandocUi

Grafische Oberfläche für [pandoc](https://pandoc.org), erstellt mit Qt 6 (Widgets) und C++17.

## Ablauf

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
5. **Konvertieren**: Die Formate werden nacheinander erzeugt; Fortschritt, Befehle,
   Warnungen und Fehler erscheinen im Protokoll. Die Konvertierung lässt sich abbrechen.

Auswahl, Ordner und Optionen werden beim Beenden gespeichert (`QSettings`).

## Voraussetzungen

- Qt ≥ 6.5 (Modul Widgets, für die Tests zusätzlich Test)
- CMake ≥ 3.19, ein C++17-Compiler
- pandoc im `PATH` (oder über *Datei → pandoc-Programm festlegen …* auswählen)
- für PDF eine PDF-Engine, z. B. `pdflatex`/`xelatex` (TeX Live, MiKTeX), `typst` oder `weasyprint`

## Bauen

Für beide Plattformen gibt es ein Skript, das konfiguriert, baut und die Tests ausführt.

### Linux / WSL (Debian): `build.sh`

```bash
sudo apt install qt6-base-dev cmake ninja-build g++ pandoc
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

## Aufbau

| Datei | Inhalt |
|-------|--------|
| `src/pandocformats.*` | Formaterkennung, Dateiendungen, Beschreibungen, Optionsdefinitionen je Format, Argumentaufbau |
| `src/pandocinfo.*` | pandoc finden, Version und Formatlisten abfragen |
| `src/formatoptionswidget.*` | Aus den Optionsdefinitionen erzeugte Eingabemaske je Format |
| `src/conversionrunner.*` | Führt die pandoc-Aufrufe nacheinander per `QProcess` aus |
| `src/mainwindow.*` | Hauptfenster mit den fünf Schritten |
| `tests/` | QtTest-Tests und Beispieldokument |

Neue Optionen für ein Format werden in `specificOptions()` in `src/pandocformats.cpp`
ergänzt – die Oberfläche und die Befehlszeile ergeben sich daraus automatisch.
