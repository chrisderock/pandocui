#include "mainwindow.h"
#include "translation.h"

#include <QApplication>
#include <QCommandLineParser>

namespace {

// Wert von --lang bzw. --lang=xx vorab lesen, damit schon die Hilfe übersetzt ist.
QString requestedLanguage(const QStringList &arguments)
{
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &arg = arguments.at(i);
        if (arg.startsWith(QLatin1String("--lang=")))
            return arg.mid(7);
        if (arg == QLatin1String("--lang") && i + 1 < arguments.size())
            return arguments.at(i + 1);
    }
    return Translation::languageSetting();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PandocUi"));
    QApplication::setApplicationName(QStringLiteral("PandocUi"));
    QApplication::setApplicationDisplayName(QStringLiteral("PandocUi"));
    QApplication::setApplicationVersion(QStringLiteral(PANDOCUI_VERSION));

    Translation::install(&app, requestedLanguage(QApplication::arguments()));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Grafische Oberfläche für pandoc"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption langOption(
        QStringLiteral("lang"),
        QCoreApplication::translate("main", "Sprache der Oberfläche (%1).")
            .arg(Translation::availableLanguages().join(QStringLiteral(", "))),
        QCoreApplication::translate("main", "sprache"));
    parser.addOption(langOption);
    parser.addPositionalArgument(
        QCoreApplication::translate("main", "datei"),
        QCoreApplication::translate("main", "Optional: Eingabedatei, die direkt geöffnet wird."));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        window.openInputFile(files.first());

    return app.exec();
}
