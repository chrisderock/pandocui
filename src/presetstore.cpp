#include "presetstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {

constexpr int FormatVersion = 1;
const QLatin1String VersionKey("pandocui-preset");

QString tr(const char *text)
{
    return QCoreApplication::translate("PresetStore", text);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

PresetStore::PresetStore(const QString &directory)
    : m_directory(directory)
{
}

QString PresetStore::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/presets");
}

QString PresetStore::filePath(const QString &name) const
{
    // Prozentkodierung macht beliebige Namen (auch mit / : ? *) zu gültigen Dateinamen.
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(name.trimmed(), " "));
    return QDir(m_directory).filePath(encoded + QStringLiteral(".json"));
}

QStringList PresetStore::names() const
{
    QStringList result;
    const QDir dir(m_directory);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &file : files) {
        const QString base = file.chopped(5); // ".json"
        result << QUrl::fromPercentEncoding(base.toLatin1());
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool PresetStore::contains(const QString &name) const
{
    return QFile::exists(filePath(name));
}

std::optional<Preset> PresetStore::load(const QString &name, QString *error) const
{
    std::optional<Preset> preset = importFromFile(filePath(name), error);
    if (preset)
        preset->name = name.trimmed(); // Dateiname ist maßgeblich
    return preset;
}

bool PresetStore::save(const Preset &preset, QString *error) const
{
    if (preset.name.trimmed().isEmpty()) {
        setError(error, tr("Das Preset benötigt einen Namen."));
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        setError(error, tr("Der Ordner „%1“ konnte nicht angelegt werden.")
                            .arg(QDir::toNativeSeparators(m_directory)));
        return false;
    }
    Preset copy = preset;
    copy.name = preset.name.trimmed();
    return exportToFile(copy, filePath(copy.name), error);
}

bool PresetStore::remove(const QString &name, QString *error) const
{
    QFile file(filePath(name));
    if (!file.exists() || file.remove())
        return true;
    setError(error, file.errorString());
    return false;
}

QByteArray PresetStore::toJson(const Preset &preset)
{
    QJsonObject options;
    for (const QString &format : preset.formats)
        options.insert(format, QJsonObject::fromVariantMap(preset.options.value(format)));

    QJsonObject root;
    root.insert(VersionKey, FormatVersion);
    root.insert(QStringLiteral("name"), preset.name);
    root.insert(QStringLiteral("formats"), QJsonArray::fromStringList(preset.formats));
    root.insert(QStringLiteral("options"), options);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<Preset> PresetStore::fromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, tr("Ungültiges JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    if (!doc.isObject() || !root.contains(VersionKey)) {
        setError(error, tr("Die Datei ist kein PandocUi-Preset."));
        return std::nullopt;
    }
    if (root.value(VersionKey).toInt() > FormatVersion) {
        setError(error, tr("Das Preset stammt aus einer neueren PandocUi-Version."));
        return std::nullopt;
    }

    Preset preset;
    preset.name = root.value(QStringLiteral("name")).toString().trimmed();
    const QJsonArray formats = root.value(QStringLiteral("formats")).toArray();
    for (const QJsonValue &value : formats) {
        const QString format = value.toString().trimmed();
        if (!format.isEmpty() && !preset.formats.contains(format))
            preset.formats << format;
    }
    const QJsonObject options = root.value(QStringLiteral("options")).toObject();
    for (const QString &format : std::as_const(preset.formats))
        preset.options.insert(format, options.value(format).toObject().toVariantMap());
    return preset;
}

bool PresetStore::exportToFile(const Preset &preset, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.write(toJson(preset));
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

std::optional<Preset> PresetStore::importFromFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return std::nullopt;
    }
    std::optional<Preset> preset = fromJson(file.readAll(), error);
    if (preset && preset->name.isEmpty())
        preset->name = QFileInfo(path).completeBaseName();
    return preset;
}
