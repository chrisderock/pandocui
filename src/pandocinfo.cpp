#include "pandocinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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

} // namespace Pandoc
