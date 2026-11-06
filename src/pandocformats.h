#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

// Beschreibung einer einzelnen pandoc-Option, aus der die Oberfläche
// ein Eingabeelement erzeugt und die Kommandozeile zusammengesetzt wird.
struct OptionDef
{
    enum class Type {
        Bool,   // Kontrollkästchen
        Int,    // Zahlenfeld, 0 = "Standard" (nicht übergeben)
        Choice, // Auswahlliste, leerer Wert = "Standard"
        Text,   // Freitext, leer = nicht übergeben
        File    // Dateiauswahl, leer = nicht übergeben
    };
    enum class Style {
        Switch,       // --flag                (nur Bool)
        Value,        // --flag=wert
        ChoiceSwitch, // --<gewählter Wert>    (z. B. --mathjax)
        Variable,     // -V name=wert
        Metadata      // -M name=wert
    };

    QString id;
    QString label;
    QString toolTip;
    Type type = Type::Bool;
    Style style = Style::Switch;
    QString flag;        // Option (Switch/Value) bzw. Variablen-/Metadatenname
    QVariant defaultValue;
    QStringList choices; // für Choice
    int minimum = 0;
    int maximum = 100;
    QString fileFilter;  // für File
    QString placeholder; // für Text/File
};

struct OptionGroup
{
    QString title;
    QList<OptionDef> options;
};

namespace PandocFormats {

// Erkennt das Eingabeformat anhand der Dateiendung und – bei mehrdeutigen
// Endungen wie .xml oder .json – anhand des Dateiinhalts.
// Ist "available" nicht leer, werden nur dort enthaltene Formate geliefert.
// Liefert einen leeren String, wenn das Format nicht erkannt wurde.
QString detectInputFormat(const QString &filePath,
                          const QStringList &available = {},
                          QString *reason = nullptr);

// Dateiendung inkl. Punkt für ein Ausgabeformat, z. B. ".html".
QString outputExtension(const QString &format);

// Dateiname der Ausgabe. Teilen sich mehrere gewählte Formate dieselbe Endung,
// erhalten alle bis auf eines den Formatnamen als Zusatz (z. B. "doc.gfm.md").
QString outputFileName(const QString &baseName, const QString &format,
                       const QStringList &selectedFormats);

// Deutschsprachige Kurzbeschreibung eines Formats (leer, wenn unbekannt).
QString description(const QString &format);

// Häufig benutzte Ausgabeformate, die in der Liste oben stehen.
QStringList commonOutputFormats();

// Formatlisten, falls pandoc nicht befragt werden kann.
QStringList fallbackInputFormats();
QStringList fallbackOutputFormats();

// Optionsgruppen, die für ein Ausgabeformat angeboten werden.
QList<OptionGroup> optionGroups(const QString &outputFormat);

// Schlüssel für zusätzliche, frei eingegebene Kommandozeilenargumente.
inline const char *extraArgumentsKey() { return "extra-args"; }

// Wandelt die gewählten Optionswerte in pandoc-Argumente um.
// Nicht enthaltene Werte werden durch den Standardwert ersetzt.
QStringList optionArguments(const QString &outputFormat, const QVariantMap &values);

} // namespace PandocFormats
