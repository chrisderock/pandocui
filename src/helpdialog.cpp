#include "helpdialog.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

HelpDialog::HelpDialog(QWidget *parent, const QString &language)
    : QDialog(parent)
{
    setWindowTitle(tr("PandocUi – Handbuch"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    resize(980, 780);

    m_browser = new QTextBrowser;
    m_browser->setOpenLinks(false); // Sprungmarken und externe Links selbst behandeln
    m_browser->installEventFilter(this);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &HelpDialog::openLink);

    auto *home = new QPushButton(style()->standardIcon(QStyle::SP_ArrowUp), tr("&Inhalt"));
    home->setToolTip(tr("Zum Inhaltsverzeichnis springen"));
    home->setAutoDefault(false);
    connect(home, &QPushButton::clicked, this,
            [this] { m_browser->verticalScrollBar()->setValue(0); });

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Im Handbuch suchen …"));
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(tr("Eingabe: nächster Treffer, Umschalt+Eingabe: vorheriger Treffer"));
    m_searchStatus = new QLabel;
    connect(m_search, &QLineEdit::returnPressed, this, [this] { findText(m_search->text()); });
    connect(m_search, &QLineEdit::textEdited, this, [this] { m_searchStatus->clear(); });
    auto *findPrevious = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), m_search);
    findPrevious->setContext(Qt::WidgetShortcut);
    connect(findPrevious, &QShortcut::activated, this,
            [this] { findText(m_search->text(), true); });
    auto *focusSearch = new QShortcut(QKeySequence::Find, this);
    connect(focusSearch, &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(home);
    toolbar->addStretch();
    toolbar->addWidget(m_searchStatus);
    toolbar->addWidget(m_search, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_browser, 1);
    layout->addWidget(buttons);

    const QString lang = language.isEmpty() ? QLocale().name().left(2) : language;
    m_browser->setSource(manualUrl(lang), QTextDocument::MarkdownResource);
    fitImages();
}

QUrl HelpDialog::manualUrl(const QString &language)
{
    return QUrl(language.startsWith(QLatin1String("de"))
                    ? QStringLiteral("qrc:/manual/BENUTZERHANDBUCH.md")
                    : QStringLiteral("qrc:/manual/USER_MANUAL.md"));
}

QString HelpDialog::headingAnchor(const QString &heading)
{
    // Wie GitHub: Kleinbuchstaben, Leerzeichen → "-", Satzzeichen entfallen
    QString anchor;
    const QString text = heading.trimmed().toLower();
    for (const QChar c : text) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            anchor += c;
        else if (c.isSpace())
            anchor += QLatin1Char('-');
    }
    return anchor;
}

bool HelpDialog::scrollToAnchor(const QString &anchor)
{
    // Der Markdown-Import von Qt legt für Überschriften keine Sprungmarken an:
    // die Überschrift mit passendem Text suchen.
    QTextDocument *doc = m_browser->document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() == 0 || headingAnchor(block.text()) != anchor)
            continue;
        m_browser->setTextCursor(QTextCursor(block));
        const QRectF rect = doc->documentLayout()->blockBoundingRect(block);
        m_browser->verticalScrollBar()->setValue(qRound(rect.top()));
        return true;
    }
    return false;
}

bool HelpDialog::findText(const QString &text, bool backward)
{
    if (text.isEmpty()) {
        m_searchStatus->clear();
        return false;
    }
    const QTextDocument::FindFlags flags = backward ? QTextDocument::FindBackward
                                                    : QTextDocument::FindFlags();
    bool found = m_browser->find(text, flags);
    if (!found) {
        // Am Ende bzw. Anfang angekommen: von der anderen Seite weitersuchen
        QTextCursor cursor(m_browser->document());
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_browser->setTextCursor(cursor);
        found = m_browser->find(text, flags);
    }
    m_searchStatus->setText(found ? QString()
                                  : tr("<span style=\"color:#c0392b\">Nicht gefunden</span>"));
    return found;
}

void HelpDialog::openLink(const QUrl &url)
{
    if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) {
        scrollToAnchor(url.fragment());
        return;
    }
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("mailto"))
        QDesktopServices::openUrl(url);
}

bool HelpDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_browser && event->type() == QEvent::Resize)
        fitImages();
    return QDialog::eventFilter(watched, event);
}

void HelpDialog::fitImages()
{
    // Bildschirmfotos höchstens so breit wie der sichtbare Bereich darstellen
    // Die Breite der Bildlaufleiste immer abziehen: Sie erscheint oft erst nach dem Laden,
    // und die Bilder sollen dann nicht schon wieder zu breit sein.
    QTextDocument *doc = m_browser->document();
    const int scrollBar = m_browser->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr,
                                                          m_browser->verticalScrollBar());
    const qreal maxWidth = m_browser->contentsRect().width() - scrollBar
                           - 2 * doc->documentMargin() - 4;
    if (maxWidth < 50)
        return;

    struct Image { int position; int length; QTextImageFormat format; };
    QList<Image> images;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isImageFormat())
                images.append({fragment.position(), fragment.length(),
                               fragment.charFormat().toImageFormat()});
        }
    }

    for (Image &image : images) {
        const QUrl url = m_browser->source().resolved(QUrl(image.format.name()));
        const QSize natural = QImageReader(QLatin1Char(':') + url.path()).size();
        if (!natural.isValid() || natural.width() <= 0)
            continue;
        const qreal width = qMin<qreal>(natural.width(), maxWidth);
        if (qAbs(image.format.width() - width) < 0.5)
            continue;
        image.format.setWidth(width);
        image.format.setHeight(natural.height() * width / natural.width());
        QTextCursor cursor(doc);
        cursor.setPosition(image.position);
        cursor.setPosition(image.position + image.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(image.format);
    }
}
