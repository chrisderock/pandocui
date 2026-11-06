#pragma once

#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>

struct ConversionJob
{
    QString format;       // Ausgabeformat
    QStringList arguments; // vollständige pandoc-Argumente
    QString outputPath;   // Zieldatei
};

// Führt die Konvertierungen nacheinander als pandoc-Prozesse aus.
class ConversionRunner : public QObject
{
    Q_OBJECT

public:
    explicit ConversionRunner(QObject *parent = nullptr);
    ~ConversionRunner() override;

    void start(const QString &program, const QString &workingDirectory,
               const QList<ConversionJob> &jobs);
    void cancel();
    bool isRunning() const { return m_running; }

    // Kommandozeile zur Anzeige, Argumente mit Leerzeichen werden in Anführungszeichen gesetzt.
    static QString formatCommandLine(const QString &program, const QStringList &arguments);

signals:
    void jobStarted(int index, int total, const ConversionJob &job);
    void jobFinished(int index, const ConversionJob &job, bool success, const QString &message);
    void finished(int succeeded, int failed, bool cancelled);
    void logMessage(const QString &message);

private:
    void startNext();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void finish(bool cancelled);

    QProcess *m_process;
    QString m_program;
    QString m_workingDirectory;
    QList<ConversionJob> m_jobs;
    QByteArray m_output;
    int m_index = -1;
    int m_succeeded = 0;
    int m_failed = 0;
    bool m_running = false;
    bool m_cancelled = false;
};
