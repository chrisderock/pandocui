#pragma once

#include <QString>
#include <QStringList>

// Auffinden des pandoc-Programms und Abfrage der unterstützten Formate.
namespace Pandoc {

struct Info
{
    bool ok = false;
    QString executable;
    QString version;           // z. B. "pandoc 3.1.11.1"
    QStringList inputFormats;  // Ausgabe von --list-input-formats
    QStringList outputFormats; // Ausgabe von --list-output-formats
    QString error;
};

// Sucht pandoc: zuerst den konfigurierten Pfad, dann PATH, dann übliche Installationsorte.
QString findExecutable(const QString &configuredPath = {});

// Startet pandoc synchron und liest Version und Formatlisten aus.
Info query(const QString &executable);

} // namespace Pandoc
