#include "translation.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QTranslator>

namespace Translation {

namespace {

const QString SettingKey = QStringLiteral("language");
const QString SourceLanguage = QStringLiteral("de");

QPointer<QTranslator> s_appTranslator;
QPointer<QTranslator> s_qtTranslator;

void removeTranslator(QPointer<QTranslator> &translator)
{
    if (translator) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
    }
}

} // namespace

QStringList availableLanguages()
{
    return {QStringLiteral("de"), QStringLiteral("en")};
}

QString nativeName(const QString &language)
{
    if (language == QLatin1String("de"))
        return QStringLiteral("Deutsch");
    if (language == QLatin1String("en"))
        return QStringLiteral("English");
    return language;
}

QString languageSetting()
{
    return QSettings().value(SettingKey).toString();
}

void setLanguageSetting(const QString &language)
{
    QSettings().setValue(SettingKey, language);
}

QString resolve(const QString &requested)
{
    const QStringList available = availableLanguages();
    const QString wanted = requested.trimmed().toLower().left(2);
    if (available.contains(wanted))
        return wanted;

    // Systemsprache(n) in der Reihenfolge der Benutzervorlieben prüfen
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &ui : uiLanguages) {
        const QString code = ui.left(2).toLower();
        if (available.contains(code))
            return code;
    }
    return QStringLiteral("en");
}

QString install(QCoreApplication *app, const QString &requested)
{
    const QString language = resolve(requested);

    removeTranslator(s_appTranslator);
    removeTranslator(s_qtTranslator);

    const QLocale locale(language);
    QLocale::setDefault(locale);

    if (language != SourceLanguage) {
        auto *translator = new QTranslator(app);
        if (translator->load(QStringLiteral(":/i18n/pandocui_%1.qm").arg(language))) {
            QCoreApplication::installTranslator(translator);
            s_appTranslator = translator;
        } else {
            delete translator;
        }
    }

    // Texte der Qt-eigenen Dialoge (Ja/Nein, Dateiauswahl …): eingebettet beim statischen
    // Build, sonst aus dem Übersetzungsordner der Qt-Installation bzw. der Bereitstellung
    auto *qtTranslator = new QTranslator(app);
    if (qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                           QStringLiteral(":/i18n"))
        || qtTranslator->load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qtTranslator);
        s_qtTranslator = qtTranslator;
    } else {
        delete qtTranslator;
    }

    return language;
}

} // namespace Translation
