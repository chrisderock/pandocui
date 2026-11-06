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

// Hinweise zur Installation von pandoc für ein bestimmtes System.
struct InstallHint
{
    QString systemName;   // z. B. "Windows", "macOS", "Debian/Ubuntu"
    QStringList commands; // Befehle für das Terminal (leer, wenn keine bekannt)
    QString note;         // ergänzender Hinweis
};

// Ermittelt die Hinweise anhand von QSysInfo::productType() und – unter Linux –
// ID_LIKE aus /etc/os-release (für abgeleitete Distributionen wie Linux Mint).
InstallHint installHint(const QString &productType, const QStringList &idLike = {});
InstallHint installHintForThisSystem();

// Einträge von ID_LIKE aus /etc/os-release (leer außerhalb von Linux).
QStringList osReleaseIdLike();

// Offizielle Installationsseite von pandoc.
QString installUrl();

} // namespace Pandoc
