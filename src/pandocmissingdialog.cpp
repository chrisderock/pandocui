#include "pandocmissingdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

PandocMissingDialog::PandocMissingDialog(const Pandoc::InstallHint &hint, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("pandoc nicht gefunden"));
    setObjectName(QStringLiteral("pandocMissingDialog"));

    auto *layout = new QGridLayout(this);
    layout->setHorizontalSpacing(16);

    auto *icon = new QLabel;
    const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(iconSize, iconSize));
    icon->setAlignment(Qt::AlignTop);
    layout->addWidget(icon, 0, 0, 7, 1);

    auto *intro = new QLabel(tr("<b>PandocUi benötigt das Programm pandoc, um Dokumente umzuwandeln.</b><br>"
                                "Es wurde auf diesem System nicht gefunden."));
    intro->setWordWrap(true);
    layout->addWidget(intro, 0, 1);

    auto *system = new QLabel(tr("Erkanntes System: %1 (%2)")
                                  .arg(QSysInfo::prettyProductName().toHtmlEscaped(),
                                       hint.systemName.toHtmlEscaped()));
    system->setWordWrap(true);
    layout->addWidget(system, 1, 1);

    int row = 2;
    if (!hint.commands.isEmpty()) {
        layout->addWidget(new QLabel(tr("Installation im Terminal:")), row++, 1);

        m_commands = new QPlainTextEdit(hint.commands.join(QLatin1Char('\n')));
        m_commands->setObjectName(QStringLiteral("installCommands"));
        m_commands->setReadOnly(true);
        m_commands->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_commands->setLineWrapMode(QPlainTextEdit::NoWrap);
        const int lines = int(hint.commands.size());
        m_commands->setFixedHeight(m_commands->fontMetrics().lineSpacing() * lines + 16);

        auto *copy = new QPushButton(tr("Kopieren"));
        copy->setToolTip(tr("Befehle in die Zwischenablage kopieren"));
        connect(copy, &QPushButton::clicked, this, [this, copy] {
            QApplication::clipboard()->setText(commandText());
            copy->setText(tr("Kopiert ✓"));
        });

        auto *commandRow = new QHBoxLayout;
        commandRow->addWidget(m_commands, 1);
        commandRow->addWidget(copy, 0, Qt::AlignTop);
        layout->addLayout(commandRow, row++, 1);
    }

    auto *note = new QLabel(hint.note.toHtmlEscaped() + QStringLiteral("<br>")
                            + tr("Weitere Installationswege: <a href=\"%1\">%1</a>")
                                  .arg(Pandoc::installUrl()));
    note->setWordWrap(true);
    note->setOpenExternalLinks(true);
    note->setTextFormat(Qt::RichText);
    layout->addWidget(note, row++, 1);

    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setEnabled(false);
    m_details->setVisible(false);
    layout->addWidget(m_details, row++, 1);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setVisible(false);
    layout->addWidget(m_status, row++, 1);

    auto *buttons = new QDialogButtonBox;
    auto *recheck = buttons->addButton(tr("Erneut prüfen"), QDialogButtonBox::ActionRole);
    recheck->setDefault(true);
    auto *choose = buttons->addButton(tr("pandoc auswählen …"), QDialogButtonBox::ActionRole);
    auto *website = buttons->addButton(tr("Installationsseite öffnen"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(recheck, &QPushButton::clicked, this, &PandocMissingDialog::recheckRequested);
    connect(choose, &QPushButton::clicked, this, &PandocMissingDialog::chooseExecutableRequested);
    connect(website, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(Pandoc::installUrl()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons, row, 0, 1, 2);
    layout->setColumnStretch(1, 1);

    setMinimumWidth(560);
}

void PandocMissingDialog::setDetails(const QString &details)
{
    m_details->setText(tr("Details: %1").arg(details));
    m_details->setVisible(!details.isEmpty());
}

void PandocMissingDialog::setStatus(const QString &status)
{
    m_status->setText(QStringLiteral("<span style=\"color:#d68910\">%1</span>").arg(status.toHtmlEscaped()));
    m_status->setVisible(!status.isEmpty());
}

QString PandocMissingDialog::commandText() const
{
    return m_commands ? m_commands->toPlainText() : QString();
}
