#include "pandocinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Pandoc {

namespace {

bool run(const QString &exe, const QStringList &args, QString *out, QString *err)
{
    QProcess process;
    process.start(exe, args);
    if (!process.waitForStarted(5000)) {
        *err = process.errorString();
        return false;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(2000);
        *err = QCoreApplication::translate("Pandoc", "Zeitüberschreitung beim Aufruf von pandoc.");
        return false;
    }
    *out = QString::fromUtf8(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

QStringList splitList(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return text.split(whitespace, Qt::SkipEmptyParts);
}

} // namespace

QString findExecutable(const QString &configuredPath)
{
    if (!configuredPath.isEmpty()) {
        const QFileInfo fi(configuredPath);
        if (fi.isFile() && fi.isExecutable())
            return fi.absoluteFilePath();
        const QString found = QStandardPaths::findExecutable(configuredPath);
        if (!found.isEmpty())
            return found;
    }

    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
    if (!inPath.isEmpty())
        return inPath;

    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Pandoc/pandoc.exe")
               << qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Pandoc/pandoc.exe");
#else
    candidates << QStringLiteral("/usr/local/bin/pandoc")
               << QStringLiteral("/opt/homebrew/bin/pandoc")
               << QDir::homePath() + QStringLiteral("/.local/bin/pandoc")
               << QDir::homePath() + QStringLiteral("/.cabal/bin/pandoc");
#endif
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo(candidate).isFile())
            return QDir::cleanPath(candidate);
    }
    return {};
}

Info query(const QString &executable)
{
    Info info;
    info.executable = executable;
    if (executable.isEmpty()) {
        info.error = QCoreApplication::translate("Pandoc", "Das Programm pandoc wurde nicht gefunden.");
        return info;
    }

    QString out;
    QString err;
    if (!run(executable, {QStringLiteral("--version")}, &out, &err)) {
        info.error = err.isEmpty()
            ? QCoreApplication::translate("Pandoc", "pandoc konnte nicht ausgeführt werden.")
            : err;
        return info;
    }
    info.version = out.section(QLatin1Char('\n'), 0, 0).trimmed();

    if (run(executable, {QStringLiteral("--list-input-formats")}, &out, &err))
        info.inputFormats = splitList(out);
    if (run(executable, {QStringLiteral("--list-output-formats")}, &out, &err))
        info.outputFormats = splitList(out);

    info.ok = true;
    return info;
}

QString installUrl()
{
    return QStringLiteral("https://pandoc.org/installing.html");
}

QStringList osReleaseIdLike()
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("ID_LIKE="))) {
            QString value = line.mid(8);
            value.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
            return value.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
    }
#endif
    return {};
}

InstallHint installHint(const QString &productType, const QStringList &idLike)
{
    const QString id = productType.toLower();
    auto is = [&](std::initializer_list<const char *> ids) {
        for (const char *candidate : ids) {
            const QString c = QString::fromLatin1(candidate);
            if (id == c || id.startsWith(c + QLatin1Char('-')) || idLike.contains(c))
                return true;
        }
        return false;
    };
    const QString restart = QCoreApplication::translate(
        "Pandoc", "Nach der Installation „Erneut prüfen“ wählen.");

    InstallHint hint;
    if (id == QLatin1String("windows")) {
        hint.systemName = QStringLiteral("Windows");
        hint.commands = {QStringLiteral("winget install --id JohnMacFarlane.Pandoc")};
        hint.note = QCoreApplication::translate(
                        "Pandoc", "Alternativ das Installationsprogramm (.msi) von pandoc.org "
                                  "herunterladen und ausführen.")
                    + QLatin1Char(' ') + restart;
    } else if (id == QLatin1String("macos") || id == QLatin1String("osx")) {
        hint.systemName = QStringLiteral("macOS");
        hint.commands = {QStringLiteral("brew install pandoc")};
        hint.note = QCoreApplication::translate(
                        "Pandoc", "Der Befehl setzt Homebrew (brew.sh) voraus. Alternativ das "
                                  "Installationspaket (.pkg) von pandoc.org verwenden.")
                    + QLatin1Char(' ') + restart;
    } else if (id == QLatin1String("fedora")) {
        hint.systemName = QStringLiteral("Fedora");
        hint.commands = {QStringLiteral("sudo dnf install pandoc")};
        hint.note = restart;
    } else if (is({"rocky", "rhel", "almalinux", "centos", "ol"})) {
        hint.systemName = QStringLiteral("Rocky Linux / RHEL");
        hint.commands = {QStringLiteral("sudo dnf install epel-release"),
                         QStringLiteral("sudo dnf install pandoc")};
        hint.note = QCoreApplication::translate(
                        "Pandoc", "pandoc stammt bei diesen Systemen aus dem EPEL-Repository.")
                    + QLatin1Char(' ') + restart;
    } else if (is({"debian", "ubuntu", "linuxmint", "pop", "raspbian", "kali", "elementary"})) {
        hint.systemName = QStringLiteral("Debian / Ubuntu");
        hint.commands = {QStringLiteral("sudo apt install pandoc")};
        hint.note = restart;
    } else if (is({"opensuse", "sles", "suse"})) {
        hint.systemName = QStringLiteral("openSUSE");
        hint.commands = {QStringLiteral("sudo zypper install pandoc")};
        hint.note = restart;
    } else if (is({"arch", "manjaro", "endeavouros"})) {
        hint.systemName = QStringLiteral("Arch Linux");
        hint.commands = {QStringLiteral("sudo pacman -S pandoc")};
        hint.note = restart;
    } else {
        hint.systemName = productType.isEmpty() ? QStringLiteral("Linux") : productType;
        hint.note = QCoreApplication::translate(
                        "Pandoc", "pandoc lässt sich meist über die Paketverwaltung des Systems "
                                  "installieren; vorgefertigte Pakete gibt es auf pandoc.org.")
                    + QLatin1Char(' ') + restart;
    }
    return hint;
}

InstallHint installHintForThisSystem()
{
    return installHint(QSysInfo::productType(), osReleaseIdLike());
}

} // namespace Pandoc
