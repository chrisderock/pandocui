#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

// Sprachauswahl der Anwendung. Quellsprache der Texte ist Deutsch,
// dazu gibt es eine englische Übersetzung (translations/pandocui_en.ts).
namespace Translation {

// Unterstützte Sprachen als ISO-639-Codes, z. B. {"de", "en"}.
QStringList availableLanguages();

// Anzeigename einer Sprache in der jeweiligen Sprache selbst ("Deutsch", "English").
QString nativeName(const QString &language);

// Gespeicherte Einstellung: "" = Systemsprache, sonst ein Sprachcode.
QString languageSetting();
void setLanguageSetting(const QString &language);

// Ermittelt die tatsächlich verwendete Sprache. Leer = Systemsprache;
// Systemsprachen ohne eigene Übersetzung fallen auf Englisch zurück.
QString resolve(const QString &requested);

// Installiert die Übersetzer (Anwendung und Qt-Dialoge) für die gewünschte
// Sprache und liefert die tatsächlich verwendete Sprache zurück.
QString install(QCoreApplication *app, const QString &requested);

} // namespace Translation
