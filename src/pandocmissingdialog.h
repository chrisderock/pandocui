#pragma once

#include "pandocinfo.h"

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Hinweisdialog, wenn pandoc nicht gefunden wurde: zeigt systemabhängige
// Installationsbefehle und bietet "Erneut prüfen" sowie "pandoc auswählen" an.
class PandocMissingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PandocMissingDialog(const Pandoc::InstallHint &hint, QWidget *parent = nullptr);

    // Technische Details (z. B. Fehlermeldung beim Aufruf) und Statuszeile
    void setDetails(const QString &details);
    void setStatus(const QString &status);

    QString commandText() const;

signals:
    void recheckRequested();
    void chooseExecutableRequested();

private:
    QPlainTextEdit *m_commands = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_status = nullptr;
};
