#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PandocUi"));
    QApplication::setApplicationName(QStringLiteral("PandocUi"));
    QApplication::setApplicationDisplayName(QStringLiteral("PandocUi"));
    QApplication::setApplicationVersion(QStringLiteral(PANDOCUI_VERSION));

    // Qt-eigene Dialoge (Ja/Nein, Dateiauswahl …) in der Systemsprache anzeigen
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Grafische Oberfläche für pandoc"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("datei"),
                                 QStringLiteral("Optional: Eingabedatei, die direkt geöffnet wird."));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openInputFile(files.first());

    return app.exec();
}
