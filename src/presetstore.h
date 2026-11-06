#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

// Ein Preset fasst die gewählten Ausgabeformate (in Reihenfolge) und
// deren Optionen zusammen. Eingabedatei und Ausgabeordner gehören nicht dazu.
struct Preset
{
    QString name;
    QStringList formats;
    QHash<QString, QVariantMap> options; // Format -> Optionswerte
};

// Speichert Presets als JSON-Dateien in einem Verzeichnis
// (standardmäßig im Anwendungsdatenordner unter "presets").
class PresetStore
{
public:
    explicit PresetStore(const QString &directory = defaultDirectory());

    static QString defaultDirectory();
    QString directory() const { return m_directory; }

    QStringList names() const; // alphabetisch sortiert
    bool contains(const QString &name) const;

    std::optional<Preset> load(const QString &name, QString *error = nullptr) const;
    bool save(const Preset &preset, QString *error = nullptr) const;
    bool remove(const QString &name, QString *error = nullptr) const;

    // Austauschformat für Import und Export
    static QByteArray toJson(const Preset &preset);
    static std::optional<Preset> fromJson(const QByteArray &json, QString *error = nullptr);
    static bool exportToFile(const Preset &preset, const QString &path, QString *error = nullptr);
    static std::optional<Preset> importFromFile(const QString &path, QString *error = nullptr);

private:
    QString filePath(const QString &name) const;

    QString m_directory;
};
