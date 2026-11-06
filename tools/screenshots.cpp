// Erzeugt die Bildschirmfotos für die README in einer sauberen Beispielumgebung.
//
//   pandocui_screenshots <ausgabeordner> <beispiel.md> [--lang de|en]
//
// Am einfachsten über tools/make-screenshots.sh aufrufen (Linux, offscreen).

#include "formatoptionswidget.h"
#include "mainwindow.h"
#include "pandocinfo.h"
#include "pandocmissingdialog.h"
#include "presetstore.h"
#include "translation.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>

namespace {

void settle(int milliseconds = 200)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool save(QWidget *widget, const QDir &dir, const QString &name)
{
    settle();
    const bool ok = widget->grab().save(dir.filePath(name));
    qInfo().noquote() << (ok ? "gespeichert:" : "FEHLER beim Speichern:") << dir.filePath(name);
    return ok;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PandocUiScreenshots"));
    QApplication::setApplicationName(QStringLiteral("PandocUiScreenshots"));
    QStandardPaths::setTestModeEnabled(true); // eigene, leere Einstellungen und Presets

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption langOption(QStringLiteral("lang"), QStringLiteral("Sprache (de, en)"),
                                        QStringLiteral("sprache"), QStringLiteral("de"));
    parser.addOption(langOption);
    parser.addPositionalArgument(QStringLiteral("ausgabeordner"), QStringLiteral("Zielordner für die PNG-Dateien"));
    parser.addPositionalArgument(QStringLiteral("beispiel"), QStringLiteral("Markdown-Datei für die Beispiele"));
    parser.process(app);
    if (parser.positionalArguments().size() != 2)
        parser.showHelp(1);

    const QDir outDir(parser.positionalArguments().at(0));
    QDir().mkpath(outDir.path());
    const QString sample = parser.positionalArguments().at(1);

    QSettings().clear();
    QDir(PresetStore::defaultDirectory()).removeRecursively();
    const QString language = Translation::install(&app, parser.value(langOption));
    const bool german = language == QLatin1String("de");
    const QString prefix = german ? QString() : language + QLatin1Char('-');

    // Beispielumgebung mit neutralen Pfaden
    QDir demo(QDir::tempPath() + QStringLiteral("/PandocUi-Demo"));
    demo.removeRecursively();
    const QString documents = demo.filePath(german ? QStringLiteral("Dokumente") : QStringLiteral("Documents"));
    const QString outputFolder = documents + (german ? QStringLiteral("/Ausgabe") : QStringLiteral("/Output"));
    QDir().mkpath(outputFolder);
    const QString input = documents + (german ? QStringLiteral("/bericht.md") : QStringLiteral("/report.md"));
    if (!QFile::copy(sample, input)) {
        qCritical() << "Beispieldatei konnte nicht kopiert werden:" << sample;
        return 1;
    }

    bool ok = true;
    MainWindow window;
    window.resize(1100, 720);
    window.show();
    settle(300);

    // Schritt 1
    window.openInputFile(input);
    ok &= save(&window, outDir, prefix + QStringLiteral("1-eingabedatei.png"));

    // Schritt 2: Formate und Optionen, als Preset gespeichert
    window.setSelectedOutputFormats({"pdf", "html", "docx", "epub"});
    window.optionsWidget("pdf")->setValues({{"--toc", true},
                                            {"--number-sections", true},
                                            {"--pdf-engine", "xelatex"},
                                            {"V:papersize", "a4"},
                                            {"V:geometry", "margin=2.5cm"},
                                            {"V:colorlinks", true},
                                            {"M:lang", german ? "de-DE" : "en-US"}});
    window.optionsWidget("html")->setValues({{"--toc", true}, {"--embed-resources", true}});
    window.setOutputDirectory(outputFolder);
    const QString presetName = german ? QStringLiteral("Bericht A4") : QStringLiteral("Report A4");
    window.presetStore().save(window.currentPreset(presetName));
    window.loadPreset(presetName);

    window.setCurrentStep(MainWindow::FormatsStep);
    ok &= save(&window, outDir, prefix + QStringLiteral("2-ausgabeformate.png"));

    // Schritt 3: PDF-Optionen
    window.setCurrentStep(MainWindow::OptionsStep);
    if (auto *tabs = window.findChild<QTabWidget *>())
        tabs->setCurrentWidget(window.optionsWidget("pdf"));
    ok &= save(&window, outDir, prefix + QStringLiteral("3-optionen.png"));

    // Schritt 4
    window.setCurrentStep(MainWindow::OutputStep);
    ok &= save(&window, outDir, prefix + QStringLiteral("4-ausgabeordner.png"));

    // Schritt 5: echte Umwandlung
    window.setCurrentStep(MainWindow::RunStep);
    QEventLoop waitForConversion;
    int failedJobs = -1;
    QObject::connect(&window, &MainWindow::conversionFinished, &waitForConversion,
                     [&](int, int failed, bool) {
                         failedJobs = failed;
                         waitForConversion.quit();
                     });
    QTimer::singleShot(180000, &waitForConversion, &QEventLoop::quit);
    window.startConversion();
    waitForConversion.exec();
    if (failedJobs != 0) {
        qCritical() << "Umwandlung nicht vollständig erfolgreich (fehlgeschlagen:" << failedJobs << ")";
        ok = false;
    }
    ok &= save(&window, outDir, prefix + QStringLiteral("5-erzeugen.png"));

    // Hinweisdialog, falls pandoc fehlt
    PandocMissingDialog dialog(Pandoc::installHintForThisSystem());
    dialog.setDetails(QCoreApplication::translate("Pandoc", "Das Programm pandoc wurde nicht gefunden."));
    dialog.show();
    settle(300);
    ok &= save(&dialog, outDir, prefix + QStringLiteral("pandoc-fehlt.png"));

    demo.removeRecursively();
    return ok ? 0 : 1;
}
