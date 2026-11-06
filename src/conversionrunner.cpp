#include "conversionrunner.h"

#include <QDir>

ConversionRunner::ConversionRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] { m_output += m_process->readAll(); });
    connect(m_process, &QProcess::finished, this, &ConversionRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ConversionRunner::onProcessError);
}

ConversionRunner::~ConversionRunner()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void ConversionRunner::start(const QString &program, const QString &workingDirectory,
                             const QList<ConversionJob> &jobs)
{
    if (m_running)
        return;
    m_program = program;
    m_workingDirectory = workingDirectory;
    m_jobs = jobs;
    m_index = -1;
    m_succeeded = 0;
    m_failed = 0;
    m_cancelled = false;
    m_running = true;
    startNext();
}

void ConversionRunner::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void ConversionRunner::startNext()
{
    if (m_cancelled) {
        finish(true);
        return;
    }
    ++m_index;
    if (m_index >= m_jobs.size()) {
        finish(false);
        return;
    }

    const ConversionJob &job = m_jobs.at(m_index);
    m_output.clear();
    emit jobStarted(m_index, m_jobs.size(), job);
    emit logMessage(formatCommandLine(m_program, job.arguments));

    m_process->setWorkingDirectory(m_workingDirectory);
    m_process->start(m_program, job.arguments);
}

void ConversionRunner::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_output += m_process->readAll();
    const ConversionJob job = m_jobs.value(m_index);
    const QString output = QString::fromUtf8(m_output).trimmed();

    if (m_cancelled) {
        ++m_failed;
        emit jobFinished(m_index, job, false, tr("Abgebrochen."));
        finish(true);
        return;
    }

    const bool success = status == QProcess::NormalExit && exitCode == 0;
    if (success) {
        ++m_succeeded;
        emit jobFinished(m_index, job, true, output);
    } else {
        ++m_failed;
        QString message = output;
        if (status == QProcess::CrashExit)
            message = tr("pandoc wurde unerwartet beendet.") + QLatin1Char('\n') + message;
        else if (message.isEmpty())
            message = tr("pandoc endete mit Exit-Code %1.").arg(exitCode);
        emit jobFinished(m_index, job, false, message.trimmed());
    }

    // Nächsten Prozess erst nach Rückkehr aus dem finished-Signal starten
    QMetaObject::invokeMethod(this, &ConversionRunner::startNext, Qt::QueuedConnection);
}

void ConversionRunner::onProcessError(QProcess::ProcessError error)
{
    // Bei allen anderen Fehlern folgt noch das finished-Signal.
    if (error != QProcess::FailedToStart || !m_running)
        return;

    const QString message = tr("pandoc konnte nicht gestartet werden (%1): %2")
                                .arg(QDir::toNativeSeparators(m_program), m_process->errorString());
    // Ohne startbares pandoc schlagen alle verbleibenden Aufträge fehl.
    for (int i = m_index; i < m_jobs.size(); ++i) {
        ++m_failed;
        emit jobFinished(i, m_jobs.at(i), false, message);
    }
    m_index = m_jobs.size();
    finish(false);
}

void ConversionRunner::finish(bool cancelled)
{
    if (!m_running)
        return;
    m_running = false;
    emit finished(m_succeeded, m_failed, cancelled);
}

QString ConversionRunner::formatCommandLine(const QString &program, const QStringList &arguments)
{
    auto quote = [](const QString &arg) {
        if (arg.isEmpty())
            return QStringLiteral("\"\"");
        if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"'))
            || arg.contains(QLatin1Char('\t'))) {
            QString escaped = arg;
            escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
            return QLatin1Char('"') + escaped + QLatin1Char('"');
        }
        return arg;
    };

    QStringList parts;
    parts << quote(QDir::toNativeSeparators(program));
    for (const QString &arg : arguments)
        parts << quote(arg);
    return parts.join(QLatin1Char(' '));
}
